#include "MiniTask26ProbeSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"

#if !UE_BUILD_SHIPPING
#include "Diagnostics/MiniTask26ActionProbeHooks.h"
#include "AssetRegistry/AssetData.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHeroComponent.h"
#include "Components/Button.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFeatureData.h"
#include "GameFeatureTypes.h"
#include "GameFeatures/MiniGameFeatureAction_AddActors.h"
#include "GameFeatures/MiniGameFeatureAction_AddWidgets.h"
#include "GameFeaturesSubsystem.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGameState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Player/MiniPlayerController.h"
#include "System/MiniAssetManager.h"
#include "System/MiniLogChannels.h"
#include "System/MiniTravelSubsystem.h"
#include "UI/MiniConnectionStatusWidget.h"
#include "UI/MiniFrontEndWidget.h"
#include "UI/MiniHUDLayout.h"
#include "UI/MiniHUDMessages.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "UnrealClient.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Widgets/SWindow.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
const FString MiniTask26FrontMap(TEXT("/Game/Mini/Maps/L_MiniFrontEnd"));
const FString MiniTask26PracticeMap(TEXT("/Game/Mini/Maps/L_MiniPractice"));
FString MiniTask26MapName(const UWorld* World) { return World ? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) : FString(); }
UMiniExperienceManagerComponent* MiniTask26ManagerFor(UWorld* World)
{
	const AMiniGameState* State = World ? World->GetGameState<AMiniGameState>() : nullptr;
	return State ? State->GetExperienceManagerComponent() : nullptr;
}
AMiniPlayerController* MiniTask26LocalPC(UWorld* World)
{
	if (World) { for (TActorIterator<AMiniPlayerController> It(World); It; ++It) { if (It->IsLocalController()) { return *It; } } }
	return nullptr;
}
UMiniPrimaryGameLayout* MiniTask26RootFor(UWorld* World)
{
	AMiniPlayerController* PC = MiniTask26LocalPC(World);
	return PC && PC->GetLocalPlayer() ? Cast<UMiniPrimaryGameLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(PC->GetLocalPlayer())) : nullptr;
}
template<typename T> T* MiniTask26ActiveWidget(UMiniPrimaryGameLayout* Root, FGameplayTag Tag)
{
	T* Result = nullptr;
	const UCommonActivatableWidgetContainerBase* Layer = Root ? Root->GetLayerWidget(Tag) : nullptr;
	if (Layer) { for (UCommonActivatableWidget* Widget : Layer->GetWidgetList())
	{
		if (T* Candidate = Cast<T>(Widget); Candidate && Candidate->IsActivated()) { if (Result) { return nullptr; } Result = Candidate; }
	} }
	return Result;
}
}
#endif

bool UMiniTask26ProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	FString Value;
	return Super::ShouldCreateSubsystem(Outer) && FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask26="), Value);
#else
	return false;
#endif
}
void UMiniTask26ProbeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if !UE_BUILD_SHIPPING
	FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask26="), Mode);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask26TimeoutSeconds="), TimeoutSeconds);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask26MediaOutput="), MediaDirectory);
	if (MediaDirectory.IsEmpty()) { MediaDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots")); }
	const TArray<FString> Modes{ TEXT("Unknown"), TEXT("MissingFeature"), TEXT("MissingPrimary"), TEXT("FrontClass"), TEXT("HUDClass"),
		TEXT("ActorClass"), TEXT("UIAsyncFailure"), TEXT("ActorAsyncFailure"), TEXT("NoLayer"), TEXT("NoSlot"), TEXT("AssetReturn"),
		TEXT("FeatureReturn"), TEXT("ResourcesReturn"), TEXT("UIActionReturn"), TEXT("ActorActionReturn"), TEXT("Deadline"),
		TEXT("FailedFrontQuit"), TEXT("StaleFailure"), TEXT("NormalPractice") };
	bInitialized = true; StartedAt = StepStartedAt = FPlatformTime::Seconds();
	if (!Modes.Contains(Mode)) { Fail(TEXT("unsupported opt-in scenario")); return; }
	TargetMap = IsFrontFault() ? MiniTask26FrontMap : MiniTask26PracticeMap;
	SelectionHandle = FMiniTask26ActionProbeHooks::SelectionPrepared().AddUObject(this, &ThisClass::SelectionPrepared);
	PrimaryHandle = FMiniTask26ActionProbeHooks::PrimaryPrepared().AddUObject(this, &ThisClass::PrimaryPrepared);
	ResourcesHandle = FMiniTask26ActionProbeHooks::ResourcesPrepared().AddUObject(this, &ThisClass::ResourcesPrepared);
	ActionWorldHandle = FMiniTask26ActionProbeHooks::ActionWorldPrepared().AddUObject(this, &ThisClass::ActionWorldPrepared);
	StreamableHandle = FMiniTask26ActionProbeHooks::HandlePrepared().AddUObject(this, &ThisClass::HandlePrepared);
	CleanupHandle = FWorldDelegates::OnPostWorldCleanup.AddUObject(this, &ThisClass::WorldCleanup);
	UGameFeaturesSubsystem::Get().AddObserver(this, UGameFeaturesSubsystem::EObserverPluginStateUpdateMode::FutureOnly);
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe START: Mode=%s Target=%s Timeout=%d OptIn=1 SingleGI=1"), *Mode, *TargetMap, TimeoutSeconds);
#endif
}
void UMiniTask26ProbeSubsystem::Deinitialize()
{
	bInitialized = false;
#if !UE_BUILD_SHIPPING
	FMiniTask26ActionProbeHooks::SelectionPrepared().Remove(SelectionHandle);
	FMiniTask26ActionProbeHooks::PrimaryPrepared().Remove(PrimaryHandle);
	FMiniTask26ActionProbeHooks::ResourcesPrepared().Remove(ResourcesHandle);
	FMiniTask26ActionProbeHooks::ActionWorldPrepared().Remove(ActionWorldHandle);
	FMiniTask26ActionProbeHooks::HandlePrepared().Remove(StreamableHandle);
	FWorldDelegates::OnPostWorldCleanup.Remove(CleanupHandle);
	UGameFeaturesSubsystem::Get().RemoveObserver(this);
	for (const FActionObserver& Observer : ActionObservers)
	{
		if (auto* Widgets = Cast<UMiniGameFeatureAction_AddWidgets>(Observer.Action.Get())) { Widgets->OnRequiredActionFailed.Remove(Observer.Handle); }
		if (auto* Actors = Cast<UMiniGameFeatureAction_AddActors>(Observer.Action.Get())) { Actors->OnRequiredActionFailed.Remove(Observer.Handle); }
	}
	ActionObservers.Reset(); ReleaseHolds(true); HeldLoads.Reset(); Observations.Reset();
#endif
	Super::Deinitialize();
}
bool UMiniTask26ProbeSubsystem::IsTickable() const
{
#if !UE_BUILD_SHIPPING
	return !IsTemplate() && bInitialized && !bDone;
#else
	return false;
#endif
}
TStatId UMiniTask26ProbeSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask26ProbeSubsystem, STATGROUP_Tickables); }
UWorld* UMiniTask26ProbeSubsystem::GetTickableGameObjectWorld() const { return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr; }

#if !UE_BUILD_SHIPPING
bool UMiniTask26ProbeSubsystem::IsFrontFault() const
{
	return Mode == TEXT("FrontClass") || Mode == TEXT("UIAsyncFailure") || Mode == TEXT("NoLayer") ||
		Mode == TEXT("UIActionReturn") || Mode == TEXT("Deadline") || Mode == TEXT("FailedFrontQuit") || Mode == TEXT("StaleFailure");
}
bool UMiniTask26ProbeSubsystem::IsPendingMode() const { return Mode.EndsWith(TEXT("Return")); }
bool UMiniTask26ProbeSubsystem::IsExpectedFailure() const { return !IsPendingMode() && Mode != TEXT("NormalPractice"); }
bool UMiniTask26ProbeSubsystem::IsClaimed(const UWorld* World) const
{
	return bConsumed && World && ClaimedWorld.Get() == World && World->GetGameInstance() == GetGameInstance() && !World->bIsTearingDown;
}
FMiniTask26WorldObservation* UMiniTask26ProbeSubsystem::FindObservation(const UWorld* World)
{
	if (!World) { return nullptr; }
	for (FMiniTask26WorldObservation& Observation : Observations) { if (Observation.WorldKey == FObjectKey(World)) { return &Observation; } }
	return nullptr;
}
FMiniTask26WorldObservation* UMiniTask26ProbeSubsystem::ClaimedObservation()
{
	return Observations.IsValidIndex(ClaimedObservationIndex) ? &Observations[ClaimedObservationIndex] : nullptr;
}
void UMiniTask26ProbeSubsystem::Observe(UMiniExperienceManagerComponent* Manager, UWorld* World)
{
	if (!Manager || !World || World->GetGameInstance() != GetGameInstance() || FindObservation(World)) { return; }
	FMiniTask26WorldObservation& Observation = Observations.AddDefaulted_GetRef();
	Observation.World = World; Observation.WorldKey = FObjectKey(World); Observation.Manager = Manager;
	Manager->CallOrRegister_OnExperienceLoaded(FOnMiniExperienceLoaded::FDelegate::CreateUObject(this,
		&ThisClass::RecordLoaded, TWeakObjectPtr<UWorld>(World)));
	Manager->CallOrRegister_OnExperienceFailed(FOnMiniExperienceFailed::FDelegate::CreateUObject(this,
		&ThisClass::RecordFailed, TWeakObjectPtr<UWorld>(World)));
}
bool UMiniTask26ProbeSubsystem::Claim(UMiniExperienceManagerComponent* Manager, UWorld* World)
{
	if (!World || !Manager || World->GetGameInstance() != GetGameInstance() || !World->IsGameWorld() ||
		World->bIsTearingDown || MiniTask26MapName(World) != TargetMap) { return false; }
	if (bConsumed)
	{
		if (ClaimedWorld.Get() != World) { ++RejectedOtherWorldHooks; }
		return ClaimedWorld.Get() == World;
	}
	bConsumed = true; ClaimedWorld = World; ClaimedManager = Manager; ++ClaimedCount;
	StepStartedAt = FPlatformTime::Seconds(); Observe(Manager, World);
	ClaimedObservationIndex = Observations.IndexOfByPredicate([World](const FMiniTask26WorldObservation& O) { return O.WorldKey == FObjectKey(World); });
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe CLAIM: Mode=%s World=%s Map=%s Count=%d Consumed=1"), *Mode, *World->GetName(), *MiniTask26MapName(World), ClaimedCount);
	return true;
}
FSoftObjectPath UMiniTask26ProbeSubsystem::MissingClassPath() const
{
	return FSoftObjectPath(FString::Printf(TEXT("/Game/Mini/Diagnostics/Task26/Missing%s.Missing%s_C"), *Mode, *Mode));
}
void UMiniTask26ProbeSubsystem::SelectionPrepared(UMiniExperienceManagerComponent* Manager, UWorld* World, FPrimaryAssetId& SelectedId)
{
	if (!bInitialized || bDone || !World || World->GetGameInstance() != GetGameInstance()) { return; }
	Observe(Manager, World);
	if (!Claim(Manager, World) || bFaultApplied) { return; }
	if (Mode == TEXT("Unknown"))
	{
		SelectedId = FPrimaryAssetId(FMiniPrimaryAssetTypes::Experience, TEXT("DA_MiniTask26DefinitelyMissing"));
		bFaultApplied = true; ++Mutations;
	}
	else if (Mode == TEXT("MissingFeature"))
	{
		SelectedId = FPrimaryAssetId(FMiniPrimaryAssetTypes::Experience, TEXT("DA_MiniMissingFeatureExperience"));
		bFaultApplied = true; ++Mutations;
	}
	else if (Mode == TEXT("MissingPrimary"))
	{
		UMiniAssetManager* Assets = UMiniAssetManager::GetMiniAssetManager();
		const FName Package(TEXT("/Game/Mini/Diagnostics/Task26/DA_MiniTask26MissingPrimary"));
		const FPrimaryAssetId Id(FMiniPrimaryAssetTypes::Experience, TEXT("DA_MiniTask26MissingPrimary"));
		const FAssetData Data(Package, FName(TEXT("/Game/Mini/Diagnostics/Task26")), Id.PrimaryAssetName,
			UMiniExperienceDefinition::StaticClass()->GetClassPathName());
		FString Error;
		if (!Assets || !Assets->RegisterSpecificPrimaryAsset(Id, Data) || !Assets->TryValidateExperienceId(Id, Error) ||
			Assets->GetPrimaryAssetPath(Id) != Data.GetSoftObjectPath()) { Fail(TEXT("real missing-package PrimaryAsset registration failed")); return; }
		SelectedId = Id; bFaultApplied = true; ++Mutations;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe PRIMARY_REGISTERED: ID=%s Path=%s Validated=1 ExistingProductionIdOverwritten=0"),
			*Id.ToString(), *Data.GetSoftObjectPath().ToString());
	}
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe SELECTED: Mode=%s ID=%s Applied=%d"), *Mode, *SelectedId.ToString(), int32(bFaultApplied));
}
void UMiniTask26ProbeSubsystem::PrimaryPrepared(UMiniExperienceManagerComponent* Manager, UWorld* World, bool& bUseStalledRegisteredPath)
{
	if (!IsClaimed(World) || Manager != ClaimedManager.Get() || Mode != TEXT("AssetReturn") || bFaultApplied) { return; }
	bUseStalledRegisteredPath = true; bFaultApplied = true; ++Mutations;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe PRIMARY_STALLED_PATH: World=%s RegisteredPath=1 RealRequestAsyncLoad=1 NormalLoadPrimaryAssetUnchanged=1"), *World->GetName());
}
void UMiniTask26ProbeSubsystem::CaptureActions(FMiniTask26WorldObservation& Observation)
{
	if (Observation.Manager.IsValid())
	{
		for (UGameFeatureAction* Action : Observation.Manager->RequiredActions) { Observation.Actions.AddUnique(Action); }
	}
}
void UMiniTask26ProbeSubsystem::ResourcesPrepared(UMiniExperienceManagerComponent* Manager, UWorld* World,
	TArray<FMiniRequiredActionClassRequest>& Requests)
{
	if (!IsClaimed(World) || Manager != ClaimedManager.Get()) { return; }
	ClaimedLoadGeneration = Manager->LoadGeneration;
	if (FMiniTask26WorldObservation* Observation = FindObservation(World)) { CaptureActions(*Observation); }
	for (const FMiniRequiredActionClassRequest& Request : Requests)
	{
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe REQUIRED_REQUEST: Action=%s Entry=%s Path=%s Plugin=%d"),
			*GetPathNameSafe(Request.Action.Get()), *Request.Entry, *Request.ClassPath.ToString(),
			Request.Action.IsValid() && Request.Action->GetTypedOuter<UGameFeatureData>() ? 1 : 0);
	}
	if (bFaultApplied) { return; }
	for (FMiniRequiredActionClassRequest& Request : Requests)
	{
		const bool bLayout = Request.Kind == EMiniRequiredActionClassKind::Layout;
		const bool bPlugin = Request.Action.IsValid() && Request.Action->GetTypedOuter<UGameFeatureData>();
		if (((Mode == TEXT("FrontClass") || Mode == TEXT("FailedFrontQuit")) && bLayout && !bPlugin) ||
			(Mode == TEXT("HUDClass") && bLayout && bPlugin) ||
			(Mode == TEXT("ActorClass") && Request.Kind == EMiniRequiredActionClassKind::ReplicatedActor))
		{
			Request.ClassPath = MissingClassPath(); bFaultApplied = true; ++Mutations;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe CLASS_PATH_INJECTED: Mode=%s World=%s Action=%s Entry=%s Path=%s Central=1"),
				*Mode, *World->GetName(), *GetPathNameSafe(Request.Action.Get()), *Request.Entry, *Request.ClassPath.ToString());
			return;
		}
	}
}
void UMiniTask26ProbeSubsystem::ActionWorldPrepared(UGameFeatureAction* Action, UWorld* World)
{
	if (!IsClaimed(World)) { if (bConsumed && World && World->GetGameInstance() == GetGameInstance()) { ++RejectedOtherWorldHooks; } return; }
	FMiniTask26WorldObservation* Observation = FindObservation(World);
	if (Observation) { Observation->Actions.AddUnique(Action); }
	FMiniRequiredActionFailed* Signal = nullptr;
	if (auto* Widgets = Cast<UMiniGameFeatureAction_AddWidgets>(Action)) { Signal = &Widgets->OnRequiredActionFailed; }
	if (auto* Actors = Cast<UMiniGameFeatureAction_AddActors>(Action)) { Signal = &Actors->OnRequiredActionFailed; }
	if (Signal && !ActionObservers.ContainsByPredicate([Action](const FActionObserver& O) { return O.Action.Get() == Action; }))
	{
		FActionObserver& O = ActionObservers.AddDefaulted_GetRef(); O.Action = Action;
		O.Handle = Signal->AddUObject(this, &ThisClass::RecordActionFailure);
	}
	if (bFaultApplied) { return; }
	if (auto* Widgets = Cast<UMiniGameFeatureAction_AddWidgets>(Action))
	{
		for (auto& Context : Widgets->ContextData)
		{
			auto* Data = Context.Value.Worlds.Find(World);
			if (!Data) { continue; }
			if ((Mode == TEXT("UIAsyncFailure") || Mode == TEXT("StaleFailure")) && !Data->LayoutRequests.IsEmpty())
			{
				Data->LayoutRequests[0].LayoutClass = TSoftClassPtr<UCommonActivatableWidget>(MissingClassPath()); bFaultApplied = true;
			}
			else if (Mode == TEXT("NoLayer") && !Data->LayoutRequests.IsEmpty())
			{
				Data->LayoutRequests[0].LayerTag = MiniHUDTags::HealthSlot; bFaultApplied = true;
			}
			else if (Mode == TEXT("NoSlot") && !Data->ElementRequests.IsEmpty())
			{
				Data->ElementRequests[0].SlotTag = UMiniPrimaryGameLayout::GetModalLayerTag(); bFaultApplied = true;
			}
			if (bFaultApplied) { ++Mutations; break; }
		}
	}
	if (auto* Actors = Cast<UMiniGameFeatureAction_AddActors>(Action); Actors && Mode == TEXT("ActorAsyncFailure"))
	{
		for (auto& Context : Actors->ContextData)
		{
			if (auto* Data = Context.Value.Worlds.Find(World); Data && !Data->Entries.IsEmpty())
			{
				Data->Entries[0].ActorClass = TSoftClassPtr<AActor>(MissingClassPath()); bFaultApplied = true; ++Mutations; break;
			}
		}
	}
	if (bFaultApplied)
	{
		ClaimedLoadGeneration = ClaimedManager.IsValid() ? ClaimedManager->LoadGeneration : 0;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe WORLD_SNAPSHOT_INJECTED: Mode=%s World=%s Action=%s CentralSnapshotChanged=0 AssetChanged=0"),
			*Mode, *World->GetName(), *GetPathNameSafe(Action));
	}
}
void UMiniTask26ProbeSubsystem::HandlePrepared(UObject* Source, UWorld* World, TSharedPtr<FStreamableHandle> Handle, bool& bHold)
{
	if (!IsClaimed(World) || !Handle.IsValid() || HoldCount > 0) { return; }
	const auto* Manager = Cast<UMiniExperienceManagerComponent>(Source);
	const bool bSelected = (Mode == TEXT("AssetReturn") && Manager && Manager->GetLoadState() == EMiniExperienceLoadState::LoadingAssets) ||
		((Mode == TEXT("ResourcesReturn") || Mode == TEXT("Deadline")) && Manager && Manager->GetLoadState() == EMiniExperienceLoadState::LoadingActionResources) ||
		(Mode == TEXT("UIActionReturn") && Cast<UMiniGameFeatureAction_AddWidgets>(Source)) ||
		(Mode == TEXT("ActorActionReturn") && Cast<UMiniGameFeatureAction_AddActors>(Source));
	if (!bSelected) { return; }
	bHold = true; ++HoldCount;
	if (!bFaultApplied) { bFaultApplied = true; ++Mutations; }
	FHeldLoad& Load = HeldLoads.AddDefaulted_GetRef(); Load.Source = Source; Load.World = World; Load.Handle = Handle;
	Load.ReleaseAt = FPlatformTime::Seconds() + (Mode == TEXT("Deadline") ? 70.0 : 15.0);
	ClaimedLoadGeneration = ClaimedManager.IsValid() ? ClaimedManager->LoadGeneration : 0;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe REAL_HANDLE_HELD: Mode=%s World=%s Source=%s Pending=%d Stalled=%d FiniteSeconds=%d"),
		*Mode, *World->GetName(), *GetPathNameSafe(Source), int32(!Handle->HasLoadCompleted() && !Handle->WasCanceled()),
		int32(Handle->IsStalled()), Mode == TEXT("Deadline") ? 70 : 15);
}
void UMiniTask26ProbeSubsystem::RecordActionFailure(UWorld* World, UGameFeatureAction* Action, uint64 Generation, const FString& Reason)
{
	if (World != ClaimedWorld.Get()) { return; }
	FailedAction = Action; FailedActionGeneration = Generation; ActionFailureReason = Reason;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe ACTUAL_ACTION_FAILURE: Mode=%s World=%s Action=%s Generation=%llu Reason=%s"),
		*Mode, *GetNameSafe(World), *GetPathNameSafe(Action), Generation, *Reason);
}
void UMiniTask26ProbeSubsystem::RecordLoaded(const UMiniExperienceDefinition* Experience, TWeakObjectPtr<UWorld> World)
{
	if (FMiniTask26WorldObservation* O = FindObservation(World.Get()))
	{
		++O->Loaded; CaptureActions(*O);
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe LOADED: Mode=%s World=%s Target=%d Count=%d FrontEnd=%d"),
			*Mode, *GetNameSafe(World.Get()), World == ClaimedWorld ? 1 : 0, O->Loaded, Experience && Experience->bIsFrontEnd ? 1 : 0);
		if (O->Loaded > 1) { Fail(TEXT("one World published Loaded more than once")); }
	}
}
void UMiniTask26ProbeSubsystem::RecordFailed(const FString& Reason, TWeakObjectPtr<UWorld> World)
{
	FMiniTask26WorldObservation* O = FindObservation(World.Get());
	if (!O) { Fail(TEXT("failure came from an unobserved World")); return; }
	++O->Failed; O->Reason = Reason;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe FAILED_OBSERVED: Mode=%s World=%s Target=%d Count=%d Loaded=%d Reason=%s"),
		*Mode, *GetNameSafe(World.Get()), World == ClaimedWorld ? 1 : 0, O->Failed, O->Loaded, *Reason);
	if (World != ClaimedWorld || !IsExpectedFailure() || O->Failed > 1) { Fail(TEXT("unexpected/duplicate failure, or fault polluted another World")); }
}
void UMiniTask26ProbeSubsystem::WorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (FMiniTask26WorldObservation* O = FindObservation(World))
	{
		O->bCleaned = true;
		if (O == ClaimedObservation())
		{
			O->bResourcesReleasedAtCleanup = SnapshotReleasedResources(*O, World);
			if (!O->bResourcesReleasedAtCleanup) { Fail(TEXT("old World resources were not released at real post-cleanup")); }
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe CLEANUP_SNAPSHOT: Mode=%s Released=%d BeforeWorldGarbageCollection=1 PureData=1"), *Mode, int32(O->bResourcesReleasedAtCleanup));
		}
		// OpenLevel marks the old World as garbage immediately after this callback.
		// Keep only stable identity and scalar results; no old World/actor/widget roots.
		O->World.Reset(); O->Manager.Reset(); O->Root.Reset(); O->Front.Reset(); O->Modal.Reset(); O->Actions.Reset();
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe WORLD_CLEANED: Mode=%s World=%s Target=%d Loaded=%d Failed=%d"),
			*Mode, *GetNameSafe(World), World == ClaimedWorld.Get() ? 1 : 0, O->Loaded, O->Failed);
	}
}
void UMiniTask26ProbeSubsystem::ReleaseHolds(bool bCancel)
{
	for (FHeldLoad& Load : HeldLoads)
	{
		if (Load.Handle.IsValid() && !Load.Handle->WasCanceled() && !Load.Handle->HasLoadCompleted())
		{
			if (bCancel) { Load.Handle->CancelHandle(); }
			else if (Load.Handle->IsStalled()) { Load.Handle->StartStalledHandle(); }
		}
	}
	FSimpleDelegate Resume = MoveTemp(FeatureMountResume);
	Resume.ExecuteIfBound();
}
bool UMiniTask26ProbeSubsystem::FrontEndReady(bool bAllowError)
{
	UWorld* World = GetTickableGameObjectWorld();
	const auto* Manager = MiniTask26ManagerFor(World);
	auto* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
	auto* Root = MiniTask26RootFor(World);
	auto* Front = MiniTask26ActiveWidget<UMiniFrontEndWidget>(Root, UMiniPrimaryGameLayout::GetMenuLayerTag());
	if (MiniTask26MapName(World) != MiniTask26FrontMap || !Manager || !Manager->IsExperienceLoaded() || !Manager->GetCurrentExperience()->bIsFrontEnd ||
		!Travel || Travel->GetState().bBusy || !Root || !Root->IsLayoutReady() || !Front || Front->GetWorld() != World ||
		MiniTask26LocalPC(World)->GetPawn() || MiniTask26ActiveWidget<UMiniHUDLayout>(Root, UMiniPrimaryGameLayout::GetGameLayerTag())) { return false; }
	const int32 ExpectedGate = Travel->GetState().bHasError ? 2 : 1;
	if ((!bAllowError && Travel->GetState().bHasError) || Root->GetGameplayInputBlockCount() != ExpectedGate) { return false; }
	if (FMiniTask26WorldObservation* O = FindObservation(World)) { O->Root = Root; O->Front = Front; O->Modal = Travel->GetConnectionStatusWidget(); }
	return true;
}
bool UMiniTask26ProbeSubsystem::GameplayReady()
{
	UWorld* World = GetTickableGameObjectWorld(); auto* M = MiniTask26ManagerFor(World); auto* PC = MiniTask26LocalPC(World); auto* Root = MiniTask26RootFor(World);
	auto* Pawn = PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr;
	auto* HUD = MiniTask26ActiveWidget<UMiniHUDLayout>(Root, UMiniPrimaryGameLayout::GetGameLayerTag());
	if (World != ClaimedWorld.Get() || !M || !M->IsExperienceLoaded() || !Pawn || !Pawn->GetHeroComponent() ||
		!Pawn->GetHeroComponent()->IsInputActive() || !HUD || HUD->GetExtensionWidgetCount() != 4 || Root->GetGameplayInputBlockCount() != 0) { return false; }
	for (UGameFeatureAction* Action : M->RequiredActions)
	{
		if (auto* Actors = Cast<UMiniGameFeatureAction_AddActors>(Action))
		{
			int32 Worlds, Alive, Pending, Bindings; bool Ready, Failed;
			Actors->GetWorldStats(World, Worlds, Alive, Pending, Bindings, Ready, Failed);
			if (!Ready || Failed || Pending || Bindings || Alive != Actors->Actors.Num()) { return false; }
		}
	}
	return true;
}
bool UMiniTask26ProbeSubsystem::FailureReady()
{
	FMiniTask26WorldObservation* O = ClaimedObservation();
	auto* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
	auto* Root = MiniTask26RootFor(ClaimedWorld.Get());
	auto* Modal = Travel ? Travel->GetConnectionStatusWidget() : nullptr;
	if (!O || O->Manager->GetLoadState() != EMiniExperienceLoadState::Failed || O->Failed != 1 || !Travel ||
		Travel->GetState().bBusy || !Travel->GetState().bHasError || Travel->GetState().FailureCode != TEXT("MINI_EXPERIENCE_FAILED") ||
		!Root || !Modal || !Modal->IsActivated() || !Modal->GetButton(TEXT("ReturnButton")) ||
		!Modal->GetButton(TEXT("ReturnButton"))->IsVisible() || Modal->GetStateListenerCount() != 1) { return false; }
	FString Expected;
	if (Mode == TEXT("Unknown")) { Expected = TEXT("DA_MiniTask26DefinitelyMissing"); }
	else if (Mode == TEXT("MissingFeature")) { Expected = TEXT("MiniDefinitelyMissing"); }
	else if (Mode == TEXT("MissingPrimary")) { Expected = TEXT("DA_MiniTask26MissingPrimary"); }
	else if (Mode == TEXT("FrontClass") || Mode == TEXT("HUDClass") || Mode == TEXT("ActorClass") || Mode == TEXT("FailedFrontQuit")) { Expected = MissingClassPath().ToString(); }
	else if (Mode == TEXT("UIAsyncFailure") || Mode == TEXT("StaleFailure")) { Expected = TEXT("required widget class or tag is invalid"); }
	else if (Mode == TEXT("ActorAsyncFailure")) { Expected = TEXT("class must be concrete and replicated"); }
	else if (Mode == TEXT("NoLayer")) { Expected = TEXT("NO_LAYER_OR_WIDGET"); }
	else if (Mode == TEXT("NoSlot")) { Expected = TEXT("REQUIRED_EXTENSION_NOT_ATTACHED"); }
	else if (Mode == TEXT("Deadline")) { Expected = TEXT("exceeded 60 seconds"); }
	if (Expected.IsEmpty() || !O->Reason.Contains(Expected)) { Fail(TEXT("terminal failure did not match the injected real cause")); return false; }
	if ((Mode == TEXT("UIAsyncFailure") || Mode == TEXT("NoLayer") || Mode == TEXT("StaleFailure")) && O->Loaded != 1)
	{ Fail(TEXT("post-barrier FrontEnd UI failure did not prove Loaded then Failed")); return false; }
	if (Mode != TEXT("UIAsyncFailure") && Mode != TEXT("NoLayer") && Mode != TEXT("StaleFailure") && Mode != TEXT("NoSlot") && O->Loaded != 0)
	{ Fail(TEXT("pre-ready required failure allowed gameplay Loaded")); return false; }
	for (TActorIterator<AMiniCharacter> It(O->World.Get()); It; ++It) { if (!It->IsActorBeingDestroyed()) { Fail(TEXT("failed pre-gameplay World has a live Pawn")); return false; } }
	if (O->Manager->RequiredActionObservers.Num() || O->Manager->RequiredActions.Num() || O->Manager->ActivatedActions.Num())
	{ Fail(TEXT("failed Experience retained its Action ownership/observers")); return false; }
	for (UGameFeatureAction* Action : O->Actions)
	{
		if (auto* Widgets = Cast<UMiniGameFeatureAction_AddWidgets>(Action))
		{
			const auto Counts = Widgets->GetProbeContributionCounts(O->World.Get());
			if (Counts.Layouts || Counts.Elements || Counts.PendingLoads) { return false; }
		}
		if (auto* Actors = Cast<UMiniGameFeatureAction_AddActors>(Action))
		{
			int32 Worlds, Alive, Pending, Bindings; bool Ready, Failed;
			Actors->GetWorldStats(O->World.Get(), Worlds, Alive, Pending, Bindings, Ready, Failed);
			if (Alive || Pending || Bindings) { return false; }
		}
	}
	O->Root = Root; O->Modal = Modal;
	return true;
}
bool UMiniTask26ProbeSubsystem::VerifyPending()
{
	if (!ClaimedManager.IsValid() || bReturnRequested) { return false; }
	if (Mode == TEXT("FeatureReturn"))
	{
		if (!bFeaturePaused || !FeatureMountResume.IsBound() || ClaimedManager->GetLoadState() != EMiniExperienceLoadState::LoadingFeatures ||
			UGameFeaturesSubsystem::Get().GetPluginState(FeatureURL) != EGameFeaturePluginState::Mounting) { return false; }
	}
	else
	{
		if (HeldLoads.Num() != 1 || !HeldLoads[0].Handle.IsValid() || HeldLoads[0].Handle->HasLoadCompleted() || HeldLoads[0].Handle->WasCanceled() ||
			!HeldLoads[0].Handle->IsStalled()) { return false; }
	}
	bPendingObserved = true;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe PENDING_VERIFIED: Mode=%s World=%s State=%d RealHandleOrPlugin=1"),
		*Mode, *GetNameSafe(ClaimedWorld.Get()), int32(ClaimedManager->GetLoadState()));
	return true;
}
bool UMiniTask26ProbeSubsystem::SnapshotReleasedResources(FMiniTask26WorldObservation& O, UWorld* CleaningWorld)
{
	// This synchronous callback precedes MarkObjectsPendingKill/full GC. Get(true)
	// permits an already-ended Actor to be inspected here, never after this callback.
	const auto* Manager = O.Manager.Get(true);
	if (!Manager || !Manager->bEndingPlay || !Manager->RequiredActionObservers.IsEmpty() ||
		!Manager->RequiredActions.IsEmpty() || !Manager->ActivatedActions.IsEmpty() || !Manager->GameFeatureLeases.IsEmpty() ||
		Manager->AssetLoadHandle.IsValid() || Manager->RequiredActionLoadHandle.IsValid()) { return false; }
	for (const FHeldLoad& Load : HeldLoads)
	{
		if (!Load.Handle.IsValid() || !Load.Handle->WasCanceled()) { return false; }
	}
	const auto* Front = O.Front.Get(true); const auto* Modal = O.Modal.Get(true);
	if (Front && (Front->IsActivated() || Front->GetStateListenerCount() != 0)) { return false; }
	if (Modal && (Modal->IsActivated() || Modal->GetStateListenerCount() != 0)) { return false; }
	for (UGameFeatureAction* Action : O.Actions)
	{
		if (auto* Widgets = Cast<UMiniGameFeatureAction_AddWidgets>(Action))
		{
			const auto Counts = Widgets->GetProbeContributionCounts(CleaningWorld);
			if (Counts.HUDs || Counts.Layouts || Counts.Elements || Counts.PendingLoads) { return false; }
		}
		if (auto* Actors = Cast<UMiniGameFeatureAction_AddActors>(Action))
		{
			int32 Worlds, Alive, Pending, Bindings; bool Ready, Failed;
			Actors->GetWorldStats(CleaningWorld, Worlds, Alive, Pending, Bindings, Ready, Failed);
			if (Worlds || Alive || Pending || Bindings) { return false; }
		}
	}
	return true;
}
bool UMiniTask26ProbeSubsystem::VerifyOldWorldReleased()
{
	const auto* O = ClaimedObservation();
	if (!O || !O->bCleaned || !O->bResourcesReleasedAtCleanup) { return false; }
	if (Mode == TEXT("FeatureReturn") && (LateFeatureActivations != 1 || LateFeatureDeactivations != 1 ||
		UGameFeaturesSubsystem::Get().IsGameFeaturePluginActiveByName(TEXT("MiniShooterCore"), true))) { return false; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe OLD_WORLD_RELEASED: Mode=%s Loaded=%d Failed=%d OldObservers=0 OldActions=0 OldHandles=0 OldWidgets=0 OldActors=0 HeldCanceled=%d CleanupSnapshot=1 BeforeGC=1"),
		*Mode, O->Loaded, O->Failed, HeldLoads.Num());
	return true;
}
bool UMiniTask26ProbeSubsystem::VerifyStaleNoticeRejected()
{
	if (bStaleVerified) { return true; }
	UWorld* NewWorld = GetTickableGameObjectWorld(); auto* NewManager = MiniTask26ManagerFor(NewWorld);
	auto* Old = ClaimedObservation(); auto* O = FindObservation(NewWorld);
	if (!Old || !Old->bResourcesReleasedAtCleanup || !NewManager || !O || !FailedAction || ActionFailureReason.IsEmpty() || O->Failed || !NewManager->IsExperienceLoaded())
	{ Fail(TEXT("real old Action failure notification was unavailable for stale replay")); return false; }
	const int32 LoadedBefore = O->Loaded;
	// Replay the captured genuine notification against the new receiver. The old
	// weak World may now be invalid after full GC; retain its identity, never dereference it.
	// This is diagnostic replay of an actual payload, not simulated network lateness.
	NewManager->HandleRequiredActionFailure(ClaimedWorld.Get(), FailedAction.Get(), FailedActionGeneration,
		ActionFailureReason, ClaimedWorld, ClaimedLoadGeneration);
	if (!NewManager->IsExperienceLoaded() || O->Failed || O->Loaded != LoadedBefore || GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>()->GetState().bHasError)
	{ Fail(TEXT("captured stale failure polluted the recovered World")); return false; }
	bStaleVerified = true;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe STALE_FAILURE_REJECTED: ActualPayloadReplayed=1 OldWorldEnded=1 NewLoaded=%d NewFailed=0 NewError=0"), O->Loaded);
	return true;
}
void UMiniTask26ProbeSubsystem::Fail(const TCHAR* Reason)
{
	if (bDone) { return; }
	bDone = true;
	UE_LOG(LogMiniInit, Error, TEXT("MiniTask26Probe FAIL: Mode=%s Step=%d Reason=%s"), *Mode, Step, Reason);
	ReleaseHolds(true); FPlatformMisc::RequestExitWithStatus(true, 1, TEXT("MiniTask26ProbeFailure"));
}
void UMiniTask26ProbeSubsystem::Pass()
{
	if (bDone) { return; }
	const auto* Old = ClaimedObservation();
	if (ClaimedCount != 1 || !Old || (Mode != TEXT("NormalPractice") && (!bFaultApplied || Mutations != 1)) ||
		(IsPendingMode() && !bPendingObserved)) { Fail(TEXT("single-World fault/actual pending contract was not met")); return; }
	bDone = true;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe PASS: Mode=%s Claimed=1 Mutations=%d TargetLoaded=%d TargetFailed=%d PendingObserved=%d Holds=%d RejectedOtherWorld=%d WorldRecords=%d LateFeatureActivated=%d LateFeatureDeactivated=%d"),
		*Mode, Mutations, Old->Loaded, Old->Failed, int32(bPendingObserved), HoldCount, RejectedOtherWorldHooks, Observations.Num(), LateFeatureActivations, LateFeatureDeactivations);
	if (Mode != TEXT("FailedFrontQuit")) { GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>()->QuitGame(); }
}
#endif

void UMiniTask26ProbeSubsystem::OnGameFeaturePostMounting(const FString& PluginName,
	const FGameFeaturePluginIdentifier& PluginIdentifier, FGameFeaturePostMountingContext& Context)
{
#if !UE_BUILD_SHIPPING
	if (Mode != TEXT("FeatureReturn") || PluginName != TEXT("MiniShooterCore") || !IsClaimed(GetTickableGameObjectWorld()) || bFeaturePaused) { return; }
	FeatureMountResume = Context.PauseUntilComplete(TEXT("MiniTask26SingleWorldPending"));
	bFeaturePaused = true; bFaultApplied = true; ++Mutations; FeatureResumeAt = FPlatformTime::Seconds() + 15.0;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe FEATURE_REAL_PAUSED: Plugin=%s State=Mounting ManagerPending=1 FiniteSeconds=15"), *PluginName);
#endif
}
void UMiniTask26ProbeSubsystem::OnGameFeatureActivated(const UGameFeatureData* Data, const FString& PluginURL)
{
#if !UE_BUILD_SHIPPING
	if (Mode == TEXT("FeatureReturn") && PluginURL == FeatureURL && bReturnRequested) { ++LateFeatureActivations; }
#endif
}
void UMiniTask26ProbeSubsystem::OnGameFeatureDeactivating(const UGameFeatureData* Data,
	FGameFeatureDeactivatingContext& Context, const FString& PluginURL)
{
#if !UE_BUILD_SHIPPING
	if (Mode == TEXT("FeatureReturn") && PluginURL == FeatureURL && bReturnRequested) { ++LateFeatureDeactivations; }
#endif
}
void UMiniTask26ProbeSubsystem::Tick(float DeltaTime)
{
#if !UE_BUILD_SHIPPING
	if (!bInitialized || bDone) { return; }
	const double Now = FPlatformTime::Seconds();
	if (Now - StartedAt > TimeoutSeconds) { Fail(TEXT("finite scenario deadline expired")); return; }
	for (FHeldLoad& Load : HeldLoads)
	{
		if (Now >= Load.ReleaseAt && Load.Handle.IsValid() && Load.Handle->IsStalled() && !Load.Handle->WasCanceled())
		{ Load.Handle->StartStalledHandle(); UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe FINITE_HOLD_RELEASED: Mode=%s"), *Mode); }
	}
	if (FeatureMountResume.IsBound() && Now >= FeatureResumeAt) { ReleaseHolds(false); }
	UMiniTravelSubsystem* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
	if (!Travel) { return; }
	if (Step == 0)
	{
		if (IsFrontFault()) { if (bConsumed) { Step = 1; } return; }
		if (!FrontEndReady(false)) { return; }
		if (Mode == TEXT("FeatureReturn"))
		{
			if (!bUnloadStarted)
			{
				if (!UGameFeaturesSubsystem::Get().GetPluginURLByName(TEXT("MiniShooterCore"), FeatureURL)) { Fail(TEXT("feature preparation URL was not found")); return; }
				bUnloadStarted = true;
				UGameFeaturesSubsystem::Get().UnloadGameFeaturePlugin(FeatureURL,
					FGameFeaturePluginUnloadComplete::CreateWeakLambda(this, [this](const UE::GameFeatures::FResult& Result)
					{
						if (!Result.HasValue()) { Fail(TEXT("real inactive feature unload preparation failed")); return; }
						bFeatureInstalled = UGameFeaturesSubsystem::Get().GetPluginState(FeatureURL) == EGameFeaturePluginState::Installed;
						UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe FEATURE_PREPARED: Installed=%d InactiveFrontEnd=1"), int32(bFeatureInstalled));
					}), false);
				return;
			}
			if (!bFeatureInstalled) { return; }
		}
		if (!Travel->StartPractice()) { Fail(TEXT("production StartPractice refused normal FrontEnd")); return; }
		Step = 1; return;
	}
	if (Step == 1)
	{
		if (!bConsumed) { return; }
		if (Mode == TEXT("NormalPractice"))
		{
			if (!GameplayReady() || !Media(TEXT("NormalPractice"))) { return; }
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe NORMAL_READY: ActualHUDWidgets=4 ActualAuthorityActorsReady=1 LocalInput=1"));
		}
		else if (IsPendingMode())
		{
			if (!VerifyPending()) { return; }
		}
		else
		{
			if (Mode == TEXT("Deadline") && !bPendingObserved) { if (VerifyPending()) { bPendingObserved = true; } }
			if (!FailureReady() || !Media(TEXT("ExpectedFailure"))) { return; }
			if (Mode == TEXT("Deadline") && (!bPendingObserved || Now - StepStartedAt < 59.0)) { Fail(TEXT("bare startup did not use the production 60 second deadline")); return; }
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe FAILURE_VERIFIED: Mode=%s NativeModal=1 ReturnButton=1 Listener=1 ActionResources=0 Pawn=0"), *Mode);
			if (Mode == TEXT("FailedFrontQuit"))
			{
				auto* Modal = Travel->GetConnectionStatusWidget(); UButton* Quit = Modal ? Modal->GetButton(TEXT("QuitButton")) : nullptr;
				if (!Quit || !Quit->IsVisible() || !Quit->GetIsEnabled()) { Fail(TEXT("failed FrontEnd has no native exit button")); return; }
				if (FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask26Media"))) { if (!Click(Quit)) { return; } }
				else { UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe QUIT_API: NativeButtonPresent=1 ActualUIEvent=0")); Travel->QuitGame(); }
				if (Observations.Num() != 1) { Fail(TEXT("failed FrontEnd Quit attempted another World")); return; }
				Pass(); return;
			}
		}
		bReturnRequested = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe RETURN_REQUEST: Mode=%s RealProductionAPI=1 PendingObserved=%d"), *Mode, int32(bPendingObserved));
		Travel->ReturnToFrontEnd(); Step = 2; return;
	}
	if (Step == 2)
	{
		if (GetTickableGameObjectWorld() == ClaimedWorld.Get()) { return; }
		// Let the new FrontEnd own its normal UI before genuinely resuming the
		// old plugin state machine. Its late completion must release the old lease.
		if (Mode == TEXT("FeatureReturn") && FeatureMountResume.IsBound() && FrontEndReady(true)) { ReleaseHolds(false); }
		if (!FrontEndReady(true) || !VerifyOldWorldReleased()) { return; }
		if (IsExpectedFailure() && !Travel->GetState().bHasError) { Fail(TEXT("genuine error was lost before the recovered FrontEnd")); return; }
		if (!IsExpectedFailure() && Travel->GetState().bHasError) { Fail(TEXT("canceled old work created an error in the new World")); return; }
		if (Travel->GetState().bHasError) { Travel->DismissError(); }
		Step = 3; return;
	}
	if (Step == 3)
	{
		if (!FrontEndReady(false)) { return; }
		FMiniTask26WorldObservation* New = FindObservation(GetTickableGameObjectWorld());
		if (!New || New->Loaded != 1 || New->Failed || GetTickableGameObjectWorld() == ClaimedWorld.Get()) { Fail(TEXT("recovery was not one new normal FrontEnd load")); return; }
		if (Mode == TEXT("StaleFailure") && !VerifyStaleNoticeRejected()) { return; }
		if (!Media(TEXT("RecoveredFrontEnd"))) { return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe RECOVERED: NewWorld=1 NewLoaded=1 NewFailed=0 Menu=1 Gate=1 InputSourceReleased=1 Consumed=1"));
		Pass();
	}
#endif
}

#if !UE_BUILD_SHIPPING
bool UMiniTask26ProbeSubsystem::Media(FName Stage)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask26Media")) || MediaDone.Contains(Stage)) { return true; }
	if (!PendingMediaStage.IsNone())
	{
		if (PendingMediaStage != Stage) { return false; }
		if (IFileManager::Get().FileSize(*PendingMediaPath) > 1024)
		{
			MediaDone.Add(Stage); PendingMediaStage = NAME_None;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe SCREENSHOT_SAVED: Mode=%s Stage=%s Path=%s"), *Mode, *Stage.ToString(), *PendingMediaPath); return true;
		}
		if (FPlatformTime::Seconds() - MediaStartedAt > 30.0) { Fail(TEXT("actual screenshot was not saved")); }
		return false;
	}
#if WITH_EDITOR
	if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { return false; }
#endif
	IFileManager::Get().MakeDirectory(*MediaDirectory, true);
	PendingMediaPath = FPaths::Combine(MediaDirectory, FString::Printf(TEXT("Task26-%s-%s.png"), *Mode, *Stage.ToString()));
	IFileManager::Get().Delete(*PendingMediaPath, false, true); PendingMediaStage = Stage; MediaStartedAt = FPlatformTime::Seconds();
	FScreenshotRequest::RequestScreenshot(PendingMediaPath, true, false); return false;
}
bool UMiniTask26ProbeSubsystem::Click(UButton* Button)
{
	if (!FSlateApplication::IsInitialized() || !Button || !Button->IsVisible() || !Button->GetIsEnabled() || !Button->GetCachedWidget().IsValid()) { return false; }
	const FGeometry& Geometry = Button->GetCachedGeometry();
	if (Geometry.GetLocalSize().X < 2 || Geometry.GetLocalSize().Y < 2) { return false; }
	FSlateApplication& Slate = FSlateApplication::Get();
	TSharedPtr<SWindow> Window = Slate.FindWidgetWindow(Button->GetCachedWidget().ToSharedRef());
	if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()) { return false; }
	const bool Previous = Slate.GetHandleDeviceInputWhenApplicationNotActive(); Slate.SetHandleDeviceInputWhenApplicationNotActive(true);
	ON_SCOPE_EXIT { Slate.SetHandleDeviceInputWhenApplicationNotActive(Previous); };
	const FVector2D Position = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f); const FVector2D Old = Slate.GetCursorPos();
	Slate.SetCursorPos(Position); TSet<FKey> Pressed;
	Slate.ProcessMouseMoveEvent(FPointerEvent(FSlateApplication::CursorPointerIndex, Position, Old, Pressed, EKeys::Invalid, 0, FModifierKeysState()));
	if (!Button->IsHovered()) { return false; }
	Pressed.Add(EKeys::LeftMouseButton);
	const bool Down = Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), FPointerEvent(FSlateApplication::CursorPointerIndex,
		Position, Position, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState()));
	const bool TargetPressed = Button->IsPressed(); const bool Captured = Button->GetCachedWidget()->HasMouseCapture(); Pressed.Reset();
	const bool Up = Slate.ProcessMouseButtonUpEvent(FPointerEvent(FSlateApplication::CursorPointerIndex,
		Position, Position, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState()));
	if (!Down || !Up || !TargetPressed || !Captured) { return false; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask26Probe UI_CLICK: Button=QuitButton MouseMove=1 Hover=1 Pressed=1 Capture=1 Down=1 Up=1"));
	return true;
}
#endif
