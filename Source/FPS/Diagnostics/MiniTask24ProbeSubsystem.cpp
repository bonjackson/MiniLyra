#include "MiniTask24ProbeSubsystem.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Arena/MiniArenaRulesComponent.h"
#include "Arena/MiniMatchRulesComponent.h"
#include "Arena/MiniMatchSubsystem.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniHeroComponent.h"
#include "CommonInputSubsystem.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/NetDriver.h"
#include "Engine/PendingNetGame.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "GameFramework/GameStateBase.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGameMode.h"
#include "GameModes/MiniGamePhaseSubsystem.h"
#include "GameModes/MiniGameState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Input/Events.h"
#include "Input/CommonUIActionRouterBase.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"
#include "System/MiniTravelSubsystem.h"
#include "Training/MiniPracticeTarget.h"
#include "UI/MiniConnectionStatusWidget.h"
#include "UI/MiniFrontEndWidget.h"
#include "UI/MiniHUDLayout.h"
#include "UI/MiniHUDViewModel.h"
#include "UI/MiniHUDWidgets.h"
#include "UI/MiniLoadingStatusWidget.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "UnrealClient.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Widgets/SWindow.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
AMiniPlayerController* Task24PC(UWorld* World)
{
	if (!World) { return nullptr; }
	for (TActorIterator<AMiniPlayerController> It(World); It; ++It) { if (It->IsLocalController()) { return *It; } }
	return nullptr;
}

AMiniPlayerState* Task24PS(AMiniPlayerController* PC) { return PC ? PC->GetPlayerState<AMiniPlayerState>() : nullptr; }
AMiniCharacter* Task24Pawn(AMiniPlayerController* PC) { return PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr; }
UMiniAbilitySystemComponent* Task24ASC(AMiniPlayerController* PC)
{
	const AMiniPlayerState* PS = Task24PS(PC);
	return PS ? PS->GetMiniAbilitySystemComponent() : nullptr;
}

UMiniPrimaryGameLayout* Task24Root(AMiniPlayerController* PC)
{
	return PC && PC->GetLocalPlayer() ? Cast<UMiniPrimaryGameLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(PC->GetLocalPlayer())) : nullptr;
}

template <typename T> T* Task24LayerWidget(UMiniPrimaryGameLayout* Root, FGameplayTag Tag)
{
	UCommonActivatableWidgetContainerBase* Layer = Root ? Root->GetLayerWidget(Tag) : nullptr;
	T* Result = nullptr;
	if (Layer)
	{
		for (UCommonActivatableWidget* Widget : Layer->GetWidgetList())
		{
			if (T* Candidate = Cast<T>(Widget); Candidate && Candidate->IsActivated())
			{
				if (Result) { return nullptr; }
				Result = Candidate;
			}
		}
	}
	return Result;
}

UMiniFrontEndWidget* Task24Front(AMiniPlayerController* PC)
{
	return Task24LayerWidget<UMiniFrontEndWidget>(Task24Root(PC), UMiniPrimaryGameLayout::GetMenuLayerTag());
}

UMiniHUDLayout* Task24HUD(AMiniPlayerController* PC)
{
	return Task24LayerWidget<UMiniHUDLayout>(Task24Root(PC), UMiniPrimaryGameLayout::GetGameLayerTag());
}

UMiniExperienceManagerComponent* Task24Experience(UWorld* World)
{
	const AMiniGameState* GS = World ? World->GetGameState<AMiniGameState>() : nullptr;
	return GS ? GS->GetExperienceManagerComponent() : nullptr;
}

bool Task24PlayerReady(AMiniPlayerController* PC)
{
	AMiniCharacter* Pawn = Task24Pawn(PC);
	UMiniAbilitySystemComponent* ASC = Task24ASC(PC);
	return Pawn && ASC && ASC->GetAvatarActor() == Pawn && ASC->GetOwnerActor() == Task24PS(PC) &&
		Pawn->GetHealthComponent() && !Pawn->GetHealthComponent()->IsDead() && Pawn->GetHeroComponent() &&
		(!PC->IsLocalController() || Pawn->GetHeroComponent()->IsInputActive()) && Pawn->GetEquipmentManager() &&
		Pawn->GetEquipmentManager()->GetCurrentEquipment();
}
}

bool UMiniTask24ProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	FString ProbeMode;
	return Super::ShouldCreateSubsystem(Outer) && FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask24="), ProbeMode);
#else
	return false;
#endif
}

void UMiniTask24ProbeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UMiniTravelSubsystem>();
	FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask24="), Mode);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask24Role="), Role);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask24Peer="), Peer);
	if (Peer.IsEmpty()) { Peer = Role; }
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask24Address="), Address);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask24UnusedAddress="), UnusedAddress);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask24SignalDir="), SignalDirectory);
	FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask24MediaOutput="), MediaDirectory);
	if (MediaDirectory.IsEmpty()) { MediaDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots")); }
	CleanupHandle = FWorldDelegates::OnPostWorldCleanup.AddUObject(this, &ThisClass::HandleCleanup);
	WidgetRebuildHandle = UCommonActivatableWidget::OnRebuilding.AddWeakLambda(this,
		[this](UCommonActivatableWidget& Widget)
		{
			if (Widget.GetGameInstance() != GetGameInstance()) { return; }
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe WIDGET_REBUILD: Peer=%s Widget=%s LocalPlayer=%s World=%s Active=%d"),
				*Peer, *Widget.GetPathName(), *GetNameSafe(Widget.GetOwningLocalPlayer()), *GetNameSafe(Widget.GetWorld()), Widget.IsActivated() ? 1 : 0);
		});
	if (GEngine)
	{
		NetworkHandle = GEngine->OnNetworkFailure().AddWeakLambda(this,
			[this](UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Type, const FString& Reason)
			{ HandleNetworkFailure(World, Driver, int32(Type), Reason); });
	}
	StartedAt = FPlatformTime::Seconds();
	bInitialized = true;
}

void UMiniTask24ProbeSubsystem::Deinitialize()
{
	bInitialized = false;
	FWorldDelegates::OnPostWorldCleanup.Remove(CleanupHandle);
	UCommonActivatableWidget::OnRebuilding.Remove(WidgetRebuildHandle);
	if (GEngine) { GEngine->OnNetworkFailure().Remove(NetworkHandle); }
	OldFrontEnd = nullptr; OldModal = nullptr; OldHUD = nullptr; OldVM = nullptr; OldMenu = nullptr; OldRoot = nullptr;
	Super::Deinitialize();
}

bool UMiniTask24ProbeSubsystem::IsTickable() const { return !IsTemplate() && bInitialized && !bDone && !bFailed; }
UWorld* UMiniTask24ProbeSubsystem::GetTickableGameObjectWorld() const { return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr; }
TStatId UMiniTask24ProbeSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask24ProbeSubsystem, STATGROUP_Tickables); }

void UMiniTask24ProbeSubsystem::Fail(const TCHAR* Reason)
{
	bFailed = true;
	UE_LOG(LogMiniInit, Error, TEXT("MiniTask24Probe FAIL: Mode=%s Role=%s Peer=%s Step=%d Reason=%s"), *Mode, *Role, *Peer, Step, Reason);
}

void UMiniTask24ProbeSubsystem::Pass(const TCHAR* Marker, const TCHAR* Evidence)
{
	bDone = true;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe %s: Peer=%s %s"), Marker, *Peer, Evidence);
}

void UMiniTask24ProbeSubsystem::LogWait(const TCHAR* Reason)
{
	if (FPlatformTime::Seconds() - LastWaitAt < 5.0) { return; }
	LastWaitAt = FPlatformTime::Seconds();
	UWorld* World = GetTickableGameObjectWorld();
	AMiniPlayerController* PC = Task24PC(World);
	const UMiniTravelSubsystem* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe WAIT: Mode=%s Role=%s Peer=%s Step=%d Reason=%s World=%s Net=%d Pawn=%s Input=%d Busy=%d Error=%s Cleanup=%d NetworkFailures=%d"),
		*Mode, *Role, *Peer, Step, Reason, *GetNameSafe(World), World ? int32(World->GetNetMode()) : -1, *GetNameSafe(PC ? PC->GetPawn() : nullptr),
		Task24Pawn(PC) && Task24Pawn(PC)->GetHeroComponent() && Task24Pawn(PC)->GetHeroComponent()->IsInputActive() ? 1 : 0, Travel && Travel->GetState().bBusy ? 1 : 0,
		Travel ? *Travel->GetState().FailureCode : TEXT("None"), CleanupCount, NetworkFailures);
}

void UMiniTask24ProbeSubsystem::Tick(float DeltaTime)
{
	int32 Timeout = 180;
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask24TimeoutSeconds="), Timeout);
	if (FPlatformTime::Seconds() - StartedAt > Timeout) { Fail(TEXT("real UI/network flow timed out")); return; }
	UWorld* World = GetTickableGameObjectWorld();
	if (!World || !World->IsGameWorld() || !World->HasBegunPlay() || World->bIsTearingDown) { return; }
	if ((Mode == TEXT("HostFailureListen") || Mode == TEXT("HostFailureCreate")) && Role == TEXT("Server"))
	{ TickHostFailure(); return; }
	if (Mode != TEXT("Flow") && Mode != TEXT("Failures") && Mode != TEXT("Capacity") && Mode != TEXT("HostLoss") && Mode != TEXT("Quit"))
	{ Fail(TEXT("unknown Task24 scenario")); return; }
	if (Role == TEXT("Server")) { TickServer(); }
	else if (Role == TEXT("Client")) { TickClient(); }
	else if (Role == TEXT("Overflow") && Mode == TEXT("Capacity")) { TickOverflow(); }
	else if (Role == TEXT("Overflow") && Mode == TEXT("Quit")) { TickQuitFrontEnd(); }
	else { Fail(TEXT("unknown Task24 peer role")); }
}

bool UMiniTask24ProbeSubsystem::FrontEndReady(bool bError)
{
	UWorld* World = GetTickableGameObjectWorld();
	AMiniPlayerController* PC = Task24PC(World);
	UMiniPrimaryGameLayout* Root = Task24Root(PC);
	UMiniFrontEndWidget* Front = Task24Front(PC);
	UMiniExperienceManagerComponent* Manager = Task24Experience(World);
	UMiniTravelSubsystem* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
	if (!World || World->GetNetMode() != NM_Standalone || !PC || !Root || !Root->IsLayoutReady() || !Front || !Manager ||
		!Manager->IsExperienceLoaded() || !Travel || Travel->GetState().bBusy || Travel->GetState().bHasError != bError) { LogWait(TEXT("FrontEndReady")); return false; }
	const UMiniExperienceDefinition* Experience = Manager->GetCurrentExperience();
	if (!Experience || !Experience->bIsFrontEnd || Experience->DefaultPawnData || Experience->ActionSets.Num() || Experience->GameFeaturesToEnable.Num() ||
		Manager->GetCurrentExperienceId().PrimaryAssetName != TEXT("DA_MiniFrontEndExperience") || !World->GetMapName().EndsWith(TEXT("L_MiniFrontEnd")) || PC->GetPawn() ||
		!Task24PS(PC) || Task24PS(PC)->GetPawnData() || !Task24ASC(PC) || Task24ASC(PC)->GetAvatarActor() || Task24ASC(PC)->GetActivatableAbilities().Num() ||
		World->GetGameState()->FindComponentByClass<UMiniArenaRulesComponent>() || World->GetGameState()->FindComponentByClass<UMiniMatchRulesComponent>())
	{ Fail(TEXT("front end acquired a Pawn, combat grants, features or Arena rules")); return false; }
	for (TActorIterator<AMiniCharacter> It(World); It; ++It) { Fail(TEXT("front end contains a combat Pawn")); return false; }
	for (TActorIterator<AMiniPracticeTarget> It(World); It; ++It) { Fail(TEXT("front end contains a training target")); return false; }
	UMiniConnectionStatusWidget* Modal = Travel->GetConnectionStatusWidget();
	if (Root->GetGameplayInputBlockCount() != (bError ? 2 : 1) || !PC->IsMiniInputBlocked() || Front->GetStateListenerCount() != 1 ||
		(!bError && Modal && Modal->IsActivated()) || !Front->GetButton(TEXT("HostArenaButton"))->GetIsEnabled() || !Front->GetButton(TEXT("PracticeButton"))->GetIsEnabled())
	{ LogWait(TEXT("FrontEndInputAndListeners")); return false; }
	return true;
}

bool UMiniTask24ProbeSubsystem::GameReady(bool bArena)
{
	UWorld* World = GetTickableGameObjectWorld();
	AMiniPlayerController* PC = Task24PC(World);
	UMiniPrimaryGameLayout* Root = Task24Root(PC);
	UMiniHUDLayout* HUD = Task24HUD(PC);
	UMiniHUDViewModel* VM = HUD ? HUD->GetViewModel() : nullptr;
	UMiniExperienceManagerComponent* Manager = Task24Experience(World);
	UMiniTravelSubsystem* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
	if (!Manager || !Manager->IsExperienceLoaded() || !Task24PlayerReady(PC) || !Root || !HUD || !VM || !VM->IsRunning() || !Travel ||
		Travel->GetState().bBusy || Travel->GetState().bHasError || Root->GetGameplayInputBlockCount() || PC->IsMiniInputBlocked() ||
		(Travel->GetConnectionStatusWidget() && Travel->GetConnectionStatusWidget()->IsActivated())) { LogWait(TEXT("GameReady")); return false; }
	const FName Expected = bArena ? FName(TEXT("DA_MiniArenaExperience")) : FName(TEXT("DA_MiniPracticeExperience"));
	const FMiniHUDSnapshot& S = VM->GetSnapshot();
	if (Manager->GetCurrentExperienceId().PrimaryAssetName != Expected || !World->GetMapName().EndsWith(bArena ? TEXT("L_MiniArena") : TEXT("L_MiniPractice")) ||
		S.World != World || S.Pawn != Task24Pawn(PC) || !S.bHealthReady || !S.bAmmoReady || S.bDead || S.ActiveSlot != 0 || S.MagazineAmmo != 30 || S.ReserveAmmo != 90)
	{ LogWait(TEXT("AvatarAndHUD")); return false; }
	const bool bHasArena = World->GetGameState()->FindComponentByClass<UMiniArenaRulesComponent>() && World->GetGameState()->FindComponentByClass<UMiniMatchRulesComponent>();
	if (bHasArena != bArena || S.bHasMatchData != bArena || S.bHasPhaseData != bArena)
	{ Fail(TEXT("travel destination loaded the wrong data/plugin assembly")); return false; }
	if (!bArena)
	{
		int32 Targets = 0;
		for (TActorIterator<AMiniPracticeTarget> It(World); It; ++It) { Targets += It->IsTargetEnabled() ? 1 : 0; }
		if (Targets > 3 || Task24ASC(PC)->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected))
		{ Fail(TEXT("training travel changed practice targets or acquired FFA protection")); return false; }
		// AddActors completes its soft-class load asynchronously after Experience
		// Loaded. Require all three enabled targets before asserting training ready.
		if (Targets < 3) { LogWait(TEXT("PracticeTargetsReady")); return false; }
	}
	return true;
}

bool UMiniTask24ProbeSubsystem::VerifyError(const FString& Code)
{
	UMiniTravelSubsystem* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
	if (!Travel || !FrontEndReady(true)) { return false; }
	UMiniConnectionStatusWidget* Modal = Travel->GetConnectionStatusWidget();
	if (!Modal || !Modal->IsActivated() || Modal->GetStateListenerCount() != 1 || Travel->GetState().FailureCode != Code ||
		Modal->GetTitleText().IsEmpty() || Modal->GetDisplayText().IsEmpty() ||
		Modal->GetTitleText().ToString() != Travel->GetState().StatusText.ToString() || Modal->GetDisplayText().ToString() != Travel->GetState().DetailText.ToString() ||
		!Modal->GetButton(TEXT("CloseButton"))->IsVisible() ||
		!Modal->GetButton(TEXT("CloseButton"))->GetIsEnabled()) { LogWait(TEXT("ErrorModal")); return false; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe ERROR_UI: Peer=%s Code=%s Title=%s Detail=%s FrontEnd=1 Modal=1 Gates=2 Listeners=1"),
		*Peer, *Code, *Modal->GetTitleText().ToString(), *Modal->GetDisplayText().ToString());
	return true;
}

void UMiniTask24ProbeSubsystem::TickServer()
{
	if (Step == 0)
	{
		if (!FrontEndReady() || !Media(TEXT("MainMenu"))) { return; }
		if (!bFrontLogged)
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe FRONT_END_PASS: Peer=%s DefaultMap=1 NoPawn=1 NoCombat=1 Menu=1 InputGate=1"), *Peer);
			bFrontLogged = true;
		}
		SaveIdentity();
		if (!Click(Task24Front(Task24PC(GetTickableGameObjectWorld()))->GetButton(TEXT("HostArenaButton")), TEXT("HostArenaButton"))) { return; }
		Step = 1;
		return;
	}
	if (Step == 1)
	{
		if (!GameReady() || GetTickableGameObjectWorld()->GetNetMode() != NM_ListenServer) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe SERVER_HOST_READY: Mode=%s Peer=%s ActualSlateHost=1 ProductionArena=1 Listen=1 UIGates=0"), *Mode, *Peer);
		Step = 2;
		return;
	}
	if (GetTickableGameObjectWorld()->GetNetMode() == NM_ListenServer) { DiscoverOwners(); }
	if (bFailed) { return; }
	if (Mode == TEXT("Flow")) { TickFlowServer(); }
	else if (Mode == TEXT("Capacity")) { TickCapacityServer(); }
	else if (Mode == TEXT("Quit")) { TickQuitServer(); }
	else if (Mode == TEXT("Failures"))
	{
		if (Checkpoint(TEXT("FailuresRecoveredArena"), 1))
		{ Pass(TEXT("FAILURES_SERVER_PASS"), TEXT("RealUIHost=1 RecoveredClientJoined=1 ProductionArena=1 ClientHUD=1")); }
	}
	else if (Mode == TEXT("HostLoss") && Step == 2 && Checkpoint(TEXT("HostLossArmed"), 1))
	{
		Step = 99;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe HOST_LOSS_ARMED: Peer=%s Listen=1 ClientHUD=1 ExternalHostTerminationMayProceed=1"), *Peer);
	}
}

void UMiniTask24ProbeSubsystem::TickClient()
{
	if (Mode == TEXT("Flow")) { TickFlowClient(); }
	else if (Mode == TEXT("Failures")) { TickFailuresClient(); }
	else if (Mode == TEXT("Capacity")) { TickCapacityClient(); }
	else if (Mode == TEXT("HostLoss")) { TickHostLossClient(); }
	else if (Mode == TEXT("Quit")) { TickQuitClient(); }
}

void UMiniTask24ProbeSubsystem::TickFlowServer()
{
	UWorld* World = GetTickableGameObjectWorld();
	AMiniPlayerController* Host = Task24PC(World);
	if (Step == 2 || Step == 5)
	{
		if (!Checkpoint(Step == 2 ? FName(TEXT("InitialArena")) : FName(TEXT("RejoinedArena")), 1)) { return; }
		AMiniPlayerController* Victim = Cast<AMiniPlayerController>(OwnerProbes[0]->GetOwner());
		if (Task24ASC(Victim)->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected)) { return; }
		const int32 OldKills = Task24PS(Host)->GetMatchStats().Kills;
		LastVictim = Victim;
		KilledPawn = Task24Pawn(Victim);
		StepStartedAt = FPlatformTime::Seconds();
		if (!World->GetAuthGameMode<AMiniGameMode>()->TryApplyTestDamage(Host, KilledPawn.Get(), 150.0f) ||
			!KilledPawn->GetHealthComponent()->IsDead() || Task24PS(Host)->GetMatchStats().Kills != OldKills + 1)
		{ Fail(TEXT("real GE failed to create a scored lifecycle before UI travel")); return; }
		Step = Step == 2 ? 3 : 6;
		return;
	}
	if (Step == 3 || Step == 6)
	{
		if (!Task24PlayerReady(LastVictim.Get()) || Task24Pawn(LastVictim.Get()) == KilledPawn.Get() ||
			Task24ASC(LastVictim.Get())->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected))
		{
			if (FPlatformTime::Seconds() - StepStartedAt > 10.0) { Fail(TEXT("scored UI travel fixture lost delayed respawn/protection expiry")); }
			return;
		}
		if (!Checkpoint(Step == 3 ? FName(TEXT("ScoredArena")) : FName(TEXT("BeforeRestart")), 1)) { return; }
		if (Step == 3)
		{
			OwnerProbes[0]->SetCommand(EMiniTask24Command::LeaveAndRejoin);
			Step = 4;
		}
		else { Step = 60; }
		return;
	}
	if (Step == 60)
	{
		if (MenuAction(TEXT("RestartArenaButton"))) { Step = 7; }
		return;
	}
	if (Step == 4)
	{
		UMiniMatchSubsystem* Matches = World->GetSubsystem<UMiniMatchSubsystem>();
		if (Matches->GetCurrentMatchState().ConnectedPlayerCount != 1 || OwnerProbes.Num() != 0) { return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe FLOW_CLIENT_LEFT: ActualLogout=1 HostRemained=1 Count=1"));
		Step = 5;
		return;
	}
	if (Step == 7)
	{
		if (!GameReady() || World == SavedWorld.Get()) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		if (!FMath::IsNearlyEqual(Task24PS(Host)->GetHealthSet()->GetHealth(), 100.0f) || !Checkpoint(TEXT("RestartedArena"), 1)) { return; }
		for (TActorIterator<AMiniPlayerState> It(World); It; ++It)
		{
			if (It->GetMatchStats().Kills || It->GetMatchStats().Deaths || It->GetMatchStats().RoundId != 1)
			{ Fail(TEXT("ordinary server restart retained prior-world scores")); return; }
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe ORDINARY_RESTART_PASS: ServerNewWorld=1 NewPlayerStates=1 NewASCs=1 PriorScoreCleared=1 ClientsReconnected=1 HUD=1"));
		Step = 70;
		return;
	}
	if (Step == 70)
	{
		if (!MenuAction(TEXT("ReturnButton"))) { return; }
		Step = 8;
		return;
	}
	if (Step == 8)
	{
		if (PracticeStep == 0)
		{
			if (!FrontEndReady()) { return; }
			if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		}
		TickPracticeRoundTrip(TEXT("FLOW_SERVER_PASS"));
	}
}

void UMiniTask24ProbeSubsystem::TickFlowClient()
{
	if (Step == 0)
	{
		if (!FrontEndReady() || !Media(TEXT("MainMenu")) || !JoinThroughUI(Address)) { return; }
		Step = 1;
		return;
	}
	if (Step == 1)
	{
		if (!GameReady()) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		if (!AcknowledgeCheckpoint(TEXT("InitialArena"))) { return; }
		Step = 2;
		return;
	}
	if (Step == 2)
	{
		if (!AcknowledgeCheckpoint(TEXT("ScoredArena"))) { return; }
		AMiniTask24ProbeActor* Probe = LocalProbe();
		if (!Probe || Probe->GetCommand() != EMiniTask24Command::LeaveAndRejoin) { return; }
		Step = 20;
		return;
	}
	if (Step == 20)
	{
		if (!MenuAction(TEXT("ReturnButton"))) { return; }
		Step = 3;
		return;
	}
	if (Step == 3)
	{
		if (!FrontEndReady()) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		StepStartedAt = FPlatformTime::Seconds();
		Step = 4;
		return;
	}
	if (Step == 4)
	{
		if (FPlatformTime::Seconds() - StepStartedAt < 1.0 || !FrontEndReady() || !JoinThroughUI(Address)) { return; }
		Step = 5;
		return;
	}
	if (Step == 5)
	{
		if (!GameReady()) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		if (!AcknowledgeCheckpoint(TEXT("RejoinedArena"))) { return; }
		Step = 6;
		return;
	}
	if (Step == 6)
	{
		if (!AcknowledgeCheckpoint(TEXT("BeforeRestart"))) { return; }
		SaveIdentity(); // The next ordinary travel is initiated by the host's real menu.
		Step = 7;
		return;
	}
	if (Step == 7)
	{
		if (GetTickableGameObjectWorld() == SavedWorld.Get() || !GameReady()) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		if (Task24PS(Task24PC(GetTickableGameObjectWorld()))->GetMatchStats().Kills || Task24PS(Task24PC(GetTickableGameObjectWorld()))->GetMatchStats().Deaths)
		{ Fail(TEXT("client ordinary restart retained prior-world statistics")); return; }
		if (!AcknowledgeCheckpoint(TEXT("RestartedArena"))) { return; }
		SaveIdentity();
		Step = 8;
		return;
	}
	if (Step == 8)
	{
		if (!VerifyError(TEXT("MINI_HOST_LEFT")) || !Media(TEXT("HostLeft"))) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		if (!CloseError()) { return; }
		Step = 9;
		return;
	}
	if (Step == 9) { TickPracticeRoundTrip(TEXT("FLOW_CLIENT_PASS")); }
}

void UMiniTask24ProbeSubsystem::TickPracticeRoundTrip(const TCHAR* Marker)
{
	if (PracticeStep == 0)
	{
		if (!FrontEndReady()) { return; }
		SaveIdentity();
		if (!Click(Task24Front(Task24PC(GetTickableGameObjectWorld()))->GetButton(TEXT("PracticeButton")), TEXT("PracticeButton"))) { return; }
		PracticeStep = 1;
		return;
	}
	if (PracticeStep == 1)
	{
		if (!GameReady(false)) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		AMiniPlayerController* PC = Task24PC(GetTickableGameObjectWorld());
		const float Before = Task24PS(PC)->GetHealthSet()->GetHealth();
		if (!GetTickableGameObjectWorld()->GetAuthGameMode<AMiniGameMode>()->TryApplyEnvironmentDamage(Task24Pawn(PC), 25.0f) ||
			!FMath::IsNearlyEqual(Task24PS(PC)->GetHealthSet()->GetHealth(), Before - 25.0f))
		{ Fail(TEXT("real UI-selected training mode lost its normal GE chain")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe PRACTICE_BUTTON_PASS: Peer=%s RealSlateClick=1 DefaultPracticeExperience=1 Targets=3 Arena=0 RealGE=1"), *Peer);
		PracticeStep = 2;
		return;
	}
	if (PracticeStep == 2)
	{
		if (!MenuAction(TEXT("ReturnButton"))) { return; }
		PracticeStep = 3;
		return;
	}
	if (PracticeStep == 3)
	{
		if (!FrontEndReady()) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		Pass(Marker, Mode == TEXT("HostFailureCreate")
			? TEXT("ActualNullDriverHostFailure=1 FriendlyError=1 ActualClosePracticeAndReturn=1 NewWorldPSASC=1 OldUIListeners=0 ActiveFrontGate=1")
			: TEXT("RealSlateHostJoinMenu=1 OrdinaryTravel=1 NewWorldPSASC=1 TrainingAndReturn=1 OldUIListeners=0 ActiveFrontGate=1"));
	}
}

void UMiniTask24ProbeSubsystem::TickFailuresClient()
{
	UMiniTravelSubsystem* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
	if (Step == 0)
	{
		if (!FrontEndReady() || !Media(TEXT("MainMenu")) || !JoinThroughUI(TEXT("127.0.0.1?listen"))) { return; }
		Step = 1;
		return;
	}
	if (Step == 1)
	{
		if (!VerifyError(TEXT("MINI_INVALID_ADDRESS")) || !Media(TEXT("InvalidAddress"))) { return; }
		UButton* Close = Travel->GetConnectionStatusWidget()->GetButton(TEXT("CloseButton"));
		if (!Close->HasKeyboardFocus())
		{
			if (FPlatformTime::Seconds() - LastWaitAt > 5.0)
			{
				const UCommonUIActionRouterBase* Router = Task24PC(GetTickableGameObjectWorld())->GetLocalPlayer()->GetSubsystem<UCommonUIActionRouterBase>();
				const UMiniConnectionStatusWidget* Modal = Travel->GetConnectionStatusWidget();
				const TSharedPtr<SWidget> Focus = FSlateApplication::Get().GetUserFocusedWidget(0);
				UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe MODAL_ROUTER_STATE: Leaf=%s Pending=%d ModalInRoot=%d ModalPaint=%d Focus=%s"),
					*GetNameSafe(Router->GetLeafmostActivatableWidget()), Router->IsPendingTreeChange() ? 1 : 0,
					Router->IsWidgetInActiveRoot(Modal) ? 1 : 0, Modal->GetCachedWidget()->GetPersistentState().LayerId,
					Focus.IsValid() ? *Focus->GetTypeAsString() : TEXT("None"));
			}
			LogWait(TEXT("FailureCloseKeyboardFocus")); return;
		}
		if (!SendKey(EKeys::Enter) || Travel->GetState().bHasError)
		{ Fail(TEXT("focused error CloseButton did not close through actual Enter input")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe KEYBOARD_ERROR_CLOSE_PASS: Peer=%s FocusedCloseButton=1 ActualEnter=1 ErrorCleared=1"), *Peer);
		Step = 2;
		return;
	}
	if (Step == 2)
	{
		if (!FrontEndReady() || !JoinThroughUI(UnusedAddress)) { return; }
		Step = 3;
		return;
	}
	if (Step == 3)
	{
		UMiniConnectionStatusWidget* Modal = Travel->GetConnectionStatusWidget();
		UMiniFrontEndWidget* Front = Task24Front(Task24PC(GetTickableGameObjectWorld()));
		const FWorldContext* Context = GEngine ? GEngine->GetWorldContextFromWorld(GetTickableGameObjectWorld()) : nullptr;
		if (!Travel->GetState().bBusy || !Modal || !Modal->IsActivated() || !Front || !Context ||
			!Context->PendingNetGame || !Context->PendingNetGame->GetNetDriver()) { LogWait(TEXT("RealPendingConnection")); return; }
		if (Front->GetButton(TEXT("JoinButton"))->GetIsEnabled() || Front->GetButton(TEXT("HostArenaButton"))->GetIsEnabled() ||
			!Modal->GetButton(TEXT("CancelButton"))->IsVisible() || !Modal->GetButton(TEXT("CancelButton"))->GetIsEnabled() ||
			Task24Root(Task24PC(GetTickableGameObjectWorld()))->GetGameplayInputBlockCount() != 2)
		{ Fail(TEXT("real pending connection failed UI duplicate-submission gating or cancel affordance")); return; }
		if (!Media(TEXT("Connecting"))) { return; }
		const uint32 Generation = Travel->GetRequestGeneration();
		const int32 Revision = Travel->GetState().Revision;
		// Negative API checks verify the same gate without pretending these are UI clicks.
		if (Travel->StartPractice() || Travel->HostArena() || Travel->JoinAddress(Address) || Travel->RestartArena() ||
			Travel->GetRequestGeneration() != Generation || Travel->GetState().Revision != Revision)
		{ Fail(TEXT("busy API requests changed the live pending connection")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe PENDING_UI_PASS: Peer=%s RealPendingDriver=1 ButtonsDisabled=1 CancelVisible=1 Gates=2 DuplicateAPIsRejected=1"), *Peer);
		Step = 30;
		return;
	}
	if (Step == 30)
	{
		UMiniConnectionStatusWidget* Modal = Travel->GetConnectionStatusWidget();
		const FWorldContext* Context = GEngine ? GEngine->GetWorldContextFromWorld(GetTickableGameObjectWorld()) : nullptr;
		if (!Travel->GetState().bBusy || Travel->GetState().Operation != EMiniTravelOperation::Join || !Modal ||
			!Modal->IsActivated() || !Context || !Context->PendingNetGame || !Context->PendingNetGame->GetNetDriver())
		{ LogWait(TEXT("PendingBeforeActualCancel")); return; }
		SaveIdentity();
		if (!Click(Modal->GetButton(TEXT("CancelButton")), TEXT("CancelButton"))) { return; }
		Step = 31;
		return;
	}
	if (Step == 31)
	{
		if (!FrontEndReady()) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		const FWorldContext* Context = GEngine ? GEngine->GetWorldContextFromWorld(GetTickableGameObjectWorld()) : nullptr;
		if (!Context || Context->PendingNetGame) { Fail(TEXT("actual Cancel left a pending connection in the recovered front end")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe CANCEL_PENDING_PASS: Peer=%s ActualSlateCancel=1 PendingDriverRemoved=1 NewFrontEndWorldPSASC=1 FrontGate=1 ButtonsEnabled=1"), *Peer);
		Step = 32;
		return;
	}
	if (Step == 32)
	{
		if (!FrontEndReady() || !JoinThroughUI(UnusedAddress)) { return; }
		Step = 33;
		return;
	}
	if (Step == 33)
	{
		const FWorldContext* Context = GEngine ? GEngine->GetWorldContextFromWorld(GetTickableGameObjectWorld()) : nullptr;
		if (!Travel->GetState().bBusy || Travel->GetState().Operation != EMiniTravelOperation::Join || !Context ||
			!Context->PendingNetGame || !Context->PendingNetGame->GetNetDriver()) { LogWait(TEXT("RealPendingConnectionAfterCancel")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe PENDING_RETRY_PASS: Peer=%s ActualAddressRetyped=1 NewPendingDriver=1 PreviousCancelDidNotBlockRetry=1"), *Peer);
		Step = 4;
		return;
	}
	if (Step == 4)
	{
		const FString Code = Travel->GetState().FailureCode;
		if (NetworkFailures < 1 || (Code != TEXT("MINI_CONNECTION_TIMEOUT") && Code != TEXT("MINI_CONNECTION_FAILED")) ||
			!VerifyError(Code) || !Media(TEXT("ConnectionFailure")) || !CloseError()) { return; }
		Step = 5;
		return;
	}
	if (Step == 5)
	{
		if (!FrontEndReady() || !JoinThroughUI(Address)) { return; }
		Step = 6;
		return;
	}
	if (Step == 6)
	{
		if (!GameReady()) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		if (!AcknowledgeCheckpoint(TEXT("FailuresRecoveredArena"))) { return; }
		const uint32 Generation = Travel->GetRequestGeneration();
		const int32 Revision = Travel->GetState().Revision;
		if (Travel->RestartArena() || Travel->HostArena() || Travel->StartPractice() || Travel->JoinAddress(Address) ||
			Travel->GetRequestGeneration() != Generation || Travel->GetState().Revision != Revision)
		{ Fail(TEXT("client gameplay API requested host travel or accepted a front-end-only action")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe CLIENT_AUTHORITY_GATE_PASS: RestartRejected=1 FrontEndOnlyAPIsRejected=1 GenerationUnchanged=1"));
		Step = 7;
		return;
	}
	if (Step == 7)
	{
		if (!MenuAction(TEXT("ReturnButton"))) { return; }
		Step = 8;
		return;
	}
	if (Step == 8)
	{
		if (!FrontEndReady()) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		Pass(TEXT("FAILURES_CLIENT_PASS"), TEXT("InvalidAddressUI=1 ErrorFocusedKeyboardClose=1 ActualPendingCancel=1 RetryAfterCancel=1 RealUnreachableDriverFailure=1 PendingGates=1 RecoveredUIJoin=1 ActualMenuReturn=1 FrontEnd=1"));
	}
}

void UMiniTask24ProbeSubsystem::TickCapacityServer()
{
	UWorld* World = GetTickableGameObjectWorld();
	UMiniMatchSubsystem* Matches = World->GetSubsystem<UMiniMatchSubsystem>();
	if (Step == 2)
	{
		if (!Checkpoint(TEXT("CapacityFour"), 3)) { return; }
		CapacityPlayerStates.Reset(); CapacityASCs.Reset(); SavedWorld = World;
		for (TActorIterator<AMiniPlayerController> It(World); It; ++It)
		{ CapacityPlayerStates.Add(*It, Task24PS(*It)); CapacityASCs.Add(*It, Task24ASC(*It)); }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe SERVER_CAPACITY_FOUR_READY: RealLogins=4 Players=4 Clients=3 HUD=1 FifthMayJoin=1"));
		Step = 3;
		return;
	}
	if (Step == 3)
	{
		if (!HasSignal(TEXT("CapacityRejected.signal"))) { return; }
		if (Matches->GetCurrentMatchState().ConnectedPlayerCount != 4 || OwnerProbes.Num() != 3)
		{ Fail(TEXT("fifth login displaced an existing player before capacity release")); return; }
		for (const TPair<TWeakObjectPtr<AMiniPlayerController>, TWeakObjectPtr<AMiniPlayerState>>& Pair : CapacityPlayerStates)
		{
			if (!Pair.Key.IsValid() || Task24PS(Pair.Key.Get()) != Pair.Value.Get() || Task24ASC(Pair.Key.Get()) != CapacityASCs[Pair.Key].Get())
			{ Fail(TEXT("capacity rejection recreated or removed an existing player")); return; }
		}
		OwnerProbes[0]->SetCommand(EMiniTask24Command::ReleaseSlot);
		Step = 4;
		return;
	}
	if (Step == 4)
	{
		if (Matches->GetCurrentMatchState().ConnectedPlayerCount != 3 || OwnerProbes.Num() != 2) { return; }
		if (!WriteSignal(TEXT("CapacityMayRejoin.signal"))) { Fail(TEXT("capacity rejoin signal could not be written")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe CAPACITY_SLOT_RELEASED: ActualLogout=1 Remaining=3 OverflowMayRejoin=1"));
		Step = 5;
		return;
	}
	if (Step == 5)
	{
		if (!Checkpoint(TEXT("CapacityRefilled"), 3)) { return; }
		int32 Retained = 0;
		for (const TPair<TWeakObjectPtr<AMiniPlayerController>, TWeakObjectPtr<AMiniPlayerState>>& Pair : CapacityPlayerStates)
		{
			if (!Pair.Key.IsValid()) { continue; }
			++Retained;
			if (Task24PS(Pair.Key.Get()) != Pair.Value.Get() || Task24ASC(Pair.Key.Get()) != CapacityASCs[Pair.Key].Get())
			{ Fail(TEXT("capacity refill recreated a retained player")); return; }
		}
		if (Retained != 3 || World != SavedWorld.Get()) { Fail(TEXT("capacity refill unexpectedly traveled or retained wrong player count")); return; }
		Pass(TEXT("CAPACITY_SERVER_PASS"), TEXT("RealFourLogins=1 ActualFifthRejected=1 ActualMenuLeave=1 OverflowRejoined=1 RemainingPSASCUnchanged=3 SameWorld=1 ClientHUD=3"));
	}
}

void UMiniTask24ProbeSubsystem::TickCapacityClient()
{
	if (Step == 0)
	{
		if (!FrontEndReady() || !Media(TEXT("MainMenu")) || !JoinThroughUI(Address)) { return; }
		Step = 1;
		return;
	}
	if (Step == 1)
	{
		if (!GameReady()) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		if (!AcknowledgeCheckpoint(TEXT("CapacityFour"))) { return; }
		SaveIdentity();
		Step = 2;
		return;
	}
	if (Step == 2)
	{
		AMiniTask24ProbeActor* Probe = LocalProbe();
		if (Probe && Probe->GetCommand() == EMiniTask24Command::ReleaseSlot)
		{
			if (!MenuAction(TEXT("ReturnButton"))) { return; }
			Step = 3;
			return;
		}
		if (!AcknowledgeCheckpoint(TEXT("CapacityRefilled"))) { return; }
		AMiniPlayerController* PC = Task24PC(GetTickableGameObjectWorld());
		if (GetTickableGameObjectWorld() != SavedWorld.Get() || Task24PS(PC) != SavedPlayerState.Get() || Task24ASC(PC) != SavedASC.Get())
		{ Fail(TEXT("remaining capacity client was traveled or recreated by rejection/refill")); return; }
		Pass(TEXT("CAPACITY_RETAINED_CLIENT_PASS"), TEXT("ActualCapacityFour=1 FifthRejected=1 Refilled=1 SameWorld=1 SamePlayerState=1 SameASC=1 HUD=1"));
		return;
	}
	if (Step == 3)
	{
		if (!FrontEndReady()) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		Pass(TEXT("CAPACITY_RELEASE_CLIENT_PASS"), TEXT("ActualEscapeMenu=1 ActualReturnClick=1 Disconnected=1 FrontEnd=1 Gate=1"));
	}
}

void UMiniTask24ProbeSubsystem::TickOverflow()
{
	if (Step == 0)
	{
		if (!FrontEndReady() || !Media(TEXT("MainMenu")) || !JoinThroughUI(Address)) { return; }
		Step = 1;
		return;
	}
	if (Step == 1)
	{
		if (NetworkFailures < 1 || !VerifyError(TEXT("MINI_SERVER_FULL")) || !Media(TEXT("ServerFull"))) { return; }
		if (!WriteSignal(TEXT("CapacityRejected.signal"))) { Fail(TEXT("actual capacity rejection signal could not be written")); return; }
		if (!CloseError()) { return; }
		Step = 2;
		return;
	}
	if (Step == 2)
	{
		if (!HasSignal(TEXT("CapacityMayRejoin.signal")) || !FrontEndReady() || !JoinThroughUI(Address)) { return; }
		Step = 3;
		return;
	}
	if (Step == 3)
	{
		if (!GameReady()) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		if (!AcknowledgeCheckpoint(TEXT("CapacityRefilled"))) { return; }
		Pass(TEXT("CAPACITY_OVERFLOW_PASS"), TEXT("ActualFifthLoginRejected=1 Code=MINI_SERVER_FULL UserReason=1 ActualClose=1 ActualRejoinAfterSlotReleased=1 HUD=1"));
	}
}

void UMiniTask24ProbeSubsystem::TickHostLossClient()
{
	UMiniTravelSubsystem* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
	if (Step == 0)
	{
		if (!FrontEndReady() || !Media(TEXT("MainMenu")) || !JoinThroughUI(Address)) { return; }
		Step = 1;
		return;
	}
	if (Step == 1)
	{
		if (!GameReady()) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		if (!AcknowledgeCheckpoint(TEXT("HostLossArmed"))) { return; }
		SaveIdentity();
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe HOST_LOSS_ARMED: Peer=%s ActualUIJoin=1 Arena=1 HUD=1 ExternalHostTerminationMayProceed=1"), *Peer);
		Step = 2;
		return;
	}
	if (Step == 2)
	{
		const FString Code = Travel->GetState().FailureCode;
		if (NetworkFailures < 1 || (Code != TEXT("MINI_CONNECTION_LOST") && Code != TEXT("MINI_CONNECTION_TIMEOUT")) || !VerifyError(Code) || !Media(TEXT("HostLoss"))) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		if (!CloseError()) { return; }
		Step = 3;
		return;
	}
	if (Step == 3)
	{
		if (!FrontEndReady()) { return; }
		Pass(TEXT("HOSTLOSS_CLIENT_PASS"), TEXT("ActualDriverFailure=1 HostLossReason=1 NewFrontEndWorld=1 ErrorModal=1 RealCloseClick=1 InteractiveFrontEnd=1"));
	}
}

void UMiniTask24ProbeSubsystem::TickQuitServer()
{
	if (Step == 2)
	{
		if (!Checkpoint(TEXT("QuitArena"), 1)) { return; }
		OwnerProbes[0]->SetCommand(EMiniTask24Command::QuitClient);
		Step = 3;
		return;
	}
	if (Step == 3)
	{
		UMiniMatchSubsystem* Matches = GetTickableGameObjectWorld()->GetSubsystem<UMiniMatchSubsystem>();
		if (!GameReady() || !Matches || Matches->GetCurrentMatchState().ConnectedPlayerCount != 1 || OwnerProbes.Num() != 0) { return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe QUIT_SERVER_READY_AFTER_CLIENT_EXIT: Peer=%s ActualLogout=1 Remaining=1 Listen=1 HUD=1"), *Peer);
		Step = 4;
		return;
	}
	if (Step == 4)
	{
		if (!HasSignal(TEXT("QuitServer.signal"))) { return; }
		Step = 5;
		return;
	}
	if (Step == 5)
	{
		if (!MenuAction(TEXT("QuitButton"))) { return; }
		Pass(TEXT("QUIT_SERVER_REQUESTED"), TEXT("ActualSlateGameplayQuit=1 ProductionDeferredReturnAndExit=1"));
	}
}

void UMiniTask24ProbeSubsystem::TickQuitClient()
{
	if (Step == 0)
	{
		if (!FrontEndReady() || !Media(TEXT("MainMenu")) || !JoinThroughUI(Address)) { return; }
		Step = 1;
		return;
	}
	if (Step == 1)
	{
		if (!GameReady()) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		if (!AcknowledgeCheckpoint(TEXT("QuitArena"))) { return; }
		Step = 2;
		return;
	}
	if (Step == 2)
	{
		AMiniTask24ProbeActor* Probe = LocalProbe();
		if (!Probe || Probe->GetCommand() != EMiniTask24Command::QuitClient) { return; }
		Step = 20;
		return;
	}
	if (Step == 20)
	{
		if (!MenuAction(TEXT("QuitButton"))) { return; }
		Pass(TEXT("QUIT_CLIENT_REQUESTED"), TEXT("ActualSlateGameplayQuit=1 ProductionDeferredReturnAndExit=1"));
	}
}

void UMiniTask24ProbeSubsystem::TickQuitFrontEnd()
{
	if (!FrontEndReady() || !Media(TEXT("MainMenu"))) { return; }
	if (!Click(Task24Front(Task24PC(GetTickableGameObjectWorld()))->GetButton(TEXT("QuitButton")), TEXT("QuitButton"))) { return; }
	Pass(TEXT("QUIT_FRONTEND_REQUESTED"), TEXT("ActualSlateFrontEndQuit=1 ProductionDirectExit=1"));
}

void UMiniTask24ProbeSubsystem::TickHostFailure()
{
	UMiniTravelSubsystem* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
	if (Step == 0)
	{
		if (!FrontEndReady() || !Media(TEXT("MainMenu"))) { return; }
		SaveIdentity();
		if (!Click(Task24Front(Task24PC(GetTickableGameObjectWorld()))->GetButton(TEXT("HostArenaButton")), TEXT("HostArenaButton"))) { return; }
		Step = 1; StepStartedAt = FPlatformTime::Seconds(); return;
	}
	if (Step == 1)
	{
		if (FPlatformTime::Seconds() - StepStartedAt > 20.0)
		{ Fail(TEXT("real host initialization failure did not promptly recover to an actionable front end")); return; }
		if (NetworkFailures != 1 || !VerifyError(TEXT("MINI_HOST_FAILED")) || !Media(TEXT("HostFailure"))) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe HOST_FAILURE_MODAL: Mode=%s RealDriverFailure=1 FrontEnd=1 FriendlyReason=1 Gates=2"), *Mode);
		if (!CloseError()) { return; }
		Step = 2; return;
	}
	if (Step == 2)
	{
		if (!FrontEndReady()) { return; }
		if (Mode == TEXT("HostFailureCreate")) { Step = 3; return; }
		if (!HasSignal(TEXT("HostFailurePortReleased.signal"))) { return; }
		SaveIdentity();
		if (!Click(Task24Front(Task24PC(GetTickableGameObjectWorld()))->GetButton(TEXT("HostArenaButton")), TEXT("HostArenaButton"))) { return; }
		Step = 4; return;
	}
	if (Step == 3) { TickPracticeRoundTrip(TEXT("HOST_FAILURE_CREATE_PASS")); return; }
	if (Step == 4)
	{
		if (!GameReady() || GetTickableGameObjectWorld()->GetNetMode() != NM_ListenServer) { return; }
		if (bIdentitySaved && !VerifyNewIdentity()) { return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe HOST_RETRY_READY: ActualSlateRetry=1 Listen=1 PreviousFailureDidNotBlockRetry=1"));
		Step = 5; return;
	}
	if (Step == 5)
	{
		if (!MenuAction(TEXT("ReturnButton"))) { return; }
		Step = 6; return;
	}
	if (Step == 6)
	{
		if (!FrontEndReady() || (bIdentitySaved && !VerifyNewIdentity())) { return; }
		Pass(TEXT("HOST_FAILURE_LISTEN_PASS"), TEXT("ActualBindFailure=1 FriendlyError=1 ActualCloseAndHostRetry=1 Listen=1 MenuReturn=1 FrontEnd=1 OldUIReleased=1"));
	}
}

bool UMiniTask24ProbeSubsystem::CloseError()
{
	UMiniTravelSubsystem* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
	UMiniConnectionStatusWidget* Modal = Travel ? Travel->GetConnectionStatusWidget() : nullptr;
	if (!Modal || !Modal->IsActivated()) { return false; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe ERROR_CLOSE_STATE: ButtonBound=%d ModalListener=%d Code=%s"),
		Modal->GetButton(TEXT("CloseButton"))->OnClicked.IsBound() ? 1 : 0, Modal->GetStateListenerCount(), *Travel->GetState().FailureCode);
	if (!Click(Modal->GetButton(TEXT("CloseButton")), TEXT("CloseButton"))) { return false; }
	if (Travel->GetState().bHasError && !Travel->GetState().bBusy)
	{ Fail(TEXT("actual error Close click failed to dismiss the product error")); return false; }
	return true;
}

bool UMiniTask24ProbeSubsystem::Click(UWidget* Widget, FName Action)
{
	if (!FSlateApplication::IsInitialized() || !Widget || !Widget->IsVisible() || !Widget->GetIsEnabled() || !Widget->GetCachedWidget().IsValid())
	{ LogWait(TEXT("SlateWidgetReady")); return false; }
	const FGeometry& Geometry = Widget->GetCachedGeometry();
	if (Geometry.GetLocalSize().X < 2.0f || Geometry.GetLocalSize().Y < 2.0f) { LogWait(TEXT("SlateGeometry")); return false; }
	FSlateApplication& Slate = FSlateApplication::Get();
	TSharedPtr<SWindow> Window = Slate.FindWidgetWindow(Widget->GetCachedWidget().ToSharedRef());
	if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()) { LogWait(TEXT("SlateWindow")); return false; }
	// Independent acceptance peers cannot all be the foreground OS application.
	// Use Slate's documented background-input option only during this diagnostic
	// mouse sequence; production widgets and normal input policy remain unchanged.
	const bool bPreviousBackgroundInput = Slate.GetHandleDeviceInputWhenApplicationNotActive();
	Slate.SetHandleDeviceInputWhenApplicationNotActive(true);
	ON_SCOPE_EXIT { Slate.SetHandleDeviceInputWhenApplicationNotActive(bPreviousBackgroundInput); };
	const FVector2D Position = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
	const FVector2D PreviousPosition = Slate.GetCursorPos();
	Slate.SetCursorPos(Position);
	TSet<FKey> Pressed;
	// A normal mouse button only executes OnClicked on release while hovered.
	// Moving the platform cursor alone does not update Slate's hover path.
	Slate.ProcessMouseMoveEvent(FPointerEvent(FSlateApplication::CursorPointerIndex,
		Position, PreviousPosition, Pressed, EKeys::Invalid, 0.0f, FModifierKeysState()));
	if (Cast<UButton>(Widget) && !Widget->IsHovered())
	{ LogWait(TEXT("SlateTargetHover")); return false; }
	const bool bTargetHovered = Widget->IsHovered();
	const FWidgetPath CaptorPath = Slate.GetUser(0)->GetCaptorPath(FSlateApplication::CursorPointerIndex);
	const TSharedPtr<SWidget> CaptorBefore = CaptorPath.IsValid() ? CaptorPath.Widgets.Last().Widget : TSharedPtr<SWidget>();
	Pressed.Add(EKeys::LeftMouseButton);
	const bool bDown = Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), FPointerEvent(FSlateApplication::CursorPointerIndex, Position, Position, Pressed, EKeys::LeftMouseButton, 0.0f, FModifierKeysState()));
	const UButton* Button = Cast<UButton>(Widget);
	const bool bTargetPressed = !Button || Button->IsPressed();
	const bool bTargetCaptured = !Button || Widget->GetCachedWidget()->HasMouseCapture();
	Pressed.Reset();
	const bool bUp = Slate.ProcessMouseButtonUpEvent(FPointerEvent(FSlateApplication::CursorPointerIndex, Position, Position, Pressed, EKeys::LeftMouseButton, 0.0f, FModifierKeysState()));
	if (!bDown || !bUp) { Fail(TEXT("actual Slate mouse event was not handled")); return false; }
	// CommonInput can consume events while a layer is settling. A handled event
	// is only a button press when the real SButton entered its pressed state.
	if (!bTargetPressed || !bTargetCaptured)
	{
		if (FPlatformTime::Seconds() - LastWaitAt > 5.0)
		{
			AMiniPlayerController* PC = Task24PC(GetTickableGameObjectWorld());
			const UCommonUIActionRouterBase* Router = PC->GetLocalPlayer()->GetSubsystem<UCommonUIActionRouterBase>();
			const UCommonInputSubsystem* Input = PC->GetLocalPlayer()->GetSubsystem<UCommonInputSubsystem>();
			const TSharedPtr<SWidget> Focus = Slate.GetUserFocusedWidget(0);
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe MOUSE_ROUTE_STATE: Action=%s Pressed=%d Capture=%d CaptorBefore=%s Focus=%s Leaf=%s Mode=%d Pending=%d InputFilter=%d"),
				*Action.ToString(), int32(bTargetPressed), int32(bTargetCaptured), CaptorBefore.IsValid() ? *CaptorBefore->GetTypeAsString() : TEXT("None"),
				Focus.IsValid() ? *Focus->GetTypeAsString() : TEXT("None"), *GetNameSafe(Router->GetLeafmostActivatableWidget()),
				int32(Router->GetActiveInputMode(ECommonInputMode::MAX)), int32(Router->IsPendingTreeChange()), Input ? int32(Input->GetInputTypeFilter(ECommonInputType::MouseAndKeyboard)) : -1);
		}
		LogWait(TEXT("SlateTargetPressedAndCaptured")); return false;
	}
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe UI_CLICK: Peer=%s Action=%s MouseDown=1 MouseUp=1 X=%.1f Y=%.1f Window=1 MouseMove=1 TargetHover=%d TargetPressed=%d TargetCapture=%d AppActive=%d ProbeBackgroundCapture=1"),
		*Peer, *Action.ToString(), Position.X, Position.Y, bTargetHovered ? 1 : 0, Button ? int32(bTargetPressed) : -1, Button ? int32(bTargetCaptured) : -1, Slate.IsActive() ? 1 : 0);
	LastInputAt = FPlatformTime::Seconds();
	return true;
}

bool UMiniTask24ProbeSubsystem::SendKey(FKey Key, bool bControl)
{
	if (!FSlateApplication::IsInitialized()) { return false; }
	const FModifierKeysState Modifiers(false, false, bControl, false, false, false, false, false, false);
	FSlateApplication& Slate = FSlateApplication::Get();
	const TSharedPtr<SWidget> FocusBefore = Slate.GetUserFocusedWidget(0);
	AMiniPlayerController* PC = Task24PC(GetTickableGameObjectWorld());
	const UCommonUIActionRouterBase* Router = PC && PC->GetLocalPlayer()
		? PC->GetLocalPlayer()->GetSubsystem<UCommonUIActionRouterBase>() : nullptr;
	const bool bHandled = Slate.ProcessKeyDownEvent(FKeyEvent(Key, Modifiers, 0, false, 0, 0));
	Slate.ProcessKeyUpEvent(FKeyEvent(Key, Modifiers, 0, false, 0, 0));
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe UI_KEY: Peer=%s Key=%s Control=%d Routed=1 Handled=%d Focus=%s RouterMode=%d"),
		*Peer, *Key.ToString(), bControl ? 1 : 0, bHandled ? 1 : 0,
		FocusBefore.IsValid() ? *FocusBefore->GetTypeAsString() : TEXT("None"), Router ? int32(Router->GetActiveInputMode(ECommonInputMode::MAX)) : -1);
	return bHandled;
}

bool UMiniTask24ProbeSubsystem::TypeAddress(const FString& InAddress)
{
	UMiniFrontEndWidget* Front = Task24Front(Task24PC(GetTickableGameObjectWorld()));
	UEditableTextBox* Entry = Front ? Front->GetAddressEntry() : nullptr;
	if (!Entry || !Click(Entry, TEXT("AddressEntry"))) { return false; }
	SendKey(EKeys::A, true);
	SendKey(EKeys::BackSpace);
	for (TCHAR Char : InAddress) { FSlateApplication::Get().ProcessKeyCharEvent(FCharacterEvent(Char, FModifierKeysState(), 0, false)); }
	if (Entry->GetText().ToString() != InAddress) { Fail(TEXT("routed Slate characters did not reach the address field")); return false; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe ADDRESS_TYPED: Peer=%s Address=%s FocusMouse=1 SelectAllKey=1 CharacterEvents=%d ExactText=1"), *Peer, *InAddress, InAddress.Len());
	return true;
}

bool UMiniTask24ProbeSubsystem::JoinThroughUI(const FString& InAddress)
{
	if (!bJoinTyped)
	{
		if (!TypeAddress(InAddress)) { return false; }
		bJoinTyped = true;
		return false; // Let the focused editable widget complete its current frame.
	}
	SaveIdentity();
	if (!SendKey(EKeys::Enter)) { Fail(TEXT("address Enter did not reach the real join handler")); return false; }
	bJoinTyped = false;
	return true;
}

bool UMiniTask24ProbeSubsystem::MenuAction(FName Action)
{
	AMiniPlayerController* PC = Task24PC(GetTickableGameObjectWorld());
	UMiniHUDLayout* HUD = Task24HUD(PC);
	UMiniDebugMenuWidget* Menu = HUD ? HUD->GetDebugMenu() : nullptr;
	if (!Menu || !Menu->IsActivated())
	{
		if (!Media(TEXT("BeforeMenu"))) { return false; }
		if (FPlatformTime::Seconds() - LastInputAt > 0.5)
		{
			const UCommonUIActionRouterBase* Router = PC && PC->GetLocalPlayer()
				? PC->GetLocalPlayer()->GetSubsystem<UCommonUIActionRouterBase>() : nullptr;
			UCommonActivatableWidgetContainerBase* GameLayer = Task24Root(PC)
				? Task24Root(PC)->GetLayerWidget(UMiniPrimaryGameLayout::GetGameLayerTag()) : nullptr;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe MENU_INPUT_STATE: Peer=%s RouterMode=%d HUDActive=%d HUDSize=%s GameLayerSize=%s GameCount=%d GameLayerVisible=%d"),
				*Peer, Router ? int32(Router->GetActiveInputMode(ECommonInputMode::MAX)) : -1, HUD && HUD->IsActivated() ? 1 : 0,
				HUD ? *HUD->GetCachedGeometry().GetLocalSize().ToString() : TEXT("None"),
				GameLayer ? *GameLayer->GetCachedGeometry().GetLocalSize().ToString() : TEXT("None"),
				GameLayer ? GameLayer->GetWidgetList().Num() : -1, GameLayer && GameLayer->IsVisible() ? 1 : 0);
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe ROUTER_WIDGET_STATE: Peer=%s Router=%s Leaf=%s Pending=%d HUDInRoot=%d HUDPlayer=%s HUDWorld=%s HUDPaintLayer=%d"),
				*Peer, *GetNameSafe(Router), *GetNameSafe(Router ? Router->GetLeafmostActivatableWidget() : nullptr),
				Router && Router->IsPendingTreeChange() ? 1 : 0, Router && Router->IsWidgetInActiveRoot(HUD) ? 1 : 0,
				HUD ? *GetNameSafe(HUD->GetOwningLocalPlayer()) : TEXT("None"), HUD ? *GetNameSafe(HUD->GetWorld()) : TEXT("None"),
				HUD && HUD->GetCachedWidget().IsValid() ? HUD->GetCachedWidget()->GetPersistentState().LayerId : -1);
			SendKey(EKeys::Escape);
			LastInputAt = FPlatformTime::Seconds();
		}
		LogWait(TEXT("EscapeOpenedMenu"));
		return false;
	}
	UMiniPrimaryGameLayout* Root = Task24Root(PC);
	if (!Root || Root->GetGameplayInputBlockCount() != 1 || !PC->IsMiniInputBlocked())
	{ Fail(TEXT("real gameplay menu failed to own one input gate")); return false; }
	if (!Media(TEXT("GameMenu"))) { return false; }
	SaveIdentity();
	return Click(Menu->GetButton(Action), Action);
}

bool UMiniTask24ProbeSubsystem::Media(FName Stage)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask24Media")) || CapturedMedia.Contains(Stage)) { return true; }
	if (PendingMedia == Stage)
	{
		if (IFileManager::Get().FileSize(*PendingMediaPath) > 1024)
		{
			CapturedMedia.Add(Stage);
			PendingMedia = NAME_None;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe SCREENSHOT_SAVED: Peer=%s Stage=%s Path=%s ActualPNG=1"), *Peer, *Stage.ToString(), *PendingMediaPath);
			return true;
		}
		if (FPlatformTime::Seconds() - MediaRequestedAt > 10.0) { Fail(TEXT("requested UI screenshot was not saved by the actual viewport")); }
		return false;
	}
#if WITH_EDITOR
	if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { return false; }
#endif
	IFileManager::Get().MakeDirectory(*MediaDirectory, true);
	PendingMediaPath = FPaths::Combine(MediaDirectory, FString::Printf(TEXT("Task24-%s-%s-%s.png"), *Mode, *Peer, *Stage.ToString()));
	IFileManager::Get().Delete(*PendingMediaPath, false, true);
	PendingMedia = Stage;
	MediaRequestedAt = FPlatformTime::Seconds();
	FScreenshotRequest::RequestScreenshot(PendingMediaPath, true, false);
	return false;
}

void UMiniTask24ProbeSubsystem::SaveIdentity()
{
	UWorld* World = GetTickableGameObjectWorld();
	AMiniPlayerController* PC = Task24PC(World);
	SavedWorld = World;
	SavedPlayerState = Task24PS(PC);
	SavedASC = Task24ASC(PC);
	SavedCleanupCount = CleanupCount;
	OldFrontEnd = Task24Front(PC);
	UMiniTravelSubsystem* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
	OldModal = Travel ? Travel->GetConnectionStatusWidget() : nullptr;
	OldHUD = Task24HUD(PC);
	OldVM = OldHUD ? OldHUD->GetViewModel() : nullptr;
	OldMenu = OldHUD ? OldHUD->GetDebugMenu() : nullptr;
	OldRoot = Task24Root(PC);
	bIdentitySaved = true;
}

void UMiniTask24ProbeSubsystem::HandleCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (!bInitialized || World != SavedWorld.Get()) { return; }
	if ((OldFrontEnd && OldFrontEnd->GetStateListenerCount()) || (OldModal && OldModal->GetStateListenerCount()) ||
		(OldVM && (OldVM->IsRunning() || OldVM->GetBindingCount())) ||
		(OldHUD && (OldHUD->GetExtensionWidgetCount() || OldHUD->GetExtensionPointCount())) ||
		(OldMenu && OldMenu->IsActivated()) || (OldRoot && OldRoot->GetGameplayInputBlockCount()))
	{ Fail(TEXT("ordinary travel retained old UI listeners, contributions or input gates")); return; }
	++CleanupCount;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe OLD_WORLD_RELEASED: Peer=%s World=%s FrontListeners=0 ModalListeners=0 HUD=0 Menu=0 UIGates=0 Cleanup=%d"), *Peer, *GetNameSafe(World), CleanupCount);
}

bool UMiniTask24ProbeSubsystem::VerifyNewIdentity()
{
	AMiniPlayerController* PC = Task24PC(GetTickableGameObjectWorld());
	if (!bIdentitySaved || !PC || !Task24PS(PC) || !Task24ASC(PC)) { return false; }
	if (GetTickableGameObjectWorld() == SavedWorld.Get() || Task24PS(PC) == SavedPlayerState.Get() || Task24ASC(PC) == SavedASC.Get() || CleanupCount <= SavedCleanupCount)
	{ Fail(TEXT("ordinary travel reused the old World, PlayerState or persistent ASC")); return false; }
	OldFrontEnd = nullptr; OldModal = nullptr; OldHUD = nullptr; OldVM = nullptr; OldMenu = nullptr; OldRoot = nullptr;
	bIdentitySaved = false;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe NEW_WORLD_PASS: Peer=%s NewWorld=1 NewPlayerState=1 NewASC=1 OldUIReleased=1"), *Peer);
	return true;
}

void UMiniTask24ProbeSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* Driver, int32 Type, const FString& Reason)
{
	if (!bInitialized || (World && World->GetGameInstance() != GetGameInstance())) { return; }
	++NetworkFailures;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe REAL_NETWORK_FAILURE: Peer=%s Type=%d Driver=%s Reason=%s Count=%d"), *Peer, Type, *GetNameSafe(Driver), *Reason, NetworkFailures);
}

bool UMiniTask24ProbeSubsystem::WriteSignal(const TCHAR* Name) const
{
	IFileManager::Get().MakeDirectory(*SignalDirectory, true);
	return FFileHelper::SaveStringToFile(TEXT("Verified by actual Task24 UI/network state.\n"), *FPaths::Combine(SignalDirectory, Name));
}

bool UMiniTask24ProbeSubsystem::HasSignal(const TCHAR* Name) const
{
	return !SignalDirectory.IsEmpty() && IFileManager::Get().FileExists(*FPaths::Combine(SignalDirectory, Name));
}

void UMiniTask24ProbeSubsystem::DiscoverOwners()
{
	UWorld* World = GetTickableGameObjectWorld();
	if (OwnerProbeWorld.Get() != World)
	{
		OwnerProbes.Reset(); OwnerProbeWorld = World; PublishedName = NAME_None; NextOwnerIndex = 1;
	}
	OwnerProbes.RemoveAll([](const TWeakObjectPtr<AMiniTask24ProbeActor>& Probe) { return !Probe.IsValid() || !IsValid(Probe->GetOwner()); });
	for (TActorIterator<AMiniPlayerController> It(World); It; ++It)
	{
		AMiniPlayerController* PC = *It;
		if (PC->IsLocalController() || !PC->GetNetConnection() || !Task24PS(PC) ||
			OwnerProbes.ContainsByPredicate([PC](const TWeakObjectPtr<AMiniTask24ProbeActor>& Probe) { return Probe->GetOwner() == PC; })) { continue; }
		FActorSpawnParameters Params;
		Params.Owner = PC;
		AMiniTask24ProbeActor* Probe = World->SpawnActor<AMiniTask24ProbeActor>(Params);
		if (!Probe) { Fail(TEXT("owner checkpoint actor spawn failed")); return; }
		Probe->InitializeServer(NextOwnerIndex++);
		OwnerProbes.Add(Probe);
		PublishedName = NAME_None;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe SERVER_JOIN: Owner=%d PlayerId=%d"), Probe->GetOwnerIndex(), Task24PS(PC)->GetPlayerId());
	}
}

bool UMiniTask24ProbeSubsystem::Checkpoint(FName Name, int32 ExpectedClients)
{
	if (!GameReady() || OwnerProbes.Num() != ExpectedClients) { return false; }
	UWorld* World = GetTickableGameObjectWorld();
	UMiniGamePhaseSubsystem* Phases = World->GetSubsystem<UMiniGamePhaseSubsystem>();
	UMiniMatchSubsystem* Matches = World->GetSubsystem<UMiniMatchSubsystem>();
	const FMiniGamePhaseState Phase = Phases->GetCurrentPhaseState();
	const FMiniMatchState Match = Matches->GetCurrentMatchState();
	if (Phase.PhaseTag != MiniGameplayTags::GamePhase_MiniArena_Playing || !Match.bAcceptingScores || Match.ConnectedPlayerCount != ExpectedClients + 1) { return false; }
	for (const TWeakObjectPtr<AMiniTask24ProbeActor>& Probe : OwnerProbes)
	{ if (!Task24PlayerReady(Cast<AMiniPlayerController>(Probe->GetOwner()))) { return false; } }
	if (PublishedName != Name || !MiniTask24SamePhase(Phase, PublishedPhase) || !MiniTask24SameMatch(Match, PublishedMatch))
	{
		PublishedName = Name; PublishedPhase = Phase; PublishedMatch = Match; ++Serial;
		for (const TWeakObjectPtr<AMiniTask24ProbeActor>& Probe : OwnerProbes) { Probe->SetCheckpoint(Serial, Name, Phase, Match); }
	}
	for (const TWeakObjectPtr<AMiniTask24ProbeActor>& Probe : OwnerProbes) { if (!Probe->HasAcknowledged()) { return false; } }
	return true;
}

AMiniTask24ProbeActor* UMiniTask24ProbeSubsystem::LocalProbe() const
{
	AMiniPlayerController* PC = Task24PC(GetTickableGameObjectWorld());
	for (TActorIterator<AMiniTask24ProbeActor> It(GetTickableGameObjectWorld()); It; ++It) { if (It->GetOwner() == PC) { return *It; } }
	return nullptr;
}

bool UMiniTask24ProbeSubsystem::AcknowledgeCheckpoint(FName Name)
{
	if (!GameReady()) { return false; }
	AMiniTask24ProbeActor* Probe = LocalProbe();
	if (!Probe || Probe->GetNameForCheckpoint() != Name || Probe->GetSerial() <= 0) { LogWait(TEXT("OwnerCheckpoint")); return false; }
	if (Probe->GetSerial() == ClientLastSerial) { return true; }
	UWorld* World = GetTickableGameObjectWorld();
	AMiniPlayerController* PC = Task24PC(World);
	AMiniPlayerState* PS = Task24PS(PC);
	const FMiniGamePhaseState Phase = World->GetSubsystem<UMiniGamePhaseSubsystem>()->GetCurrentPhaseState();
	const FMiniMatchState Match = World->GetSubsystem<UMiniMatchSubsystem>()->GetCurrentMatchState();
	UMiniHUDViewModel* VM = Task24HUD(PC)->GetViewModel();
	const FMiniPlayerMatchStats& Stats = PS->GetMatchStats();
	const FMiniHUDSnapshot& HUD = VM->GetSnapshot();
	const FMiniMatchPlayerRow* OwnRow = Match.Rows.FindByPredicate([PS](const FMiniMatchPlayerRow& Row) { return Row.PlayerId == PS->GetPlayerId(); });
	if (!MiniTask24SamePhase(Phase, Probe->GetExpectedPhase()) || !MiniTask24SameMatch(Match, Probe->GetExpectedMatch()) ||
		!MiniTask24SameMatch(HUD.MatchState, Match) || HUD.PhaseTag != Phase.PhaseTag || Stats.RoundId != Match.RoundId || !OwnRow || !OwnRow->bConnected ||
		OwnRow->Kills != Stats.Kills || OwnRow->Deaths != Stats.Deaths || HUD.Score != Stats.Kills || HUD.Deaths != Stats.Deaths ||
		PS->GetCurrentLifeId() != Probe->GetExpectedLifeId() ||
		Task24Pawn(PC) != Probe->GetExpectedPawn()) { return false; }
	ClientLastSerial = Probe->GetSerial();
	Probe->ServerAcknowledge(ClientLastSerial, Task24Experience(World)->GetCurrentExperienceId().PrimaryAssetName, Phase, Match,
		Stats, PS->GetCurrentLifeId(), Task24Pawn(PC), World->GetGameState()->GetServerWorldTimeSeconds(), true, true);
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask24Probe CLIENT_CHECKPOINT: Peer=%s Owner=%d Name=%s Round=%d Life=%u World=%s HUD=1 Gates=0"),
		*Peer, Probe->GetOwnerIndex(), *Name.ToString(), Match.RoundId, PS->GetCurrentLifeId(), *World->GetName());
	return true;
}
