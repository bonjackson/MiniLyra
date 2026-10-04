#include "MiniTravelSubsystem.h"

#include "CommonLocalPlayer.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/PendingNetGame.h"
#include "Engine/World.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGameMode.h"
#include "GameModes/MiniGameState.h"
#include "GameFramework/GameSession.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Player/MiniPlayerController.h"
#include "System/MiniLogChannels.h"
#include "System/MiniUIManagerSubsystem.h"
#include "UI/MiniConnectionStatusWidget.h"
#include "UI/MiniGameUIPolicy.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "MiniTravel"

namespace
{
const FString MiniTravelPracticeMap(TEXT("/Game/Mini/Maps/L_MiniPractice"));
const FString MiniTravelArenaMap(TEXT("/Game/Mini/Maps/L_MiniArena"));
const FString MiniTravelFrontEndMap(TEXT("/Game/Mini/Maps/L_MiniFrontEnd"));
constexpr double MiniTravelTimeoutSeconds = 75.0;

bool MiniTravelMatchesMap(const UWorld* World, const FString& Map)
{
	return World && UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) == Map;
}

UMiniExperienceManagerComponent* MiniTravelExperience(UWorld* World)
{
	AMiniGameState* State = World ? World->GetGameState<AMiniGameState>() : nullptr;
	return State ? State->GetExperienceManagerComponent() : nullptr;
}
}

bool UMiniTravelSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UGameInstance* Instance = Cast<UGameInstance>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && Instance && !Instance->IsDedicatedServerInstance();
}

void UMiniTravelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMiniUIManagerSubsystem>();
	bInitialized = true;
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddWeakLambda(this,
			[this](UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Failure, const FString& Reason)
			{ HandleNetworkFailure(World, Driver, int32(Failure), Reason); });
		TravelFailureHandle = GEngine->OnTravelFailure().AddWeakLambda(this,
			[this](UWorld* World, ETravelFailure::Type Failure, const FString& Reason)
			{ HandleTravelFailure(World, int32(Failure), Reason); });
	}
	PreClientTravelHandle = GetGameInstance()->OnNotifyPreClientTravel().AddUObject(this, &ThisClass::HandlePreClientTravel);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::HandlePostLoadMap);
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &ThisClass::HandleWorldCleanup);
	BindPolicy();
}

void UMiniTravelSubsystem::Deinitialize()
{
	bInitialized = false;
	++RequestGeneration;
	StopWatchdog();
	ClearWorldBinding();
	RemoveModal();
	if (UMiniGameUIPolicy* BoundPolicy = Policy.Get())
	{
		BoundPolicy->OnRootLayoutReady.Remove(RootReadyHandle);
		BoundPolicy->OnRootLayoutUnavailable.Remove(RootUnavailableHandle);
	}
	Policy.Reset();
	CurrentRoot.Reset();
	RootReadyHandle.Reset();
	RootUnavailableHandle.Reset();
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	GetGameInstance()->OnNotifyPreClientTravel().Remove(PreClientTravelHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	NetworkFailureHandle.Reset();
	TravelFailureHandle.Reset();
	PreClientTravelHandle.Reset();
	PostLoadMapHandle.Reset();
	WorldCleanupHandle.Reset();
	LocalController.Reset();
	RequestSourceWorld.Reset();
	PendingRequestDriver.Reset();
	OnStateChanged.Clear();
	Super::Deinitialize();
}

AMiniPlayerController* UMiniTravelSubsystem::GetLocalController() const
{
	AMiniPlayerController* Controller = LocalController.Get();
	return IsValid(Controller) && Controller->IsLocalController() && Controller->GetWorld() == GetWorld() &&
		!Controller->IsActorBeingDestroyed() ? Controller : nullptr;
}

UMiniConnectionStatusWidget* UMiniTravelSubsystem::GetConnectionStatusWidget() const
{
	return Modal.Get();
}

void UMiniTravelSubsystem::BindPolicy()
{
	UMiniUIManagerSubsystem* Manager = GetGameInstance()->GetSubsystem<UMiniUIManagerSubsystem>();
	UMiniGameUIPolicy* Current = Manager ? Cast<UMiniGameUIPolicy>(Manager->GetCurrentUIPolicy()) : nullptr;
	if (!Current || Policy.Get() == Current) { return; }
	RemoveModal();
	if (UMiniGameUIPolicy* Previous = Policy.Get())
	{
		Previous->OnRootLayoutReady.Remove(RootReadyHandle);
		Previous->OnRootLayoutUnavailable.Remove(RootUnavailableHandle);
	}
	Policy = Current;
	RootReadyHandle = Current->OnRootLayoutReady.AddUObject(this, &ThisClass::HandleRootReady);
	RootUnavailableHandle = Current->OnRootLayoutUnavailable.AddUObject(this, &ThisClass::HandleRootUnavailable);
}

void UMiniTravelSubsystem::ObserveLocalController(AMiniPlayerController* Controller)
{
	if (!bInitialized || !IsValid(Controller) || !Controller->IsLocalController() ||
		Controller->GetGameInstance() != GetGameInstance()) { return; }
	LocalController = Controller;
	BindPolicy();
	ObserveWorld(Controller->GetWorld());
	if (UMiniGameUIPolicy* BoundPolicy = Policy.Get())
	{
		UCommonLocalPlayer* Player = Cast<UCommonLocalPlayer>(Controller->GetLocalPlayer());
		if (UMiniPrimaryGameLayout* Root = Cast<UMiniPrimaryGameLayout>(BoundPolicy->GetRootLayout(Player)))
		{
			if (Root->IsLayoutReady()) { HandleRootReady(Player, Root); }
		}
	}
}

void UMiniTravelSubsystem::ClearWorldBinding()
{
	if (UWorld* World = ObservedWorld.Get()) { World->GameStateSetEvent.Remove(GameStateSetHandle); }
	GameStateSetHandle.Reset();
	ObservedWorld.Reset();
	ObservedExperience.Reset();
	++WorldGeneration;
}

void UMiniTravelSubsystem::ObserveWorld(UWorld* World)
{
	if (!bInitialized || !World || !World->IsGameWorld() || World->bIsTearingDown ||
		World->GetGameInstance() != GetGameInstance()) { return; }
	if (ObservedWorld.Get() != World)
	{
		ClearWorldBinding();
		ObservedWorld = World;
		GameStateSetHandle = World->GameStateSetEvent.AddUObject(this, &ThisClass::BindGameState);
	}
	BindGameState(World->GetGameState());
}

void UMiniTravelSubsystem::BindGameState(AGameStateBase* GameState)
{
	UWorld* World = ObservedWorld.Get();
	AMiniGameState* MiniState = Cast<AMiniGameState>(GameState);
	UMiniExperienceManagerComponent* Manager = MiniState ? MiniState->GetExperienceManagerComponent() : nullptr;
	if (!bInitialized || !World || World->bIsTearingDown || !MiniState || MiniState->GetWorld() != World ||
		!Manager || ObservedExperience.Get() == Manager) { return; }
	ObservedExperience = Manager;
	const uint32 ExpectedWorldGeneration = WorldGeneration;
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	Manager->CallOrRegister_OnExperienceLoaded(FOnMiniExperienceLoaded::FDelegate::CreateWeakLambda(this,
		[this, WeakWorld, ExpectedWorldGeneration](const UMiniExperienceDefinition* Experience)
		{ HandleExperienceLoaded(Experience, WeakWorld, ExpectedWorldGeneration); }));
	Manager->CallOrRegister_OnExperienceFailed(FOnMiniExperienceFailed::FDelegate::CreateWeakLambda(this,
		[this, WeakWorld, ExpectedWorldGeneration](const FString& Reason)
		{ HandleExperienceFailed(Reason, WeakWorld, ExpectedWorldGeneration); }));
}

void UMiniTravelSubsystem::HandlePostLoadMap(UWorld* World)
{
	ObserveWorld(World);
}

void UMiniTravelSubsystem::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (World == ObservedWorld.Get())
	{
		ClearWorldBinding();
		LocalController.Reset();
		CurrentRoot.Reset();
		RemoveModal();
	}
}

bool UMiniTravelSubsystem::IsFrontEndReady() const
{
	AMiniPlayerController* Controller = GetLocalController();
	const UMiniExperienceManagerComponent* Manager = MiniTravelExperience(GetWorld());
	const UMiniExperienceDefinition* Experience = Manager ? Manager->GetCurrentExperience() : nullptr;
	return bInitialized && Controller && GetWorld()->GetNetMode() == NM_Standalone &&
		Experience && Experience->bIsFrontEnd;
}

bool UMiniTravelSubsystem::BeginRequest(EMiniTravelOperation Operation, const FString& Map, const FText& Status)
{
	if (!bInitialized || State.bBusy || !GetLocalController() || !GetWorld() || GetWorld()->bIsTearingDown) { return false; }
	++RequestGeneration;
	RequestSourceWorld = GetWorld();
	PendingRequestDriver.Reset();
	TargetMap = Map;
	State.bBusy = true;
	State.bHasError = false;
	State.FailureCode.Reset();
	State.Operation = Operation;
	State.StatusText = Status;
	State.DetailText = LOCTEXT("PleaseWait", "请稍候…");
	State.TargetAddress.Reset();
	StartWatchdog();
	CommitState();
	return State.bBusy && State.Operation == Operation;
}

bool UMiniTravelSubsystem::StartPractice()
{
	if (!IsFrontEndReady() || !BeginRequest(EMiniTravelOperation::Practice, MiniTravelPracticeMap,
		LOCTEXT("OpeningPractice", "正在进入训练场"))) { return false; }
	UGameplayStatics::OpenLevel(GetGameInstance(), FName(*MiniTravelPracticeMap), true);
	return true;
}

bool UMiniTravelSubsystem::HostArena()
{
	if (!IsFrontEndReady() || !BeginRequest(EMiniTravelOperation::Host, MiniTravelArenaMap,
		LOCTEXT("HostingArena", "正在创建竞技场"))) { return false; }
	UGameplayStatics::OpenLevel(GetGameInstance(), FName(*MiniTravelArenaMap), true, TEXT("listen"));
	return true;
}

bool UMiniTravelSubsystem::TryNormalizeAddress(const FString& Address, FString& OutAddress)
{
	OutAddress.Reset();
	const FString Trimmed = Address.TrimStartAndEnd();
	if (Trimmed != Address || Trimmed.IsEmpty() || Trimmed.Len() > 21) { return false; }
	for (TCHAR Char : Trimmed)
	{
		if ((Char < TEXT('0') || Char > TEXT('9')) && Char != TEXT('.') && Char != TEXT(':')) { return false; }
	}
	TArray<FString> Parts;
	Trimmed.ParseIntoArray(Parts, TEXT(":"), false);
	if (Parts.Num() < 1 || Parts.Num() > 2) { return false; }
	TArray<FString> Octets;
	Parts[0].ParseIntoArray(Octets, TEXT("."), false);
	if (Octets.Num() != 4) { return false; }
	int32 Values[4] = {};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (Octets[Index].IsEmpty() || Octets[Index].Len() > 3) { return false; }
		Values[Index] = FCString::Atoi(*Octets[Index]);
		if (Values[Index] < 0 || Values[Index] > 255) { return false; }
	}
	if ((Values[0] == 0 && Values[1] == 0 && Values[2] == 0 && Values[3] == 0) ||
		Values[0] >= 224 || Values[0] == 0) { return false; }
	int32 Port = 7777;
	if (Parts.Num() == 2)
	{
		if (Parts[1].IsEmpty() || Parts[1].Len() > 5) { return false; }
		Port = FCString::Atoi(*Parts[1]);
		if (Port < 1 || Port > 65535) { return false; }
	}
	OutAddress = FString::Printf(TEXT("%d.%d.%d.%d:%d"), Values[0], Values[1], Values[2], Values[3], Port);
	return true;
}

bool UMiniTravelSubsystem::JoinAddress(const FString& Address)
{
	if (!IsFrontEndReady() || State.bBusy) { return false; }
	FString Normalized;
	if (!TryNormalizeAddress(Address, Normalized))
	{
		SetError(TEXT("MINI_INVALID_ADDRESS"), LOCTEXT("AddressInvalid", "地址格式无效"),
			LOCTEXT("AddressHelp", "请输入 IPv4 地址与可选端口，例如 192.168.1.20:7777。"));
		return false;
	}
	if (!BeginRequest(EMiniTravelOperation::Join, MiniTravelArenaMap, LOCTEXT("JoiningArena", "正在连接竞技场"))) { return false; }
	State.TargetAddress = Normalized;
	State.DetailText = FText::Format(LOCTEXT("JoiningAddress", "正在连接 {0}…"), FText::FromString(Normalized));
	CommitState();
	if (!State.bBusy || State.Operation != EMiniTravelOperation::Join || !GetLocalController()) { return false; }
	GetLocalController()->ClientTravel(Normalized, TRAVEL_Absolute);
	return true;
}

bool UMiniTravelSubsystem::RestartArena()
{
	UWorld* World = GetWorld();
	const UMiniExperienceManagerComponent* Manager = MiniTravelExperience(World);
	const UMiniExperienceDefinition* Experience = Manager ? Manager->GetCurrentExperience() : nullptr;
	if (!World || World->GetNetMode() != NM_ListenServer || !Experience || Experience->bIsFrontEnd ||
		Manager->GetCurrentExperienceId().PrimaryAssetName != TEXT("DA_MiniArenaExperience") ||
		!World->NextURL.IsEmpty() || !BeginRequest(EMiniTravelOperation::Restart, MiniTravelArenaMap,
			LOCTEXT("RestartingArena", "正在重新开始竞技场"))) { return false; }
	if (!World->ServerTravel(MiniTravelArenaMap + TEXT("?listen"), true))
	{
		SetError(TEXT("MINI_TRAVEL_FAILED"), LOCTEXT("RestartFailed", "无法重新开始"),
			LOCTEXT("RestartFailedDetail", "服务器拒绝了地图旅行，请返回主菜单后重新创建。"));
		return false;
	}
	return true;
}

void UMiniTravelSubsystem::BeginReturn(bool bPreserveError)
{
	++RequestGeneration;
	RequestSourceWorld = GetWorld();
	TargetMap = MiniTravelFrontEndMap;
	PendingRequestDriver.Reset();
	State.bBusy = true;
	State.Operation = bQuitAfterReturn ? EMiniTravelOperation::Quit : EMiniTravelOperation::ReturnToFrontEnd;
	State.StatusText = bQuitAfterReturn ? LOCTEXT("Quitting", "正在退出游戏") : LOCTEXT("Returning", "正在返回主菜单");
	if (!bPreserveError)
	{
		State.bHasError = false;
		State.FailureCode.Reset();
		State.DetailText = LOCTEXT("LeavingGame", "正在离开当前对局…");
	}
	StartWatchdog();
	CommitState();
}

void UMiniTravelSubsystem::ReturnToFrontEnd()
{
	if (!bInitialized || !GetWorld() || GetWorld()->bIsTearingDown) { return; }
	if ((State.Operation == EMiniTravelOperation::ReturnToFrontEnd || State.Operation == EMiniTravelOperation::Quit) &&
		State.bBusy) { return; }
	BeginReturn(State.bHasError);
	UWorld* World = GetWorld();
	if (AMiniGameMode* Mode = World->GetAuthGameMode<AMiniGameMode>())
	{
		if (World->GetNetMode() == NM_ListenServer && Mode->GameSession)
		{
			Mode->GameSession->ReturnToMainMenuHost();
			return;
		}
	}
	GetGameInstance()->ReturnToMainMenu();
}

void UMiniTravelSubsystem::QuitGame()
{
	if (!bInitialized) { return; }
	const UWorld* World = GetWorld();
	const UMiniExperienceManagerComponent* Manager = MiniTravelExperience(GetWorld());
	// A permanently broken front-end resource must still have a native exit path.
	const bool bFailedStandaloneFrontEnd = World && World->GetNetMode() == NM_Standalone &&
		MiniTravelMatchesMap(World, MiniTravelFrontEndMap) && Manager && Manager->GetLoadState() == EMiniExperienceLoadState::Failed;
	if (!State.bBusy && (IsFrontEndReady() || bFailedStandaloneFrontEnd))
	{
		RequestExit();
		return;
	}
	bQuitAfterReturn = true;
	ReturnToFrontEnd();
}

void UMiniTravelSubsystem::RequestExit()
{
	StopWatchdog();
	UE_LOG(LogMiniExperience, Display, TEXT("MiniTravel QUIT: FrontEnd=%d PendingRequest=0"), IsFrontEndReady() ? 1 : 0);
	FPlatformMisc::RequestExit(false, TEXT("MiniFrontEndQuit"));
}

void UMiniTravelSubsystem::NotifyReturnReason(const FText& Reason)
{
	if (!bInitialized || Reason.IsEmpty()) { return; }
	State.bHasError = true;
	State.FailureCode = TEXT("MINI_HOST_LEFT");
	State.DetailText = Reason.ToString().Contains(TEXT("Host has left"))
		? LOCTEXT("HostLeft", "主机已结束对局。") : Reason;
	BeginReturn(true); // Super's return RPC will schedule the engine's closed travel.
}

void UMiniTravelSubsystem::DismissError()
{
	if (!bInitialized || State.bBusy) { return; }
	const UMiniExperienceManagerComponent* Manager = MiniTravelExperience(GetWorld());
	if (Manager && Manager->GetLoadState() == EMiniExperienceLoadState::Failed)
	{
		ReturnToFrontEnd();
		return;
	}
	State.bHasError = false;
	State.FailureCode.Reset();
	State.StatusText = FText::GetEmpty();
	State.DetailText = FText::GetEmpty();
	CommitState();
}

void UMiniTravelSubsystem::HandleExperienceLoaded(const UMiniExperienceDefinition* Experience,
	TWeakObjectPtr<UWorld> WorldKey, uint32 ExpectedWorldGeneration)
{
	UWorld* World = WorldKey.Get();
	if (!bInitialized || !Experience || !World || World->bIsTearingDown || WorldGeneration != ExpectedWorldGeneration ||
		ObservedWorld != WorldKey || MiniTravelExperience(World) != ObservedExperience.Get()) { return; }
	if (!State.bBusy) { RefreshModal(); return; }
	if (World == RequestSourceWorld.Get() || !MiniTravelMatchesMap(World, TargetMap)) { return; }
	if ((State.Operation == EMiniTravelOperation::Join && World->GetNetMode() != NM_Client) ||
		(State.Operation == EMiniTravelOperation::Host && World->GetNetMode() != NM_ListenServer) ||
		((State.Operation == EMiniTravelOperation::ReturnToFrontEnd || State.Operation == EMiniTravelOperation::Quit) &&
			(!Experience->bIsFrontEnd || World->GetNetMode() != NM_Standalone))) { return; }
	StopWatchdog();
	const EMiniTravelOperation CompletedOperation = State.Operation;
	State.bBusy = false;
	State.Operation = EMiniTravelOperation::None;
	State.StatusText = State.bHasError ? LOCTEXT("GameEnded", "对局已结束") : FText::GetEmpty();
	if (!State.bHasError) { State.DetailText = FText::GetEmpty(); }
	TargetMap.Reset();
	RequestSourceWorld.Reset();
	PendingRequestDriver.Reset();
	CommitState();
	UE_LOG(LogMiniExperience, Display, TEXT("MiniTravel READY: Operation=%d Generation=%u World=%s NetMode=%d FrontEnd=%d"),
		int32(CompletedOperation), RequestGeneration, *World->GetName(), int32(World->GetNetMode()), Experience->bIsFrontEnd ? 1 : 0);
	if (bQuitAfterReturn && Experience->bIsFrontEnd) { RequestExit(); }
}

void UMiniTravelSubsystem::HandleExperienceFailed(const FString& Reason,
	TWeakObjectPtr<UWorld> WorldKey, uint32 ExpectedWorldGeneration)
{
	UWorld* World = WorldKey.Get();
	if (!bInitialized || !World || World->bIsTearingDown || WorldGeneration != ExpectedWorldGeneration ||
		ObservedWorld != WorldKey) { return; }
	UE_LOG(LogMiniExperience, Display, TEXT("MiniTravel EXPERIENCE_FAILURE_DETAIL: %s"), *Reason);
	SetError(TEXT("MINI_EXPERIENCE_FAILED"), LOCTEXT("ExperienceFailed", "玩法加载失败"),
		LOCTEXT("ExperienceFailedHelp", "未能载入玩法资源，请返回主菜单后重试。若问题持续，请重新启动游戏。"));
}

void UMiniTravelSubsystem::HandleRootReady(UCommonLocalPlayer* Player, UMiniPrimaryGameLayout* Root)
{
	if (!bInitialized || !Player || Player->GetGameInstance() != GetGameInstance() || !Player->IsPrimaryPlayer() ||
		!Root || !Root->IsLayoutReady()) { return; }
	if (CurrentRoot.Get() != Root) { RemoveModal(); CurrentRoot = Root; }
	RefreshModal();
}

void UMiniTravelSubsystem::HandleRootUnavailable(UCommonLocalPlayer* Player, UMiniPrimaryGameLayout* Root)
{
	if (CurrentRoot.Get() == Root)
	{
		CurrentRoot.Reset();
		RemoveModal();
	}
}

void UMiniTravelSubsystem::CommitState()
{
	++State.Revision;
	const FMiniTravelState Snapshot = State;
	OnStateChanged.Broadcast(Snapshot);
	RefreshModal();
}

void UMiniTravelSubsystem::SetError(const FString& Code, const FText& Title, const FText& Detail)
{
	StopWatchdog();
	++RequestGeneration;
	State.bBusy = false;
	State.bHasError = true;
	State.FailureCode = Code;
	State.StatusText = Title;
	State.DetailText = Detail;
	State.Operation = EMiniTravelOperation::None;
	TargetMap.Reset();
	PendingRequestDriver.Reset();
	UE_LOG(LogMiniExperience, Display, TEXT("MiniTravel ERROR: Code=%s Generation=%u Reason=%s"),
		*Code, RequestGeneration, *Detail.ToString());
	CommitState();
}

void UMiniTravelSubsystem::RemoveModal()
{
	UMiniConnectionStatusWidget* Widget = Modal.Get();
	UMiniPrimaryGameLayout* Root = ModalRoot.Get();
	Modal.Reset();
	ModalRoot.Reset();
	if (Widget)
	{
		Widget->DeactivateWidget();
		if (Root) { Root->FindAndRemoveWidgetFromLayer(Widget); }
		else { Widget->RemoveFromParent(); }
	}
}

void UMiniTravelSubsystem::RefreshModal()
{
	if (bRefreshingModal) { return; }
	TGuardValue<bool> Guard(bRefreshingModal, true);
	UMiniPrimaryGameLayout* Root = CurrentRoot.Get();
	if (!bInitialized || (!State.bBusy && !State.bHasError) || !Root || !Root->IsLayoutReady())
	{
		RemoveModal();
		return;
	}
	if (UMiniConnectionStatusWidget* Existing = Modal.Get())
	{
		if (Existing->IsActivated() && ModalRoot.Get() == Root) { return; }
		RemoveModal();
	}
	UMiniConnectionStatusWidget* Added = Root->PushWidgetToLayerStack<UMiniConnectionStatusWidget>(
		UMiniPrimaryGameLayout::GetModalLayerTag(), UMiniConnectionStatusWidget::StaticClass());
	if (!Added) { return; }
	if (!bInitialized || CurrentRoot.Get() != Root || !Root->IsLayoutReady() || (!State.bBusy && !State.bHasError))
	{
		Added->DeactivateWidget();
		Root->FindAndRemoveWidgetFromLayer(Added);
		return;
	}
	Modal = Added;
	ModalRoot = Root;
}

bool UMiniTravelSubsystem::IsOwnNetworkContext(UWorld* World, UNetDriver* Driver) const
{
	// A retired gameplay driver can still broadcast during teardown. Only the
	// current world's registered driver may change this GameInstance's UI state.
	if (Driver && Driver->NetDriverName == NAME_GameNetDriver)
	{
		UWorld* DriverWorld = World ? World : Driver->GetWorld();
		return GEngine && DriverWorld && DriverWorld == GetWorld() &&
			DriverWorld->GetGameInstance() == GetGameInstance() &&
			GEngine->FindNamedNetDriver(DriverWorld, NAME_GameNetDriver) == Driver;
	}
	if (Driver && PendingRequestDriver.Get() == Driver) { return true; }
	if (GEngine && Driver)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.OwningGameInstance == GetGameInstance() && Context.PendingNetGame &&
				Context.PendingNetGame->NetDriver == Driver) { return true; }
		}
	}
	return false;
}

void UMiniTravelSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* Driver, int32 FailureType, const FString& Reason)
{
	const bool bHostCreationFailure = FailureType == int32(ENetworkFailure::NetDriverAlreadyExists) ||
		FailureType == int32(ENetworkFailure::NetDriverCreateFailure) ||
		FailureType == int32(ENetworkFailure::NetDriverListenFailure);
	if (bInitialized && World && World == GetWorld() && World->GetGameInstance() == GetGameInstance() &&
		State.bBusy && (State.Operation == EMiniTravelOperation::Host || State.Operation == EMiniTravelOperation::Restart) &&
		bHostCreationFailure &&
		((!Driver && FailureType == int32(ENetworkFailure::NetDriverCreateFailure)) ||
			(Driver && Driver->NetDriverName == NAME_GameNetDriver && IsOwnNetworkContext(World, Driver))))
	{
		UE_LOG(LogMiniExperience, Display, TEXT("MiniTravel HOST_FAILURE_DETAIL: Type=%d Driver=%s Reason=%s"),
			FailureType, *GetNameSafe(Driver), *Reason);
		SetError(TEXT("MINI_HOST_FAILED"), LOCTEXT("HostFailed", "无法创建服务器"),
			LOCTEXT("HostFailedHelp", "服务器未能启动，请关闭占用同一端口的程序后重试。若问题持续，请重新启动游戏。"));
		// A null-driver creation failure has no engine disconnect handler. Queue
		// the same safe closed travel; Browse and driver destruction happen later.
		if (!Driver && GEngine) { GEngine->SetClientTravel(World, TEXT("?closed"), TRAVEL_Absolute); }
		return;
	}
	if (!bInitialized || !Driver || (Driver->NetDriverName != NAME_GameNetDriver && Driver->NetDriverName != NAME_PendingNetDriver) ||
		!IsOwnNetworkContext(World, Driver)) { return; }
	if (Driver->GetNetMode() != NM_Client)
	{
		// An individual remote leaving never aborts its Listen Server.
		return;
	}
	if (State.bBusy && (State.Operation == EMiniTravelOperation::ReturnToFrontEnd || State.Operation == EMiniTravelOperation::Quit))
	{
		return; // An intentional leave or host return reason must not become a second generic error.
	}
	UE_LOG(LogMiniExperience, Display, TEXT("MiniTravel NETWORK_FAILURE_DETAIL: Type=%d Reason=%s"), FailureType, *Reason);
	if (Reason.Contains(TEXT("MINI_SERVER_FULL")))
	{
		SetError(TEXT("MINI_SERVER_FULL"), LOCTEXT("ServerFull", "服务器已满"), LOCTEXT("ServerFullDetail", "这场对局最多支持 4 名玩家，请稍后重试。"));
	}
	else if (FailureType == int32(ENetworkFailure::ConnectionTimeout))
	{
		SetError(TEXT("MINI_CONNECTION_TIMEOUT"), LOCTEXT("TimedOut", "连接超时"), LOCTEXT("TimedOutDetail", "服务器没有响应，请检查 IP、端口和网络后重试。"));
	}
	else if (FailureType == int32(ENetworkFailure::ConnectionLost))
	{
		SetError(TEXT("MINI_CONNECTION_LOST"), LOCTEXT("Disconnected", "与主机的连接已断开"), LOCTEXT("DisconnectedDetail", "主机已离开或网络连接中断，你可以重新创建或加入对局。"));
	}
	else if (FailureType == int32(ENetworkFailure::OutdatedClient) || FailureType == int32(ENetworkFailure::OutdatedServer))
	{
		SetError(TEXT("MINI_VERSION_MISMATCH"), LOCTEXT("VersionMismatch", "游戏版本不一致"),
			LOCTEXT("VersionMismatchDetail", "请确认你与主机使用相同版本的游戏后重试。"));
	}
	else
	{
		SetError(TEXT("MINI_CONNECTION_FAILED"), LOCTEXT("ConnectionFailed", "连接失败"),
			LOCTEXT("ConnectionFailedDetail", "无法建立连接，请检查主机地址与网络后重试。"));
	}
	// The engine has already scheduled ?closed. Do not Browse while this driver is ticking.
}

void UMiniTravelSubsystem::HandleTravelFailure(UWorld* World, int32 FailureType, const FString& Reason)
{
	if (!bInitialized || !World || World->GetGameInstance() != GetGameInstance()) { return; }
	if (State.bHasError) { return; } // Preserve the more specific network or RPC reason.
	UE_LOG(LogMiniExperience, Display, TEXT("MiniTravel TRAVEL_FAILURE_DETAIL: Type=%d Reason=%s"), FailureType, *Reason);
	SetError(TEXT("MINI_TRAVEL_FAILED"), LOCTEXT("TravelFailed", "无法打开对局"),
		LOCTEXT("TravelFailedHelp", "未能打开游戏场景，请返回主菜单后重试。"));
}

void UMiniTravelSubsystem::HandlePreClientTravel(const FString& URL, ETravelType Type, bool bSeamless)
{
	if (!bInitialized || State.bBusy || !GetLocalController() || bSeamless) { return; }
	FString Map = URL;
	FString Options;
	URL.Split(TEXT("?"), &Map, &Options);
	if (Map == MiniTravelArenaMap || Map == MiniTravelPracticeMap)
	{
		BeginRequest(EMiniTravelOperation::Restart, Map, LOCTEXT("FollowingServer", "正在载入下一场对局"));
	}
}

void UMiniTravelSubsystem::StartWatchdog()
{
	StopWatchdog();
	RequestDeadline = FPlatformTime::Seconds() + MiniTravelTimeoutSeconds;
	WatchdogHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickWatchdog), 0.1f);
}

void UMiniTravelSubsystem::StopWatchdog()
{
	FTSTicker::GetCoreTicker().RemoveTicker(WatchdogHandle);
	WatchdogHandle.Reset();
}

bool UMiniTravelSubsystem::TickWatchdog(float DeltaTime)
{
	if (!bInitialized || !State.bBusy) { WatchdogHandle.Reset(); return false; }
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.OwningGameInstance == GetGameInstance() && Context.PendingNetGame)
			{
				PendingRequestDriver = Context.PendingNetGame->NetDriver;
				break;
			}
		}
	}
	if (FPlatformTime::Seconds() < RequestDeadline) { return true; }
	const bool bWasLeaving = State.Operation == EMiniTravelOperation::ReturnToFrontEnd || State.Operation == EMiniTravelOperation::Quit;
	SetError(TEXT("MINI_CONNECTION_TIMEOUT"), LOCTEXT("OperationTimedOut", "操作超时"),
		LOCTEXT("OperationTimedOutDetail", "对局未能及时准备完成，请返回主菜单后重试。"));
	if (!bWasLeaving && GetWorld() && !GetWorld()->bIsTearingDown)
	{
		ReturnToFrontEnd(); // The core ticker runs outside NetDriver::TickDispatch.
		return WatchdogHandle.IsValid();
	}
	return false;
}

#undef LOCTEXT_NAMESPACE
