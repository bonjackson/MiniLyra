#include "MiniTask26RoleProbeSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"

#if !UE_BUILD_SHIPPING
#include "Diagnostics/MiniTask26ActionProbeHooks.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHeroComponent.h"
#include "CommonActivatableWidget.h"
#include "Components/Button.h"
#include "EngineUtils.h"
#include "GameFeatureData.h"
#include "GameFeatures/MiniGameFeatureAction_AddActors.h"
#include "GameFeatures/MiniGameFeatureAction_AddWidgets.h"
#include "GameFeaturesSubsystem.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniExperienceActionSet.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGameState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/Base64.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/MiniPlayerController.h"
#include "System/MiniLogChannels.h"
#include "System/MiniTravelSubsystem.h"
#include "UI/MiniConnectionStatusWidget.h"
#include "UI/MiniHUDLayout.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "UnrealClient.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
FString Mini26RoleMap(const UWorld* World) { return World ? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) : FString(); }
UMiniExperienceManagerComponent* Mini26RoleManager(UWorld* World)
{
	const auto* State = World ? World->GetGameState<AMiniGameState>() : nullptr;
	return State ? State->GetExperienceManagerComponent() : nullptr;
}
AMiniPlayerController* Mini26RoleLocalPC(UWorld* World)
{
	if (World) { for (TActorIterator<AMiniPlayerController> It(World); It; ++It) { if (It->IsLocalController()) { return *It; } } }
	return nullptr;
}
UMiniPrimaryGameLayout* Mini26RoleRoot(UWorld* World)
{
	auto* PC = Mini26RoleLocalPC(World);
	return PC && PC->GetLocalPlayer() ? Cast<UMiniPrimaryGameLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(PC->GetLocalPlayer())) : nullptr;
}
UMiniHUDLayout* Mini26RoleHUD(UMiniPrimaryGameLayout* Root)
{
	UMiniHUDLayout* Result = nullptr;
	const auto* Layer = Root ? Root->GetLayerWidget(UMiniPrimaryGameLayout::GetGameLayerTag()) : nullptr;
	if (Layer) { for (auto* Widget : Layer->GetWidgetList()) { if (auto* HUD = Cast<UMiniHUDLayout>(Widget); HUD && HUD->IsActivated())
		{ if (Result) { return nullptr; } Result = HUD; } } }
	return Result;
}
FSoftObjectPath Mini26RoleMissingClass(const FString& Scenario)
{
	return FSoftObjectPath(FString::Printf(TEXT("/Game/Mini/Diagnostics/Task26/MissingRole%s.MissingRole%s_C"), *Scenario, *Scenario));
}
}
#endif

bool UMiniTask26RoleProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	FString Value;
	return Super::ShouldCreateSubsystem(Outer) && FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask26Role="), Value);
#else
	return false;
#endif
}
void UMiniTask26RoleProbeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if !UE_BUILD_SHIPPING
	FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask26Role="), Role);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask26RoleScenario="), Scenario);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask26RolePeer="), Peer);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask26RoleFinishFile="), FinishFile);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask26RoleInjectFile="), InjectFile);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask26RoleMediaOutput="), MediaDirectory);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask26RoleTimeoutSeconds="), TimeoutSeconds);
	if (MediaDirectory.IsEmpty()) { MediaDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots")); }
	bInitialized = true; StartedAt = FPlatformTime::Seconds();
	const TArray<FString> Roles{ TEXT("Standalone"), TEXT("Listen"), TEXT("Client"), TEXT("Dedicated") };
	const TArray<FString> Scenarios{ TEXT("Normal"), TEXT("ActorClassFailure"), TEXT("LateUIFailure") };
	if (!Roles.Contains(Role) || !Scenarios.Contains(Scenario) || Peer.IsEmpty() || FinishFile.IsEmpty() ||
		(Scenario == TEXT("LateUIFailure") && (Role == TEXT("Dedicated") || Role == TEXT("Standalone") || InjectFile.IsEmpty())))
	{ Fail(TEXT("invalid independent role scenario or control paths")); return; }
	// Practice owns real AddActors contributions; Arena deliberately has none.
	// Use the same assembly across roles to exercise the required Actor barrier.
	TargetMap = TEXT("/Game/Mini/Maps/L_MiniPractice");
	SelectionHandle = FMiniTask26ActionProbeHooks::SelectionPrepared().AddUObject(this, &ThisClass::SelectionPrepared);
	ResourceHandle = FMiniTask26ActionProbeHooks::ResourcesPrepared().AddUObject(this, &ThisClass::ResourcesPrepared);
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Role START: Role=%s Peer=%s Scenario=%s Target=%s OptIn=1"), *Role, *Peer, *Scenario, *TargetMap);
#endif
}
void UMiniTask26RoleProbeSubsystem::Deinitialize()
{
	bInitialized = false;
#if !UE_BUILD_SHIPPING
	FMiniTask26ActionProbeHooks::SelectionPrepared().Remove(SelectionHandle);
	FMiniTask26ActionProbeHooks::ResourcesPrepared().Remove(ResourceHandle);
	ActorActions.Reset(); SelectedHUDAction = nullptr;
#endif
	Super::Deinitialize();
}
bool UMiniTask26RoleProbeSubsystem::IsTickable() const
{
#if !UE_BUILD_SHIPPING
	return !IsTemplate() && bInitialized && !bDone;
#else
	return false;
#endif
}
TStatId UMiniTask26RoleProbeSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask26RoleProbeSubsystem, STATGROUP_Tickables); }
UWorld* UMiniTask26RoleProbeSubsystem::GetTickableGameObjectWorld() const { return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr; }

#if !UE_BUILD_SHIPPING
bool UMiniTask26RoleProbeSubsystem::Claim(UMiniExperienceManagerComponent* Manager, UWorld* World)
{
	if (!bInitialized || bDone || !Manager || !World || !World->IsGameWorld() || World->bIsTearingDown ||
		World->GetGameInstance() != GetGameInstance() || Mini26RoleMap(World) != TargetMap) { return false; }
	const ENetMode ExpectedMode = Role == TEXT("Standalone") ? NM_Standalone : Role == TEXT("Listen") ? NM_ListenServer :
		Role == TEXT("Dedicated") ? NM_DedicatedServer : NM_Client;
	if (World->GetNetMode() != ExpectedMode) { return false; }
	if (Claims) { return ClaimedWorld.Get() == World && ClaimedManager.Get() == Manager; }
	ClaimedWorld = World; ClaimedManager = Manager; ++Claims;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Role CLAIM: Role=%s Peer=%s Scenario=%s World=%s NetMode=%d Count=1"), *Role, *Peer, *Scenario, *World->GetName(), int32(World->GetNetMode()));
	Manager->CallOrRegister_OnExperienceLoaded(FOnMiniExperienceLoaded::FDelegate::CreateUObject(this, &ThisClass::Loaded));
	Manager->CallOrRegister_OnExperienceFailed(FOnMiniExperienceFailed::FDelegate::CreateUObject(this, &ThisClass::Failed));
	return true;
}
void UMiniTask26RoleProbeSubsystem::SelectionPrepared(UMiniExperienceManagerComponent* Manager, UWorld* World, FPrimaryAssetId& Id)
{
	Claim(Manager, World);
}
void UMiniTask26RoleProbeSubsystem::ResourcesPrepared(UMiniExperienceManagerComponent* Manager, UWorld* World,
	TArray<FMiniRequiredActionClassRequest>& Requests)
{
	if (!Claim(Manager, World) || bResourcesObserved) { return; }
	bResourcesObserved = true;
	for (const auto& Request : Requests)
	{
		LayoutRequests += Request.Kind == EMiniRequiredActionClassKind::Layout ? 1 : 0;
		ElementRequests += Request.Kind == EMiniRequiredActionClassKind::HUDElement ? 1 : 0;
		ActorRequests += Request.Kind == EMiniRequiredActionClassKind::ReplicatedActor ? 1 : 0;
		if (auto* Actors = Cast<UMiniGameFeatureAction_AddActors>(Request.Action.Get())) { ActorActions.AddUnique(Actors); }
		if (Request.Kind != EMiniRequiredActionClassKind::ReplicatedActor && Request.Action.IsValid() && Request.Action->GetTypedOuter<UGameFeatureData>())
		{
			++SelectedPluginUIRequests;
			if (auto* Widgets = Cast<UMiniGameFeatureAction_AddWidgets>(Request.Action.Get())) { SelectedHUDAction = Widgets; }
		}
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Role REQUIRED_REQUEST: Role=%s Peer=%s Action=%s Kind=%d Path=%s SelectedGFD=%d"),
			*Role, *Peer, *GetPathNameSafe(Request.Action.Get()), int32(Request.Kind), *Request.ClassPath.ToString(),
			Request.Action.IsValid() && Request.Action->GetTypedOuter<UGameFeatureData>() ? 1 : 0);
	}
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Role RESOURCE_SCOPE: Role=%s Peer=%s Layouts=%d Elements=%d ActorClasses=%d SelectedGFDUI=%d"),
		*Role, *Peer, LayoutRequests, ElementRequests, ActorRequests, SelectedPluginUIRequests);
	if (Scenario == TEXT("ActorClassFailure") && World->GetNetMode() != NM_Client)
	{
		for (auto& Request : Requests)
		{
			if (Request.Kind == EMiniRequiredActionClassKind::ReplicatedActor)
			{
				Request.ClassPath = Mini26RoleMissingClass(Scenario); ++MutationCount;
				UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Role CENTRAL_INJECTED: Role=%s Peer=%s Authority=1 Count=1 RealRequestAsyncLoad=1 Path=%s"), *Role, *Peer, *Request.ClassPath.ToString());
				return;
			}
		}
		Fail(TEXT("authority central snapshot did not include an Actor request"));
	}
}
void UMiniTask26RoleProbeSubsystem::Loaded(const UMiniExperienceDefinition* Experience)
{
	++LoadedCount;
	if (Experience)
	{
		const auto RememberActors = [this](const TArray<TObjectPtr<UGameFeatureAction>>& Actions)
		{
			for (UGameFeatureAction* Action : Actions) { if (auto* Actors = Cast<UMiniGameFeatureAction_AddActors>(Action)) { ActorActions.AddUnique(Actors); } }
		};
		RememberActors(Experience->Actions);
		for (const UMiniExperienceActionSet* Set : Experience->ActionSets) { if (Set) { RememberActors(Set->Actions); } }
	}
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Role LOADED: Role=%s Peer=%s Scenario=%s Count=%d ID=%s"), *Role, *Peer, *Scenario, LoadedCount,
		Experience ? *Experience->GetPrimaryAssetId().ToString() : TEXT("None"));
	if (LoadedCount != 1 || !Experience || Experience->bIsFrontEnd ||
		(Scenario == TEXT("ActorClassFailure") && Role != TEXT("Client"))) { Fail(TEXT("unexpected/duplicate authority Loaded before required Actor failure")); }
}
void UMiniTask26RoleProbeSubsystem::Failed(const FString& Reason)
{
	++FailedCount; FailureReason = Reason;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Role FAILED_OBSERVED: Role=%s Peer=%s Scenario=%s Count=%d Loaded=%d ReasonB64=%s Reason=%s"),
		*Role, *Peer, *Scenario, FailedCount, LoadedCount, *FBase64::Encode(Reason), *Reason);
	if (Scenario == TEXT("Normal") || FailedCount != 1 ||
		(Scenario == TEXT("ActorClassFailure") && !Reason.Contains(TEXT("/Game/Mini/Diagnostics/Task26/MissingRole"))) ||
		(Scenario == TEXT("LateUIFailure") && (!bReadyEvidence || LoadedCount != 1))) { Fail(TEXT("unexpected failure or late scenario did not first prove real Ready")); }
}
bool UMiniTask26RoleProbeSubsystem::NormalReady()
{
	UWorld* World = ClaimedWorld.Get(); auto* Manager = ClaimedManager.Get();
	if (!World || World->bIsTearingDown || !Manager || !Manager->IsExperienceLoaded() || LoadedCount != 1 || FailedCount || !bResourcesObserved) { return false; }
	const bool bClient = World->GetNetMode() == NM_Client;
	const bool bDedicated = World->GetNetMode() == NM_DedicatedServer;
	if (ActorActions.IsEmpty() || (bClient && ActorRequests != 0) || (!bClient && ActorRequests <= 0) ||
		(bDedicated && (LayoutRequests || ElementRequests || SelectedPluginUIRequests)) ||
		(!bDedicated && (LayoutRequests < 1 || ElementRequests != 4 || SelectedPluginUIRequests != 5)))
	{ Fail(TEXT("central resource role scope did not match Client/Server UI/Actor contract")); return false; }
	int32 TotalActors = 0;
	for (const UMiniGameFeatureAction_AddActors* Action : ActorActions)
	{
		int32 Worlds, Actors, Pending, Bindings; bool Ready, Failure;
		Action->GetWorldStats(World, Worlds, Actors, Pending, Bindings, Ready, Failure);
		if (Worlds != 1 || !Ready || Failure || Pending || Bindings || Actors != (bClient ? 0 : Action->Actors.Num())) { return false; }
		TotalActors += Actors;
	}
	auto* Root = Mini26RoleRoot(World); auto* PC = Mini26RoleLocalPC(World);
	if (bDedicated)
	{
		if (PC || Root || GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>())
		{ Fail(TEXT("Dedicated role created local UI/controller/travel state")); return false; }
	}
	else
	{
		auto* HUD = Mini26RoleHUD(Root); auto* Pawn = PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr;
		if (!Root || !Root->IsLayoutReady() || Root->GetGameplayInputBlockCount() || !HUD || HUD->GetExtensionWidgetCount() != 4 ||
			!Pawn || !Pawn->GetHeroComponent() || !Pawn->GetHeroComponent()->IsInputActive()) { return false; }
	}
	if (!Media(TEXT("Ready"))) { return false; }
	bReadyEvidence = true;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Role READY: Role=%s Peer=%s Scenario=%s Loaded=1 Failed=0 ResourceScope=1 ActorsReady=1 AuthorityActors=%d ClientActorSkip=%d LocalUI=%d HUDWidgets=%d LocalInput=%d"),
		*Role, *Peer, *Scenario, TotalActors, bClient ? 1 : 0, bDedicated ? 0 : 1, bDedicated ? 0 : 4, bDedicated ? 0 : 1);
	return true;
}
bool UMiniTask26RoleProbeSubsystem::InjectLateUIFailure()
{
	UWorld* World = ClaimedWorld.Get(); auto* Manager = ClaimedManager.Get(); auto* Widgets = SelectedHUDAction.Get();
	if (Role != TEXT("Listen") || !bReadyEvidence || !World || !Manager || !Manager->IsExperienceLoaded() || !Widgets || bLateInjected) { return false; }
	// Existing suspension tears down real contributions, but keeps this World's
	// registrations/Manager observers. Rearming creates a new genuine self load.
	Widgets->SetProbeSuspended(World, true);
	bool bChanged = false;
	for (auto& Context : Widgets->ContextData)
	{
		if (auto* Data = Context.Value.Worlds.Find(World); Data && !Data->LayoutRequests.IsEmpty())
		{
			Data->LayoutRequests[0].LayoutClass = TSoftClassPtr<UCommonActivatableWidget>(Mini26RoleMissingClass(Scenario));
			bChanged = true; break;
		}
	}
	if (!bChanged) { Fail(TEXT("selected HUD Action had no exact World layout snapshot")); return false; }
	++MutationCount; bLateInjected = true;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Role LATE_UI_REARMED: Role=%s Peer=%s Authority=1 ActualPriorLoaded=1 ExactWorldSnapshot=1 SharedAssetChanged=0 DiagnosticRearm=1 RealSelfRequestAsyncLoad=1"), *Role, *Peer);
	Widgets->SetProbeSuspended(World, false);
	return true;
}
bool UMiniTask26RoleProbeSubsystem::FailureReady()
{
	UWorld* World = ClaimedWorld.Get(); auto* Manager = ClaimedManager.Get();
	if (!World || !Manager || Manager->GetLoadState() != EMiniExperienceLoadState::Failed || FailedCount != 1) { return false; }
	const bool bAuthority = World->GetNetMode() != NM_Client;
	if ((bAuthority && MutationCount != 1) || (!bAuthority && MutationCount != 0) ||
		(Scenario == TEXT("ActorClassFailure") && bAuthority && LoadedCount != 0) ||
		(Scenario == TEXT("LateUIFailure") && LoadedCount != 1) || Manager->GetFailureReason() != FailureReason)
	{ Fail(TEXT("terminal failure did not match the authority-only mutation contract")); return false; }
	if (Scenario == TEXT("ActorClassFailure") && !FailureReason.Contains(Mini26RoleMissingClass(Scenario).ToString()))
	{ Fail(TEXT("central failure did not contain the exact missing Actor class")); return false; }
	if (Scenario == TEXT("LateUIFailure") && (!FailureReason.Contains(TEXT("required widget class or tag is invalid")) || !FailureReason.Contains(TEXT("MiniTask19_AddWidgets"))))
	{ Fail(TEXT("late failure was not the selected HUD Action's real self load")); return false; }
	int32 LivePawns = 0;
	for (TActorIterator<AMiniCharacter> It(World); It; ++It) { LivePawns += !It->IsActorBeingDestroyed() ? 1 : 0; }
	if (Scenario == TEXT("ActorClassFailure") && LivePawns) { Fail(TEXT("required central Actor failure allowed a gameplay Pawn")); return false; }
	for (const UMiniGameFeatureAction_AddActors* Action : ActorActions)
	{
		int32 Worlds, Actors, Pending, Bindings; bool Ready, Failure;
		Action->GetWorldStats(World, Worlds, Actors, Pending, Bindings, Ready, Failure);
		if (Actors || Pending || Bindings) { return false; }
	}
	const bool bDedicated = World->GetNetMode() == NM_DedicatedServer;
	if (!bDedicated)
	{
		auto* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>(); auto* Root = Mini26RoleRoot(World);
		auto* Modal = Travel ? Travel->GetConnectionStatusWidget() : nullptr;
		if (!Travel || Travel->GetState().bBusy || !Travel->GetState().bHasError || Travel->GetState().FailureCode != TEXT("MINI_EXPERIENCE_FAILED") ||
			!Root || !Modal || !Modal->IsActivated() || Modal->GetStateListenerCount() != 1 || !Modal->GetButton(TEXT("ReturnButton")) ||
			!Modal->GetButton(TEXT("ReturnButton"))->IsVisible() || Root->GetGameplayInputBlockCount() < 1 || Mini26RoleHUD(Root)) { return false; }
	}
	if (!Media(TEXT("Failure"))) { return false; }
	bFailureEvidence = true;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Role FAILURE_VERIFIED: Role=%s Peer=%s Scenario=%s Loaded=%d Failed=1 AuthorityMutation=%d NativeModal=%d Pawn=%d ActionResources=0 ReasonB64=%s"),
		*Role, *Peer, *Scenario, LoadedCount, MutationCount, bDedicated ? 0 : 1, LivePawns, *FBase64::Encode(FailureReason));
	return true;
}
bool UMiniTask26RoleProbeSubsystem::Media(FName Stage)
{
	if (Role == TEXT("Dedicated") || !FParse::Param(FCommandLine::Get(), TEXT("MiniTask26RoleMedia")) || MediaDone.Contains(Stage)) { return true; }
	if (!MediaStage.IsNone())
	{
		if (MediaStage != Stage) { return false; }
		if (IFileManager::Get().FileSize(*MediaPath) > 1024)
		{
			MediaDone.Add(Stage); MediaStage = NAME_None;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Role SCREENSHOT_SAVED: Role=%s Peer=%s Scenario=%s Stage=%s Path=%s"), *Role, *Peer, *Scenario, *Stage.ToString(), *MediaPath);
			return true;
		}
		if (FPlatformTime::Seconds() - MediaStartedAt > 30.0) { Fail(TEXT("role screenshot was not saved")); }
		return false;
	}
#if WITH_EDITOR
	if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { return false; }
#endif
	IFileManager::Get().MakeDirectory(*MediaDirectory, true);
	MediaPath = FPaths::Combine(MediaDirectory, FString::Printf(TEXT("Task26Roles-%s-%s-%s.png"), *Scenario, *Peer, *Stage.ToString()));
	IFileManager::Get().Delete(*MediaPath, false, true); MediaStage = Stage; MediaStartedAt = FPlatformTime::Seconds();
	FScreenshotRequest::RequestScreenshot(MediaPath, true, false); return false;
}
void UMiniTask26RoleProbeSubsystem::Pass()
{
	if (bDone) { return; }
	if (Claims != 1 || (Scenario == TEXT("Normal") ? (!bReadyEvidence || FailedCount || MutationCount) : !bFailureEvidence))
	{ Fail(TEXT("role acceptance was incomplete at normal exit")); return; }
	bDone = true;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Role PASS: Role=%s Peer=%s Scenario=%s Claimed=1 Loaded=%d Failed=%d Mutations=%d"), *Role, *Peer, *Scenario, LoadedCount, FailedCount, MutationCount);
	// Runner has observed all peer evidence before requesting normal process exit.
	FPlatformMisc::RequestExit(false, TEXT("MiniTask26RoleFinished"));
}
void UMiniTask26RoleProbeSubsystem::Fail(const TCHAR* Reason)
{
	if (bDone) { return; }
	bDone = true;
	UE_LOG(LogMiniInit, Error, TEXT("MiniTask26Role FAIL: Role=%s Peer=%s Scenario=%s Reason=%s"), *Role, *Peer, *Scenario, Reason);
	FPlatformMisc::RequestExitWithStatus(true, 1, TEXT("MiniTask26RoleFailure"));
}
#endif

void UMiniTask26RoleProbeSubsystem::Tick(float DeltaTime)
{
#if !UE_BUILD_SHIPPING
	if (!bInitialized || bDone) { return; }
	if (FPlatformTime::Seconds() - StartedAt > TimeoutSeconds) { Fail(TEXT("finite independent role deadline expired")); return; }
	UWorld* World = GetTickableGameObjectWorld();
	if (!Claims) { Claim(Mini26RoleManager(World), World); }
	if (!Claims) { return; }
	if (World != ClaimedWorld.Get() || !ClaimedManager.IsValid()) { Fail(TEXT("role target traveled or ended before evidence completion")); return; }
	if ((Scenario == TEXT("Normal") || Scenario == TEXT("LateUIFailure")) && !bReadyEvidence && !NormalReady()) { return; }
	if (Scenario == TEXT("LateUIFailure") && Role == TEXT("Listen") && !bLateInjected && IFileManager::Get().FileExists(*InjectFile))
	{ if (!InjectLateUIFailure()) { return; } }
	if (Scenario != TEXT("Normal") && !bFailureEvidence && !FailureReady()) { return; }
	if (IFileManager::Get().FileExists(*FinishFile)) { Pass(); }
#endif
}
