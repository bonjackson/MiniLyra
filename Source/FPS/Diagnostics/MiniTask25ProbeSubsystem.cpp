#include "MiniTask25ProbeSubsystem.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniAbilitySet.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Arena/MiniArenaRulesComponent.h"
#include "Arena/MiniMatchRulesComponent.h"
#include "Arena/MiniMatchSubsystem.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniHeroComponent.h"
#include "Components/BoxComponent.h"
#include "Components/Button.h"
#include "Components/SphereComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/MiniEquipmentDefinition.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Equipment/MiniQuickBarComponent.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFeatureData.h"
#include "GameFeaturesSubsystem.h"
#include "GameFeatures/MiniGameFeatureAction_AddActors.h"
#include "GameFeatures/MiniGameFeatureAction_AddWidgets.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/WorldSettings.h"
#include "GameModes/MiniExperienceActionSet.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGameMode.h"
#include "GameModes/MiniGamePhaseAbility.h"
#include "GameModes/MiniGamePhaseSubsystem.h"
#include "GameModes/MiniGameState.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Inventory/MiniInventoryManagerComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "Practice/MiniPracticeSupply.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"
#include "System/MiniTravelSubsystem.h"
#include "Training/MiniPracticeTarget.h"
#include "UI/MiniConnectionStatusWidget.h"
#include "UI/MiniFrontEndWidget.h"
#include "UI/MiniHUDLayout.h"
#include "UI/MiniHUDViewModel.h"
#include "UI/MiniHUDWidgets.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "UnrealClient.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#include "Widgets/SWindow.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
AMiniPlayerController* Task25LocalPC(UWorld* World)
{
	if (World) { for (TActorIterator<AMiniPlayerController> It(World); It; ++It) { if (It->IsLocalController()) { return *It; } } }
	return nullptr;
}
AMiniPlayerState* Task25PS(AMiniPlayerController* PC) { return PC ? PC->GetPlayerState<AMiniPlayerState>() : nullptr; }
AMiniCharacter* Task25Pawn(AMiniPlayerController* PC) { return PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr; }
UMiniAbilitySystemComponent* Task25ASC(AMiniPlayerController* PC) { return Task25PS(PC) ? Task25PS(PC)->GetMiniAbilitySystemComponent() : nullptr; }
UMiniEquipmentInstance* Task25Equipment(AMiniPlayerController* PC)
{
	return Task25Pawn(PC) && Task25Pawn(PC)->GetEquipmentManager() ? Task25Pawn(PC)->GetEquipmentManager()->GetCurrentEquipment() : nullptr;
}
UMiniPrimaryGameLayout* Task25Root(AMiniPlayerController* PC)
{
	return PC && PC->GetLocalPlayer() ? Cast<UMiniPrimaryGameLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(PC->GetLocalPlayer())) : nullptr;
}
template<typename T> T* Task25LayerWidget(UMiniPrimaryGameLayout* Layout, FGameplayTag Tag)
{
	UCommonActivatableWidgetContainerBase* Layer = Layout ? Layout->GetLayerWidget(Tag) : nullptr;
	T* Result = nullptr;
	if (Layer) { for (UCommonActivatableWidget* W : Layer->GetWidgetList())
	{
		if (T* Candidate = Cast<T>(W); Candidate && Candidate->IsActivated()) { if (Result) { return nullptr; } Result = Candidate; }
	} }
	return Result;
}
UMiniHUDLayout* Task25HUD(AMiniPlayerController* PC) { return Task25LayerWidget<UMiniHUDLayout>(Task25Root(PC), UMiniPrimaryGameLayout::GetGameLayerTag()); }
UMiniFrontEndWidget* Task25Front(AMiniPlayerController* PC) { return Task25LayerWidget<UMiniFrontEndWidget>(Task25Root(PC), UMiniPrimaryGameLayout::GetMenuLayerTag()); }
UMiniExperienceManagerComponent* Task25Manager(UWorld* World)
{
	AMiniGameState* GS = World ? World->GetGameState<AMiniGameState>() : nullptr;
	return GS ? GS->GetExperienceManagerComponent() : nullptr;
}
FMiniGamePhaseState Task25Phase(UWorld* World) { return World->GetSubsystem<UMiniGamePhaseSubsystem>()->GetCurrentPhaseState(); }
FMiniMatchState Task25Match(UWorld* World) { return World->GetSubsystem<UMiniMatchSubsystem>()->GetCurrentMatchState(); }
int32 Task25SourceSpecs(UMiniAbilitySystemComponent* AbilitySystem, const UObject* Source)
{
	int32 Count = 0;
	if (AbilitySystem && Source) { for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities()) { Count += Spec.SourceObject.Get() == Source ? 1 : 0; } }
	return Count;
}
int32 Task25ActiveSpecs(UMiniAbilitySystemComponent* AbilitySystem)
{
	int32 Count = 0;
	if (AbilitySystem) { for (const FGameplayAbilitySpec& Spec : AbilitySystem->GetActivatableAbilities()) { Count += Spec.IsActive() ? 1 : 0; } }
	return Count;
}
int32 Task25ExpectedEquipmentSpecs(UMiniEquipmentInstance* Instance)
{
	const UMiniEquipmentDefinition* Definition = Instance && Instance->GetEquipmentDefinition() ? Instance->GetEquipmentDefinition()->GetDefaultObject<UMiniEquipmentDefinition>() : nullptr;
	const UMiniAbilitySet* Set = Definition ? Definition->GetAbilitySet() : nullptr;
	return Set ? Set->Abilities.Num() : -1;
}
int32 Task25SavedMoves(AMiniCharacter* Character)
{
	UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	// Calling GetPredictionData before Has would allocate the object being measured.
	return Movement && Movement->HasPredictionData_Client() ? Movement->GetPredictionData_Client_Character()->SavedMoves.Num() : 0;
}
bool Task25Live(AMiniPlayerController* PC)
{
	return Task25PS(PC) && Task25Pawn(PC) && Task25ASC(PC) && Task25ASC(PC)->GetOwnerActor() == Task25PS(PC) && Task25ASC(PC)->GetAvatarActor() == Task25Pawn(PC) &&
		Task25Pawn(PC)->GetHealthComponent() && !Task25Pawn(PC)->GetHealthComponent()->IsDead() && Task25Equipment(PC) &&
		Task25Pawn(PC)->GetHeroComponent() && (!PC->IsLocalController() || Task25Pawn(PC)->GetHeroComponent()->IsInputActive());
}
bool Task25Fresh(AMiniPlayerController* PC)
{
	UMiniQuickBarComponent* Bar = PC ? PC->GetQuickBar() : nullptr;
	UMiniInventoryItemInstance* Rifle = Bar ? Bar->GetSlotItem(0) : nullptr;
	UMiniInventoryItemInstance* Pistol = Bar ? Bar->GetSlotItem(1) : nullptr;
	return Bar && PC->GetInventoryManager() && PC->GetInventoryManager()->GetEntries().Num() == 2 && Bar->GetActiveSlotIndex() == 0 &&
		Rifle && Pistol && Rifle->GetInstanceId().IsValid() && Pistol->GetInstanceId().IsValid() && Rifle->GetInstanceId() != Pistol->GetInstanceId() &&
		Rifle->GetStat(MiniInventoryTags::AmmoInMagazine) == 30 && Rifle->GetStat(MiniInventoryTags::ReserveAmmo) == 90 &&
		Pistol->GetStat(MiniInventoryTags::AmmoInMagazine) == 12 && Pistol->GetStat(MiniInventoryTags::ReserveAmmo) == 36;
}
void Task25Key(AMiniPlayerController* PC, FKey Value, EInputEvent Event)
{
	if (PC) { PC->InputKey(FInputKeyEventArgs::CreateSimulated(Value, Event, Event == IE_Released ? 0.0f : 1.0f)); }
}
TArray<UGameFeatureAction*> Task25Actions(UWorld* World)
{
	TArray<UGameFeatureAction*> Result;
	const UMiniExperienceDefinition* Experience = Task25Manager(World) ? Task25Manager(World)->GetCurrentExperience() : nullptr;
	if (!Experience) { return Result; }
	for (UGameFeatureAction* Action : Experience->Actions) { if (Action) { Result.Add(Action); } }
	for (const UMiniExperienceActionSet* Set : Experience->ActionSets) { if (Set) { for (UGameFeatureAction* Action : Set->Actions) { if (Action) { Result.Add(Action); } } } }
	TArray<FString> Plugins = Experience->GameFeaturesToEnable;
	for (const UMiniExperienceActionSet* Set : Experience->ActionSets) { if (Set) { for (const FString& Plugin : Set->GameFeaturesToEnable) { Plugins.AddUnique(Plugin); } } }
	for (const FString& Plugin : Plugins)
	{
		FString URL;
		if (!UGameFeaturesSubsystem::Get().GetPluginURLByName(Plugin, URL)) { continue; }
		// Read the already active data. Acceptance never activates a feature itself.
		const UGameFeatureData* Data = UGameFeaturesSubsystem::Get().GetGameFeatureDataForActivePluginByURL(URL);
		if (Data) { for (UGameFeatureAction* Action : Data->GetActions()) { if (Action) { Result.AddUnique(Action); } } }
	}
	return Result;
}
}

bool UMiniTask25ProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	FString Value;
	return Super::ShouldCreateSubsystem(Outer) && FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask25="), Value);
#else
	return false;
#endif
}
void UMiniTask25ProbeSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask25="), Mode);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask25Role="), Role);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask25Peer="), Peer);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask25Address="), Address);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask25SignalDir="), SignalDirectory);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask25MediaOutput="), MediaDirectory);
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask25TimeoutSeconds="), TimeoutSeconds);
	if (MediaDirectory.IsEmpty()) { MediaDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots")); }
	CleanupHandle = FWorldDelegates::OnPostWorldCleanup.AddUObject(this, &ThisClass::HandleCleanup);
	if (GEngine)
	{
		NetworkHandle = GEngine->OnNetworkFailure().AddWeakLambda(this,
			[this](UWorld* World, UNetDriver* Driver, ENetworkFailure::Type Failure, const FString& Reason)
			{ HandleNetworkFailure(World, Driver, int32(Failure), Reason); });
	}
	StartedAt = FPlatformTime::Seconds(); StepStartedAt = StartedAt; bInitialized = true;
}
void UMiniTask25ProbeSubsystem::Deinitialize()
{
	bInitialized = false; ++LoadedGeneration;
	FWorldDelegates::OnPostWorldCleanup.Remove(CleanupHandle); CleanupHandle.Reset();
	if (GEngine) { GEngine->OnNetworkFailure().Remove(NetworkHandle); } NetworkHandle.Reset();
	RemoveSpawnBlockers(); ReleaseOld(); Owners.Reset(); LocalLife = FMiniTask25OwnerRecord();
	Super::Deinitialize();
}
bool UMiniTask25ProbeSubsystem::IsTickable() const { return !IsTemplate() && bInitialized && !bDone && !bFailed; }
TStatId UMiniTask25ProbeSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask25ProbeSubsystem, STATGROUP_Tickables); }
UWorld* UMiniTask25ProbeSubsystem::GetTickableGameObjectWorld() const { return GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr; }
void UMiniTask25ProbeSubsystem::Fail(const TCHAR* Reason)
{
	bFailed = true;
	UE_LOG(LogMiniInit, Error, TEXT("MiniTask25Probe FAIL: Mode=%s Peer=%s Step=%d Iteration=%d Reason=%s"), *Mode, *Peer, Step, Iteration, Reason);
}
void UMiniTask25ProbeSubsystem::Pass(const TCHAR* Marker, const TCHAR* Evidence)
{
	bDone = true;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe %s: %s"), Marker, Evidence);
}
void UMiniTask25ProbeSubsystem::LogWait(const TCHAR* Reason)
{
	const double Now = FPlatformTime::Seconds(); if (Now - LastWaitAt < 5) { return; } LastWaitAt = Now;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe WAIT: Mode=%s Peer=%s Step=%d Iteration=%d World=%s Reason=%s"),
		*Mode, *Peer, Step, Iteration, *GetNameSafe(GetTickableGameObjectWorld()), Reason);
}
void UMiniTask25ProbeSubsystem::Tick(float DeltaTime)
{
	if (!IsTickable()) { return; }
	if (FPlatformTime::Seconds() - StartedAt > TimeoutSeconds) { Fail(TEXT("lifecycle acceptance timed out")); return; }
	UWorld* World = GetTickableGameObjectWorld(); if (!World || !World->HasBegunPlay() || World->bIsTearingDown) { return; }
	if (Mode == TEXT("RoundTrip")) { TickRoundTrip(); }
	else if (Role == TEXT("Client")) { TickClient(); }
	else if (Mode == TEXT("Stress")) { TickStressServer(); }
	else if (Mode.StartsWith(TEXT("Recovery"))) { TickRecovery(); }
	else { Fail(TEXT("unknown task25 mode")); }
}
bool UMiniTask25ProbeSubsystem::VerifyLateLoaded()
{
	UWorld* World = GetTickableGameObjectWorld(); UMiniExperienceManagerComponent* M = Task25Manager(World);
	if (!M || !M->IsExperienceLoaded()) { return false; }
	if (LoadedObservedWorld.Get() != World)
	{
		LoadedObservedWorld = World; LoadedCalls = 0; const uint32 Generation = ++LoadedGeneration;
		const TWeakObjectPtr<UWorld> WeakWorld(World);
		M->CallOrRegister_OnExperienceLoaded(FOnMiniExperienceLoaded::FDelegate::CreateWeakLambda(this,
			[this, WeakWorld, Generation](const UMiniExperienceDefinition* Experience)
			{
				if (Generation != LoadedGeneration || WeakWorld.Get() != GetTickableGameObjectWorld() || !Experience) { Fail(TEXT("late Loaded callback targeted an old World")); return; }
				++LoadedCalls;
			}));
		if (LoadedCalls != 1) { Fail(TEXT("Loaded late subscription was not synchronous exactly once")); return false; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe LATE_LOADED_PASS: Peer=%s World=%s CallbackCount=1 Generation=%u"), *Peer, *World->GetName(), Generation);
	}
	if (LoadedCalls != 1) { Fail(TEXT("Loaded late callback repeated after initialization notifications")); return false; }
	return true;
}
bool UMiniTask25ProbeSubsystem::FrontEndReady(bool bError)
{
	UWorld* World = GetTickableGameObjectWorld(); AMiniPlayerController* PC = Task25LocalPC(World);
	UMiniTravelSubsystem* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
	UMiniExperienceManagerComponent* M = Task25Manager(World); UMiniPrimaryGameLayout* Layout = Task25Root(PC); UMiniFrontEndWidget* Menu = Task25Front(PC);
	if (!World || World->GetNetMode() != NM_Standalone || !PC || !M || !M->IsExperienceLoaded() || !Layout || !Menu || !Travel ||
		Travel->GetState().bBusy || Travel->GetState().bHasError != bError) { LogWait(TEXT("FrontEndReady")); return false; }
	if (!World->GetMapName().EndsWith(TEXT("L_MiniFrontEnd")) || M->GetCurrentExperienceId().PrimaryAssetName != TEXT("DA_MiniFrontEndExperience") ||
		PC->GetPawn() || !Task25PS(PC) || Task25PS(PC)->GetPawnData() || !Task25ASC(PC) || Task25ASC(PC)->GetAvatarActor() || Task25ASC(PC)->GetActivatableAbilities().Num() ||
		World->GetGameState()->FindComponentByClass<UMiniArenaRulesComponent>() || World->GetGameState()->FindComponentByClass<UMiniMatchRulesComponent>())
	{ Fail(TEXT("front end retained combat state")); return false; }
	UMiniConnectionStatusWidget* Modal = Travel->GetConnectionStatusWidget();
	if (Layout->GetGameplayInputBlockCount() != (bError ? 2 : 1) || !PC->IsMiniInputBlocked() || Menu->GetStateListenerCount() != 1 ||
		(bError && (!Modal || !Modal->IsActivated() || Modal->GetStateListenerCount() != 1)) || (!bError && Modal && Modal->IsActivated())) { return false; }
	return VerifyLateLoaded();
}
bool UMiniTask25ProbeSubsystem::GameplayReady(bool bArena, bool bFresh)
{
	UWorld* World = GetTickableGameObjectWorld(); AMiniPlayerController* PC = Task25LocalPC(World); UMiniExperienceManagerComponent* M = Task25Manager(World);
	if (!M || !M->IsExperienceLoaded() || !Task25Live(PC) || !Task25Root(PC) || Task25Root(PC)->GetGameplayInputBlockCount() || PC->IsMiniInputBlocked()) { return false; }
	const FName Expected(bArena ? TEXT("DA_MiniArenaExperience") : TEXT("DA_MiniPracticeExperience"));
	if (M->GetCurrentExperienceId().PrimaryAssetName != Expected || !World->GetMapName().EndsWith(bArena ? TEXT("L_MiniArena") : TEXT("L_MiniPractice")))
	{ Fail(TEXT("wrong production map or Experience")); return false; }
	FMiniTask25Observation O; if (!ObserveOwner(PC, false, bFresh, O)) { return false; }
	if (bArena != (World->GetGameState()->FindComponentByClass<UMiniArenaRulesComponent>() != nullptr)) { Fail(TEXT("wrong Arena feature assembly")); return false; }
	int32 Targets = 0, Supplies = 0;
	for (TActorIterator<AMiniPracticeTarget> It(World); It; ++It) { ++Targets; if (!It->IsTargetEnabled()) { return false; } }
	for (TActorIterator<AMiniPracticeSupply> It(World); It; ++It) { ++Supplies; }
	if (bArena ? Targets || Supplies : Targets != 3 || Supplies != 1) { return false; }
	return VerifyLateLoaded();
}
bool UMiniTask25ProbeSubsystem::ObserveOwner(AMiniPlayerController* PC, bool bDead, bool bFresh, FMiniTask25Observation& O)
{
	AMiniCharacter* Character = Task25Pawn(PC); UMiniAbilitySystemComponent* AbilitySystem = Task25ASC(PC);
	if (!PC || !Character || !AbilitySystem || !Task25PS(PC) || !Character->GetHealthComponent() || Character->GetHealthComponent()->IsDead() != bDead ||
		AbilitySystem->GetAvatarActor() != Character || AbilitySystem->GetOwnerActor() != Task25PS(PC)) { return false; }
	UMiniHeroComponent* Hero = Character->GetHeroComponent(); UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!Hero || !Movement) { return false; }
	O.InputBindings = Hero->GetInputBindingCount(); O.HeldInput = AbilitySystem->GetHeldInputCount();
	O.SavedMoves = Task25SavedMoves(Character); O.bMovementTick = Movement->IsComponentTickEnabled(); O.bMapping = Hero->OwnsInputMapping();
	O.EquipmentSpecs = Task25SourceSpecs(AbilitySystem, Task25Equipment(PC));
	O.OldEquipmentSpecs = LocalLife.DeadPawn ? Task25SourceSpecs(AbilitySystem, LocalLife.DeadEquipment) : 0;
	O.bFreshInventory = Task25Fresh(PC);
	O.RifleId = PC->GetQuickBar()->GetSlotItemId(0); O.PistolId = PC->GetQuickBar()->GetSlotItemId(1);
	if (bDead)
	{
		if (Movement->MovementMode != MOVE_None || O.bMovementTick || O.HeldInput || O.InputBindings || O.bMapping || Task25Equipment(PC) ||
			(PC->IsLocalController() && O.SavedMoves)) { return false; }
	}
	else if (!Task25Live(PC) || !O.bMovementTick || O.EquipmentSpecs != Task25ExpectedEquipmentSpecs(Task25Equipment(PC)) || (bFresh && !O.bFreshInventory)) { return false; }
	if (!PC->IsLocalController()) { return true; }
	UMiniHUDLayout* Layout = Task25HUD(PC); UMiniHUDViewModel* VM = Layout ? Layout->GetViewModel() : nullptr;
	if (!VM || !VM->IsRunning() || !Task25Root(PC) || Task25Root(PC)->GetGameplayInputBlockCount() || !Layout || Layout->GetExtensionWidgetCount() != 4) { return false; }
	const FMiniHUDSnapshot& S = VM->GetSnapshot();
	O.HUDWidgets = Layout->GetExtensionWidgetCount(); O.VMBindings = VM->GetBindingCount();
	O.bHUDMatches = S.World == Character->GetWorld() && S.Pawn == Character && S.bHealthReady && S.bDead == bDead &&
		FMath::IsNearlyEqual(S.Health, Task25PS(PC)->GetHealthSet()->GetHealth(), 0.01f);
	if (!O.bHUDMatches || O.VMBindings <= 0 || O.OldEquipmentSpecs || (!bDead && (O.InputBindings != 16 || !O.bMapping))) { return false; }
	if (!bDead && bFresh && (!S.bAmmoReady || S.ActiveSlot != 0 || S.MagazineAmmo != 30 || S.ReserveAmmo != 90 || S.ItemId != O.RifleId)) { return false; }
	if (S.bHasMatchData)
	{
		const FMiniMatchState Current = Task25Match(Character->GetWorld()); const FMiniGamePhaseState CurrentPhase = Task25Phase(Character->GetWorld());
		const int32 Remaining = FMath::CeilToInt(FMath::Max(0.0, CurrentPhase.PhaseEndTimeServer - Character->GetWorld()->GetGameState()->GetServerWorldTimeSeconds()));
		if (!MiniTask25SameMatch(S.MatchState, Current) || S.Score != Task25PS(PC)->GetMatchStats().Kills || S.Deaths != Task25PS(PC)->GetMatchStats().Deaths ||
			S.PhaseTag != CurrentPhase.PhaseTag || (CurrentPhase.PhaseEndTimeServer > 0 && (!VM->IsPhaseCountdownRunning() || FMath::Abs(S.PhaseRemainingSeconds - Remaining) > 1))) { return false; }
	}
	return true;
}

void UMiniTask25ProbeSubsystem::DiscoverOwners()
{
	UWorld* World = GetTickableGameObjectWorld();
	for (TActorIterator<AMiniPlayerController> It(World); It; ++It)
	{
		AMiniPlayerController* PC = *It;
		if (!PC->HasActorBegunPlay() || !Task25Live(PC) || Owners.ContainsByPredicate([PC](const FMiniTask25OwnerRecord& R) { return R.PC == PC; })) { continue; }
		FMiniTask25OwnerRecord Record; Record.PC = PC; Record.Index = PC->IsLocalController() ? 0 : NextOwnerIndex;
		if (!CaptureLife(Record, Iteration)) { continue; }
		if (!PC->IsLocalController())
		{
			FActorSpawnParameters Params; Params.Owner = PC; Params.ObjectFlags |= RF_Transient;
			Record.Probe = World->SpawnActor<AMiniTask25ProbeActor>(Params);
			if (!Record.Probe) { Fail(TEXT("owner checkpoint actor spawn failed")); return; }
			Record.Probe->InitializeServer(Record.Index);
			++NextOwnerIndex;
		}
		Owners.Add(MoveTemp(Record));
	}
}
bool UMiniTask25ProbeSubsystem::CaptureLife(FMiniTask25OwnerRecord& R, int32 DeathIteration)
{
	if (!Task25Live(R.PC) || !Task25Fresh(R.PC)) { return false; }
	FMiniTask25Observation Observation;
	if (!ObserveOwner(R.PC, false, true, Observation)) { return false; }
	AMiniPlayerState* State = Task25PS(R.PC); UMiniAbilitySystemComponent* AbilitySystem = Task25ASC(R.PC); AMiniCharacter* Character = Task25Pawn(R.PC);
	const int32 CurrentSpecs = AbilitySystem->GetActivatableAbilities().Num();
	if (R.AbilitySpecs >= 0 && R.AbilitySpecs != CurrentSpecs)
	{ Fail(TEXT("ASC ability specs grew or changed across equivalent live lives")); return false; }
	R.AbilitySpecs = CurrentSpecs;
	if (R.PS && (R.PS != State || R.ASC != AbilitySystem)) { Fail(TEXT("same-world respawn replaced PlayerState or ASC")); return false; }
	if (!R.PS)
	{
		R.PS = State; R.ASC = AbilitySystem; R.InitialKills = State->GetMatchStats().Kills; R.InitialDeaths = State->GetMatchStats().Deaths;
	}
	if (R.Pawn == Character) { return true; }
	if (R.Life > 0 && State->GetCurrentLifeId() != R.Life + 1) { Fail(TEXT("new Pawn did not advance life exactly once")); return false; }
	if (R.DeadPawn && (R.DeadPawn->HasActorBegunPlay() || Task25SourceSpecs(AbilitySystem, R.DeadEquipment) ||
		(R.DeadEquipment && !R.DeadEquipment->GetGrantedHandles().IsEmpty()))) { return false; }
	if (R.DeadItem && R.bFrozenAmmo && (R.DeadItem->GetStat(MiniInventoryTags::AmmoInMagazine) != R.FrozenMagazine ||
		R.DeadItem->GetStat(MiniInventoryTags::ReserveAmmo) != R.FrozenReserve))
	{ Fail(TEXT("old owner fire/reload item changed across the real respawn delay")); return false; }
	if (R.PC->HasAuthority() && Task25Equipment(R.PC)->GetGrantedHandles().GetAbilityCount() != Task25ExpectedEquipmentSpecs(Task25Equipment(R.PC))) { return false; }
	if (R.PC->IsLocalController() && R.VMBindings >= 0 && R.VMBindings != Observation.VMBindings)
	{ Fail(TEXT("HUD bindings grew or changed across equivalent live lives")); return false; }
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		const FGuid Id = R.PC->GetQuickBar()->GetSlotItemId(Slot);
		if (!Id.IsValid() || R.SeenItemIds.Contains(Id)) { Fail(TEXT("new life reused an inventory GUID")); return false; }
		R.SeenItemIds.Add(Id);
	}
	const int32 SpecsBefore = AbilitySystem->GetActivatableAbilities().Num();
	const FGuid RifleBefore = R.PC->GetQuickBar()->GetSlotItemId(0); const FGuid PistolBefore = R.PC->GetQuickBar()->GetSlotItemId(1);
	if (R.PC->HasAuthority() && (!State->SetPawnData(State->GetPawnData()) || !R.PC->GetQuickBar()->InitializeForPawn(Character)))
	{ Fail(TEXT("repeated same data/loadout assignment was rejected")); return false; }
	Character->NotifyInitDependenciesChanged();
	if (AbilitySystem->GetActivatableAbilities().Num() != SpecsBefore || R.PC->GetQuickBar()->GetSlotItemId(0) != RifleBefore ||
		R.PC->GetQuickBar()->GetSlotItemId(1) != PistolBefore) { Fail(TEXT("repeated initialization duplicated grants or items")); return false; }
	const bool bRespawn = R.Life > 0;
	R.Pawn = Character; R.Life = State->GetCurrentLifeId(); R.CurrentEquipment = Task25Equipment(R.PC); R.CurrentItem = R.PC->GetQuickBar()->GetSlotItem(0);
	if (bRespawn) { ++R.RespawnsSeen; }
	if (R.PC->IsLocalController())
	{
		R.VMBindings = Observation.VMBindings; bMoveVerified = false; bMovePressed = false;
	}
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe LIFE_READY: Peer=%s Owner=%d PlayerId=%d Life=%u Iteration=%d Inventory=2 DuplicateNotifications=0"),
		*Peer, R.Index, State->GetPlayerId(), R.Life, DeathIteration);
	return true;
}
bool UMiniTask25ProbeSubsystem::CheckDead(FMiniTask25OwnerRecord& R)
{
	if (!R.PC || !R.PS || !R.ASC || Task25Pawn(R.PC) != R.DeadPawn || !R.DeadPawn || !R.DeadPawn->GetHealthComponent()->IsDead()) { return false; }
	UCharacterMovementComponent* Movement = R.DeadPawn->GetCharacterMovement();
	if (!Movement || Movement->IsComponentTickEnabled() || Movement->MovementMode != MOVE_None || R.ASC->GetHeldInputCount() ||
		Task25SourceSpecs(R.ASC, R.DeadEquipment) || (R.DeadEquipment && !R.DeadEquipment->GetGrantedHandles().IsEmpty()) ||
		R.PS->GetCurrentLifeId() != R.Life || Task25PS(R.PC) != R.PS || Task25ASC(R.PC) != R.ASC || R.ASC->GetAvatarActor() != R.DeadPawn ||
		( R.PC->IsLocalController() && Task25SavedMoves(R.DeadPawn))) { return false; }
	if (R.DeadItem && !R.bFrozenAmmo)
	{
		R.FrozenMagazine = R.DeadItem->GetStat(MiniInventoryTags::AmmoInMagazine); R.FrozenReserve = R.DeadItem->GetStat(MiniInventoryTags::ReserveAmmo); R.bFrozenAmmo = true;
	}
	return true;
}
bool UMiniTask25ProbeSubsystem::Checkpoint(FName Name, int32 ExpectedClients, bool bDead, bool bFresh)
{
	UWorld* World = GetTickableGameObjectWorld(); const FMiniGamePhaseState CurrentPhase = Task25Phase(World); const FMiniMatchState CurrentMatch = Task25Match(World);
	int32 Clients = 0;
	for (FMiniTask25OwnerRecord& R : Owners) { Clients += R.PC && R.PC->HasActorBegunPlay() && !R.PC->IsLocalController() ? 1 : 0; }
	if (Clients != ExpectedClients) { return false; }
	if (!bPublished || PublishedName != Name)
	{
		PublishedName = Name; PublishedPhase = CurrentPhase; PublishedMatch = CurrentMatch; bPublished = true; ++Serial;
		for (FMiniTask25OwnerRecord& R : Owners) { if (R.PC && R.PC->HasActorBegunPlay() && R.Probe) { R.Probe->SetCheckpoint(Serial, Name, Iteration, bDead, bFresh, CurrentPhase, CurrentMatch); } }
	}
	if (!MiniTask25SamePhase(CurrentPhase, PublishedPhase) || !MiniTask25SameMatch(CurrentMatch, PublishedMatch))
	{ Fail(TEXT("match/phase changed while waiting for a checkpoint")); return false; }
	for (const FMiniTask25OwnerRecord& R : Owners) { if (R.PC && R.PC->HasActorBegunPlay() && R.Probe && !R.Probe->HasAcknowledged()) { return false; } }
	// RecoveryDeath kills the remote owner only; the host must remain alive.
	FMiniTask25Observation O; return ObserveOwner(Task25LocalPC(World), bDead && Mode == TEXT("Stress"), bFresh, O);
}
AMiniTask25ProbeActor* UMiniTask25ProbeSubsystem::LocalProbe() const
{
	UWorld* World = GetTickableGameObjectWorld(); AMiniPlayerController* PC = Task25LocalPC(World);
	for (TActorIterator<AMiniTask25ProbeActor> It(World); It; ++It) { if (It->GetOwner() == PC && It->GetOwnerIndex() > 0) { return *It; } }
	return nullptr;
}
bool UMiniTask25ProbeSubsystem::DriveArmInput(AMiniTask25ProbeActor* Probe)
{
	if (Probe->GetCommand() != EMiniTask25Command::ArmFire && Probe->GetCommand() != EMiniTask25Command::ArmReload) { return true; }
	AMiniPlayerController* PC = Task25LocalPC(GetTickableGameObjectWorld());
	if (!Task25Live(PC) || !PC->GetQuickBar()->GetSlotItem(0)) { return false; }
	UMiniInventoryItemInstance* Item = PC->GetQuickBar()->GetSlotItem(0);
	if (!ArmedSerials.Contains(Probe->GetSerial()))
	{
		ArmedSerials.Add(Probe->GetSerial()); ArmStep = 1; ArmStartedAt = FPlatformTime::Seconds();
		PC->SetControlRotation(FRotator(80, 0, 0)); Task25Key(PC, EKeys::LeftMouseButton, IE_Pressed); return false;
	}
	if (Item->GetStat(MiniInventoryTags::AmmoInMagazine) >= 30) { return false; }
	if (Probe->GetCommand() == EMiniTask25Command::ArmFire)
	{
		if (Task25ASC(PC)->GetHeldInputCount() <= 0) { return false; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe INPUT_ARMED: Peer=%s FireHeld=1 AmmoChanged=1 ActualInputKey=1"), *Peer); return true;
	}
	if (ArmStep == 1)
	{
		Task25Key(PC, EKeys::LeftMouseButton, IE_Released); Task25Key(PC, EKeys::R, IE_Pressed); ArmStep = 2; return false;
	}
	if (!Task25ASC(PC)->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading)) { return false; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe INPUT_ARMED: Peer=%s ReloadActive=1 AmmoChanged=1 ActualInputKey=1"), *Peer); return true;
}
bool UMiniTask25ProbeSubsystem::Acknowledge(AMiniTask25ProbeActor* Probe)
{
	UWorld* World = GetTickableGameObjectWorld(); AMiniPlayerController* PC = Task25LocalPC(World);
	if (!Probe || Probe->GetSerial() <= 0 || Probe->GetSerial() == LastClientSerial) { return false; }
	const auto Wait = [this, Probe, PC, World](const TCHAR* Reason)
	{
		const double Now = FPlatformTime::Seconds(); if (Now - LastWaitAt < 5) { return false; } LastWaitAt = Now;
		UMiniInventoryItemInstance* Rifle = PC && PC->GetQuickBar() ? PC->GetQuickBar()->GetSlotItem(0) : nullptr;
		UMiniHUDLayout* HUD = Task25HUD(PC); UMiniHUDViewModel* VM = HUD ? HUD->GetViewModel() : nullptr;
		const FMiniHUDSnapshot* Snapshot = VM ? &VM->GetSnapshot() : nullptr;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe ACK_WAIT: Peer=%s Checkpoint=%s Serial=%d Reason=%s Pawn=%s ExpectedPawn=%s Life=%u ExpectedLife=%u Fresh=%d Magazine=%d Reserve=%d Held=%d RawMouse=%d RawR=%d HUDMagazine=%d HUDPawn=%s PhaseMatches=%d MatchMatches=%d"),
			*Peer, *Probe->GetCheckpointName().ToString(), Probe->GetSerial(), Reason, *GetNameSafe(Task25Pawn(PC)), *GetNameSafe(Probe->GetExpectedPawn()),
			Task25PS(PC) ? Task25PS(PC)->GetCurrentLifeId() : 0, Probe->GetExpectedLife(), Task25Fresh(PC) ? 1 : 0,
			Rifle ? Rifle->GetStat(MiniInventoryTags::AmmoInMagazine) : -1, Rifle ? Rifle->GetStat(MiniInventoryTags::ReserveAmmo) : -1,
			Task25ASC(PC) ? Task25ASC(PC)->GetHeldInputCount() : -1, PC && PC->IsInputKeyDown(EKeys::LeftMouseButton) ? 1 : 0,
			PC && PC->IsInputKeyDown(EKeys::R) ? 1 : 0, Snapshot ? Snapshot->MagazineAmmo : -1, Snapshot ? *GetNameSafe(Snapshot->Pawn) : TEXT("None"),
			MiniTask25SamePhase(Task25Phase(World), Probe->GetExpectedPhase()) ? 1 : 0, MiniTask25SameMatch(Task25Match(World), Probe->GetExpectedMatch()) ? 1 : 0);
		return false;
	};
	if (!PC || !Task25PS(PC) ||
		Task25PS(PC) != Probe->GetExpectedPlayerState() || Task25PS(PC)->GetCurrentLifeId() != Probe->GetExpectedLife() || Task25Pawn(PC) != Probe->GetExpectedPawn() ||
		!MiniTask25SamePhase(Task25Phase(World), Probe->GetExpectedPhase()) || !MiniTask25SameMatch(Task25Match(World), Probe->GetExpectedMatch()) || !VerifyLateLoaded()) { return Wait(TEXT("IdentityPhaseMatch")); }
	if (Probe->ExpectsDead())
	{
		if (LocalLife.LastDeadIteration != Probe->GetIteration())
		{
			LocalLife.DeadPawn = LocalLife.Pawn; LocalLife.DeadEquipment = LocalLife.CurrentEquipment; LocalLife.DeadItem = LocalLife.CurrentItem;
			LocalLife.bFrozenAmmo = false;
		}
		if (!CheckDead(LocalLife)) { return Wait(TEXT("DeadCancellation")); }
	}
	else
	{
		// Arm checkpoints deliberately have changed ammo. Every new life was already captured at a fresh checkpoint.
		if (Probe->ExpectsFreshInventory() && !CaptureLife(LocalLife, Probe->GetIteration())) { return Wait(TEXT("FreshLife")); }
		if (!DriveArmInput(Probe)) { return Wait(TEXT("ArmInput")); }
	}
	FMiniTask25Observation O; if (!ObserveOwner(PC, Probe->ExpectsDead(), Probe->ExpectsFreshInventory(), O)) { return Wait(TEXT("OwnerHUDInput")); }
	if (Probe->ExpectsDead())
	{
		// Measure cancellation while still held, then release the simulated hardware
		// keys before testing a fresh input sequence on the replacement Pawn.
		Task25Key(PC, EKeys::LeftMouseButton, IE_Released);
		Task25Key(PC, EKeys::R, IE_Released);
	}
	if (Mode == TEXT("Stress") && Probe->ExpectsFreshInventory() && !Probe->ExpectsDead() && !bMoveVerified)
	{
		if (!bMovePressed) { MoveStart = Task25Pawn(PC)->GetActorLocation(); MoveStartedAt = FPlatformTime::Seconds(); Task25Key(PC, EKeys::W, IE_Pressed); bMovePressed = true; return false; }
		if (FPlatformTime::Seconds() - MoveStartedAt < 0.2) { return false; }
		Task25Key(PC, EKeys::W, IE_Released);
		if (FVector::Dist2D(MoveStart, Task25Pawn(PC)->GetActorLocation()) < 8.0) { Fail(TEXT("new live Pawn did not respond to actual movement input")); return false; }
		bMoveVerified = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe NEW_INPUT_PASS: Peer=%s Life=%u ActualW=1 Distance=%.1f"), *Peer, Task25PS(PC)->GetCurrentLifeId(), FVector::Dist2D(MoveStart, Task25Pawn(PC)->GetActorLocation()));
	}
	const FName Name = Probe->GetCheckpointName();
	if ((Name == TEXT("StressBaseline") || Name == TEXT("Rejoined")) && !Media(Name)) { return false; }
	LastClientSerial = Probe->GetSerial();
	if (Probe->ExpectsDead() && LocalLife.LastDeadIteration != Probe->GetIteration())
	{
		LocalLife.LastDeadIteration = Probe->GetIteration(); ++LocalLife.DeathsSeen; ++ClientDeaths;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe CLIENT_DEATH: Peer=%s Iteration=%d Life=%u Bindings=0 Held=0 Prediction=0 MovementTick=0 OldEquipmentSpecs=0"), *Peer, Probe->GetIteration(), Task25PS(PC)->GetCurrentLifeId());
	}
	if (!Probe->ExpectsDead() && Probe->ExpectsFreshInventory() && Probe->GetIteration() > 0 && Name != TEXT("Rejoined") &&
		LocalLife.LastLiveIteration < Probe->GetIteration())
	{
		LocalLife.LastLiveIteration = Probe->GetIteration(); ++ClientRespawns;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe CLIENT_RESPAWN: Peer=%s Iteration=%d Life=%u SameASC=1 FreshInventory=1 Input=1 HUD=1"), *Peer, Probe->GetIteration(), Task25PS(PC)->GetCurrentLifeId());
	}
	Probe->ServerAcknowledge(LastClientSerial, Task25Phase(World), Task25Match(World), Task25PS(PC)->GetMatchStats(), Task25PS(PC)->GetCurrentLifeId(), Task25Pawn(PC), Task25PS(PC), World->GetGameState()->GetServerWorldTimeSeconds(), O);
	return true;
}

void UMiniTask25ProbeSubsystem::TickStressServer()
{
	UWorld* World = GetTickableGameObjectWorld(); if (World->GetNetMode() != NM_ListenServer || !VerifyLateLoaded()) { return; }
	AMiniGameMode* GM = World->GetAuthGameMode<AMiniGameMode>(); const FMiniMatchState M = Task25Match(World); const FMiniGamePhaseState P = Task25Phase(World);
	if (!GM || P.PhaseTag != MiniGameplayTags::GamePhase_MiniArena_Playing) { return; }
	DiscoverOwners(); if (bFailed) { return; }
	const double Now = FPlatformTime::Seconds();
	if (Step == 0)
	{
		if (!GameplayReady(true) || M.ConnectedPlayerCount != 3 || !Checkpoint(TEXT("LateJoinBaseline"), 2)) { return; }
		InitialRound = M.RoundId;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe STRESS_LATE_JOIN_READY: Players=3 Playing=1 OwnersAcknowledged=2 Round=%d Deadline=%.3f"), M.RoundId, P.PhaseEndTimeServer);
		WriteSignal(TEXT("LateJoinReady")); Step = 1; return;
	}
	if (Step == 1)
	{
		if (M.ConnectedPlayerCount != 4 || Owners.Num() != 4 || !Checkpoint(TEXT("StressBaseline"), 3)) { return; }
		if (M.RoundId != InitialRound || M.ScoreLimit != 10 || FMath::Abs(P.PhaseEndTimeServer - P.PhaseStartTimeServer - 300.0) > 0.1)
		{ Fail(TEXT("Stress did not preserve production round, score limit and deadline")); return; }
		Step = 2; return;
	}
	if (Step >= 2 && Step <= 6 && (M.RoundId != InitialRound || M.ConnectedPlayerCount != 4 || !M.bAcceptingScores || M.bHasResult))
	{ Fail(TEXT("environment stress unexpectedly changed round or roster")); return; }
	if (Step == 2)
	{
		for (const FMiniTask25OwnerRecord& R : Owners) { if (R.ASC->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected)) { return; } }
		++Iteration; bPublished = false;
		for (FMiniTask25OwnerRecord& R : Owners) { if (R.Probe) { R.Probe->SetCommand(R.Index == 1 && Iteration <= 2 ? (Iteration == 1 ? EMiniTask25Command::ArmFire : EMiniTask25Command::ArmReload) : EMiniTask25Command::None); } }
		Step = 3; return;
	}
	if (Step == 3)
	{
		if (!Checkpoint(FName(*FString::Printf(TEXT("Armed%d"), Iteration)), 3, false, false)) { return; }
		for (const FMiniTask25OwnerRecord& R : Owners) { if (R.ASC->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected)) { return; } }
		FMiniTask25OwnerRecord* Armed = Owners.FindByPredicate([](const FMiniTask25OwnerRecord& R) { return R.Index == 1; });
		if (Iteration == 1 && (!Armed || !Armed->CurrentItem || Armed->CurrentItem->GetStat(MiniInventoryTags::AmmoInMagazine) >= 30)) { return; }
		if (Iteration == 2 && (!Armed || !Armed->ASC->HasMatchingGameplayTag(MiniGameplayTags::State_Reloading))) { return; }
		for (FMiniTask25OwnerRecord& R : Owners)
		{
			R.DeadPawn = Task25Pawn(R.PC); R.DeadEquipment = Task25Equipment(R.PC); R.DeadItem = R.CurrentItem; R.bFrozenAmmo = false;
			const int32 DeathsBefore = R.PS->GetMatchStats().Deaths;
			if (!GM->TryApplyEnvironmentDamage(R.DeadPawn, R.PS->GetHealthSet()->GetHealth()) || !R.DeadPawn->GetHealthComponent()->IsDead() ||
				R.PS->GetMatchStats().Deaths != DeathsBefore + 1 || R.PS->GetMatchStats().Kills != R.InitialKills ||
				GM->TryApplyEnvironmentDamage(R.DeadPawn, 1.0f) || R.PS->GetMatchStats().Deaths != DeathsBefore + 1)
			{ Fail(TEXT("real environment GE death or duplicate-death rejection failed")); return; }
			++R.DeathsSeen;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe STRESS_DEATH: Iteration=%d PlayerId=%d OldLife=%u RealGE=1"), Iteration, R.PS->GetPlayerId(), R.Life);
		}
		bPublished = false; StepStartedAt = Now; Step = 4; return;
	}
	if (Step == 4)
	{
		for (FMiniTask25OwnerRecord& R : Owners) { if (!CheckDead(R)) { return; } }
		if (GM->GetPendingRespawnCount() != 4 || !Checkpoint(FName(*FString::Printf(TEXT("Dead%d"), Iteration)), 3, true, false)) { return; }
		Step = 5; return;
	}
	if (Step == 5)
	{
		for (FMiniTask25OwnerRecord& R : Owners)
		{
			if (!Task25Live(R.PC) || Task25Pawn(R.PC) == R.DeadPawn || !Task25Fresh(R.PC)) { return; }
			if (R.DeadItem && R.bFrozenAmmo && (R.DeadItem->GetStat(MiniInventoryTags::AmmoInMagazine) != R.FrozenMagazine || R.DeadItem->GetStat(MiniInventoryTags::ReserveAmmo) != R.FrozenReserve))
			{ Fail(TEXT("old fire/reload timer changed an old item after death")); return; }
		}
		if (Now - StepStartedAt < 2.9 || GM->GetPendingRespawnCount()) { return; }
		for (FMiniTask25OwnerRecord& R : Owners)
		{
			if (!CaptureLife(R, Iteration)) { return; }
			if (R.PS->GetMatchStats().Deaths != R.InitialDeaths + Iteration || R.DeathsSeen != Iteration || R.RespawnsSeen != Iteration)
			{ Fail(TEXT("stress life/death/stat counters diverged")); return; }
			if (R.Probe) { R.Probe->SetCommand(EMiniTask25Command::None); }
		}
		bPublished = false; Step = 6; return;
	}
	if (Step == 6)
	{
		if (!Checkpoint(FName(*FString::Printf(TEXT("Live%d"), Iteration)), 3)) { return; }
		for (FMiniTask25OwnerRecord& R : Owners)
		{
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe STRESS_RESPAWN: Iteration=%d PlayerId=%d NewLife=%u SamePS=1 SameASC=1 FreshInventory=1"), Iteration, R.PS->GetPlayerId(), R.Life);
			if (R.Probe) { R.Probe->SetCommand(EMiniTask25Command::None); }
		}
		if (Iteration < 10) { Step = 2; return; }
		FMiniTask25OwnerRecord* Leaving = Owners.FindByPredicate([](const FMiniTask25OwnerRecord& R) { return R.Index == 1; });
		Leaving->Probe->SetCommand(EMiniTask25Command::LeaveAndRejoin); StepStartedAt = Now; Step = 7; return;
	}
	if (Step == 7)
	{
		if (M.ConnectedPlayerCount != 3) { return; }
		FMiniTask25OwnerRecord* Leaving = Owners.FindByPredicate([](const FMiniTask25OwnerRecord& R) { return R.Index == 1; });
		if ((Leaving->PC && Leaving->PC->HasActorBegunPlay()) || (Leaving->Pawn && Leaving->Pawn->HasActorBegunPlay()) || GM->GetPendingRespawnCount() || GM->GetPendingOutOfWorldRecoveryCount()) { return; }
		for (const FMiniTask25OwnerRecord& R : Owners) { if (R.Index != 1 && (Task25PS(R.PC) != R.PS || Task25ASC(R.PC) != R.ASC)) { Fail(TEXT("one Logout replaced retained participant identities")); return; } }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe STRESS_LOGOUT_PASS: Roster=3 OldPawnEnded=1 RetainedPSASC=1 OldWork=0"));
		bPublished = false; Step = 8; return;
	}
	if (Step == 8)
	{
		if (M.ConnectedPlayerCount != 4 || Owners.Num() != 5) { return; }
		const FMiniTask25OwnerRecord& Rejoined = Owners.Last();
		const FMiniTask25OwnerRecord* Departed = Owners.FindByPredicate([](const FMiniTask25OwnerRecord& R) { return R.Index == 1; });
		if (Rejoined.InitialDeaths || Rejoined.InitialKills || Rejoined.PS->GetMatchStats().Deaths || Rejoined.PS->GetMatchStats().Kills ||
			!Departed || Rejoined.PS == Departed->PS || Rejoined.ASC == Departed->ASC || !Task25Fresh(Rejoined.PC)) { Fail(TEXT("rejoined player inherited the departed life or score")); return; }
		if (!Checkpoint(TEXT("Rejoined"), 3)) { return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe STRESS_SERVER_PASS: Players=4 Iterations=10 EnvironmentDeaths=40 Respawns=40 LateJoinPlaying=1 Rejoin=1"));
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe HOST_LOSS_ARMED: Peer=%s ActualClients=3"), *Peer);
		bDone = true;
	}
}

bool UMiniTask25ProbeSubsystem::RetainWorld(bool bOpenMenu)
{
	if (Old.bRetained) { Fail(TEXT("old observation was overwritten before verification")); return false; }
	UWorld* World = GetTickableGameObjectWorld(); AMiniPlayerController* PC = Task25LocalPC(World);
	if (!World || !PC || !Task25PS(PC) || !Task25ASC(PC)) { return false; }
	Old.World = World; Old.GM = World->GetAuthGameMode<AMiniGameMode>(); Old.PC = PC; Old.PS = Task25PS(PC); Old.ASC = Task25ASC(PC);
	Old.PSKey = FObjectKey(Old.PS); Old.ASCKey = FObjectKey(Old.ASC);
	Old.Pawn = Task25Pawn(PC); Old.Hero = Old.Pawn ? Old.Pawn->GetHeroComponent() : nullptr;
	Old.Equipment = Task25Equipment(PC); Old.Item = PC->GetQuickBar()->GetSlotItem(0);
	Old.Root = Task25Root(PC); Old.HUD = Task25HUD(PC); Old.VM = Old.HUD ? Old.HUD->GetViewModel() : nullptr;
	Old.Front = Task25Front(PC); Old.Modal = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>()->GetConnectionStatusWidget();
	if (Old.HUD)
	{
		if (bOpenMenu) { Old.Menu = Old.HUD->OpenDebugMenu(); if (!Old.Menu || !Old.Menu->IsActivated()) { return false; } }
		else { Old.Menu = Old.HUD->GetDebugMenu(); }
		TArray<UMiniHUDDataWidget*> Widgets; Old.HUD->GetExtensionWidgets(Widgets); for (UMiniHUDDataWidget* Widget : Widgets) { Old.Widgets.Add(Widget); }
	}
	for (TActorIterator<AMiniPracticeTarget> It(World); It; ++It) { Old.Targets.Add(*It); }
	for (TActorIterator<AMiniPracticeSupply> It(World); It; ++It) { Old.Supply = *It; }
	for (UGameFeatureAction* Action : Task25Actions(World))
	{
		if (UMiniGameFeatureAction_AddActors* ActorAction = Cast<UMiniGameFeatureAction_AddActors>(Action)) { Old.ActorActions.Add(ActorAction); }
		if (UMiniGameFeatureAction_AddWidgets* WidgetAction = Cast<UMiniGameFeatureAction_AddWidgets>(Action)) { Old.WidgetActions.Add(WidgetAction); }
	}
	Old.bRetained = true; Old.bCleaned = false;
	return true;
}
void UMiniTask25ProbeSubsystem::HandleCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (!bInitialized || !Old.bRetained || Old.bCleaned || World != Old.World) { return; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe CLEANUP_OBSERVATION: Peer=%s PC=%d PS=%d Pawn=%d Avatar=%d Input=%d Held=%d EquipmentSpecs=%d ActiveSpecs=%d ASCRegistered=%d VMRunning=%d VMBindings=%d VMWorld=%d VMPawn=%d HUDWidgets=%d HUDPoints=%d Menu=%d Gates=%d FrontListeners=%d ModalListeners=%d"),
		*Peer, Old.PC && Old.PC->HasActorBegunPlay(), Old.PS && Old.PS->HasActorBegunPlay(), Old.Pawn && Old.Pawn->HasActorBegunPlay(), Old.ASC && Old.ASC->GetAvatarActor() != nullptr,
		Old.Hero ? Old.Hero->GetInputBindingCount() : 0, Old.ASC ? Old.ASC->GetHeldInputCount() : 0, Task25SourceSpecs(Old.ASC, Old.Equipment), Task25ActiveSpecs(Old.ASC), Old.ASC && Old.ASC->IsRegistered(),
		Old.VM && Old.VM->IsRunning(), Old.VM ? Old.VM->GetBindingCount() : 0, Old.VM && Old.VM->GetSnapshot().World != nullptr, Old.VM && Old.VM->GetSnapshot().Pawn != nullptr,
		Old.HUD ? Old.HUD->GetExtensionWidgetCount() : 0, Old.HUD ? Old.HUD->GetExtensionPointCount() : 0, Old.Menu && Old.Menu->IsActivated(), Old.Root ? Old.Root->GetGameplayInputBlockCount() : 0,
		Old.Front ? Old.Front->GetStateListenerCount() : 0, Old.Modal ? Old.Modal->GetStateListenerCount() : 0);
	if ((Old.GM && (Old.GM->GetPendingRespawnCount() || Old.GM->GetPendingOutOfWorldRecoveryCount())) ||
		(Old.PC && Old.PC->HasActorBegunPlay()) || (Old.PS && Old.PS->HasActorBegunPlay()) || (Old.Pawn && Old.Pawn->HasActorBegunPlay()) ||
		(Old.ASC && Old.Pawn && Old.ASC->GetAvatarActor() == Old.Pawn) ||
		(Old.Hero && (Old.Hero->IsInputActive() || Old.Hero->GetInputBindingCount() || Old.Hero->OwnsInputMapping())) ||
		(Old.ASC && (Old.ASC->GetHeldInputCount() || Task25ActiveSpecs(Old.ASC) || Old.ASC->IsRegistered() ||
			(Old.PS && Old.PS->HasAuthority() && Task25SourceSpecs(Old.ASC, Old.Equipment)))) ||
		(Old.Equipment && !Old.Equipment->GetGrantedHandles().IsEmpty()) ||
		(Old.VM && (Old.VM->IsRunning() || Old.VM->GetBindingCount() || Old.VM->GetSnapshot().World || Old.VM->GetSnapshot().Pawn)) ||
		(Old.HUD && (Old.HUD->GetExtensionWidgetCount() || Old.HUD->GetExtensionPointCount())) ||
		(Old.Menu && Old.Menu->IsActivated()) || (Old.Root && Old.Root->GetGameplayInputBlockCount()) ||
		(Old.Front && Old.Front->GetStateListenerCount()) || (Old.Modal && Old.Modal->GetStateListenerCount()))
	{ Fail(TEXT("World teardown retained input, abilities, UI, identity or pending work")); return; }
	for (UMiniHUDDataWidget* Widget : Old.Widgets) { if (Widget && (Widget->IsListening() || Widget->GetMessageListenerCount())) { Fail(TEXT("old data widget remained subscribed")); return; } }
	for (UMiniGameFeatureAction_AddActors* Action : Old.ActorActions)
	{
		int32 Worlds, Actors, Pending, Bindings; bool bReady, bFailedAction;
		Action->GetWorldStats(World, Worlds, Actors, Pending, Bindings, bReady, bFailedAction);
		if (Worlds || Actors || Pending || Bindings) { Fail(TEXT("old actor Action retained contributions or asynchronous work")); return; }
	}
	for (UMiniGameFeatureAction_AddWidgets* Action : Old.WidgetActions)
	{
		const FMiniWidgetContributionCounts C = Action->GetProbeContributionCounts(World);
		if (C.HUDs || C.Layouts || C.Elements || C.PendingLoads) { Fail(TEXT("old widget Action retained contributions or asynchronous work")); return; }
	}
	for (AMiniPracticeTarget* Target : Old.Targets)
	{
		if (!Target || Target->HasActorBegunPlay() || Target->GetStatusWidget() || Target->GetMiniAbilitySystemComponent()->GetOwnerActor() || Target->GetMiniAbilitySystemComponent()->GetAvatarActor())
		{ Fail(TEXT("old practice target retained lifecycle resources")); return; }
		Old.TargetResets.Add(Target->GetTargetState().ResetCount); Old.TargetDisables.Add(Target->GetTargetState().DisableCount);
	}
	if (Old.Supply && Old.Supply->HasActorBegunPlay()) { Fail(TEXT("old supply did not end")); return; }
	Old.SupplyRefills = Old.Supply ? Old.Supply->GetRefillCount() : -1;
	Old.VMRefreshes = Old.VM ? Old.VM->GetStateRefreshCount() : -1;
	Old.Magazine = Old.Item ? Old.Item->GetStat(MiniInventoryTags::AmmoInMagazine) : -1;
	Old.Reserve = Old.Item ? Old.Item->GetStat(MiniInventoryTags::ReserveAmmo) : -1;
	Old.PawnLocation = Old.Pawn ? Old.Pawn->GetActorLocation() : FVector::ZeroVector;
	Old.CleanupTime = FPlatformTime::Seconds(); Old.bCleaned = true; ++CleanupCount;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe OLD_WORLD_RELEASED: Cleanup=%d Peer=%s World=%s OldInput=0 OldHUD=0 OldWork=0 OldGrants=0 ActorContributions=0 WidgetContributions=0"), CleanupCount, *Peer, *World->GetName());
}
bool UMiniTask25ProbeSubsystem::OldStateSilent(bool bWaitDelay)
{
	if (!Old.bRetained || !Old.bCleaned || (bWaitDelay && FPlatformTime::Seconds() - Old.CleanupTime < 3.5)) { return false; }
	if ((Old.GM && (Old.GM->GetPendingRespawnCount() || Old.GM->GetPendingOutOfWorldRecoveryCount())) ||
		(Old.Pawn && (Old.Pawn->HasActorBegunPlay() || !Old.Pawn->GetActorLocation().Equals(Old.PawnLocation, 0.01))) ||
		(Old.PC && Old.PC->HasActorBegunPlay()) || (Old.PS && Old.PS->HasActorBegunPlay()) ||
		(Old.Hero && (Old.Hero->IsInputActive() || Old.Hero->GetInputBindingCount())) ||
		(Old.Equipment && !Old.Equipment->GetGrantedHandles().IsEmpty()) ||
		(Old.Item && (Old.Item->GetStat(MiniInventoryTags::AmmoInMagazine) != Old.Magazine || Old.Item->GetStat(MiniInventoryTags::ReserveAmmo) != Old.Reserve)) ||
		(Old.VM && (Old.VM->IsRunning() || Old.VM->GetBindingCount() || Old.VM->GetStateRefreshCount() != Old.VMRefreshes)) ||
		(Old.Menu && Old.Menu->IsActivated()) || (Old.Supply && Old.Supply->GetRefillCount() != Old.SupplyRefills))
	{ Fail(TEXT("a retained old object changed after World cleanup")); return false; }
	for (int32 I = 0; I < Old.Targets.Num(); ++I)
	{
		AMiniPracticeTarget* Target = Old.Targets[I];
		if (Target && (Target->GetTargetState().ResetCount != Old.TargetResets[I] || Target->GetTargetState().DisableCount != Old.TargetDisables[I]))
		{ Fail(TEXT("old target reset changed state after cleanup")); return false; }
	}
	for (UMiniHUDDataWidget* W : Old.Widgets) { if (W && (W->IsListening() || W->GetMessageListenerCount())) { Fail(TEXT("old widget resumed listening")); return false; } }
	// Root can already belong to the new FE/Modal. Its total gate count is checked by FrontEndReady, not required to remain zero here.
	return true;
}
bool UMiniTask25ProbeSubsystem::VerifyNewWorldIdentity()
{
	AMiniPlayerController* PC = Task25LocalPC(GetTickableGameObjectWorld());
	if (!Old.bRetained || !Old.bCleaned || !PC || !Task25PS(PC) || !Task25ASC(PC)) { return false; }
	if (GetTickableGameObjectWorld() == Old.World || FObjectKey(Task25PS(PC)) == Old.PSKey || FObjectKey(Task25ASC(PC)) == Old.ASCKey)
	{ Fail(TEXT("ordinary travel reused old World/PlayerState/ASC identity")); return false; }
	return true;
}
void UMiniTask25ProbeSubsystem::ReleaseOld() { Old = FMiniTask25OldWorld(); }
bool UMiniTask25ProbeSubsystem::PreparePracticeTravel()
{
	AMiniPlayerController* PC = Task25LocalPC(GetTickableGameObjectWorld()); AMiniCharacter* Character = Task25Pawn(PC);
	AMiniGameMode* GM = GetTickableGameObjectWorld()->GetAuthGameMode<AMiniGameMode>();
	AMiniPracticeTarget* Target = nullptr; AMiniPracticeSupply* Supply = nullptr;
	for (TActorIterator<AMiniPracticeTarget> It(GetTickableGameObjectWorld()); It; ++It) { if (!Target) { Target = *It; } }
	for (TActorIterator<AMiniPracticeSupply> It(GetTickableGameObjectWorld()); It; ++It) { Supply = *It; }
	FMiniDamageResult Damage;
	if (!GM || !Character || !Target || !Supply || !GM->TryApplyDamageToActor(Character, Target, 150.0f, Damage) ||
		!Damage.bTargetDefeated || Damage.TargetKind != EMiniDamageTargetKind::PracticeTarget || Target->GetTargetState().DisableCount != 1)
	{ Fail(TEXT("practice travel failed to arm real target reset")); return false; }
	// Setup only: empty existing items so a real overlap has observable refill work.
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		UMiniInventoryItemInstance* Item = PC->GetQuickBar()->GetSlotItem(Slot);
		if (!Item || !Item->SetStat(MiniInventoryTags::AmmoInMagazine, 0) || !Item->SetStat(MiniInventoryTags::ReserveAmmo, 0)) { Fail(TEXT("supply fixture preparation failed")); return false; }
	}
	Character->GetCharacterMovement()->StopMovementImmediately(); Character->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	Character->SetActorLocation(Supply->GetActorLocation() + FVector(0, 0, 96), false, nullptr, ETeleportType::TeleportPhysics);
	if (!Supply->GetSupplyArea()->IsOverlappingActor(Character) || Supply->GetRefillCount() < 1) { Fail(TEXT("supply travel fixture did not create actual overlap")); return false; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe PRACTICE_TRAVEL_ARMED: RealGE=1 TargetResetPending=1 SupplyOverlap=1 SetupAmmoOnly=1"));
	return true;
}
void UMiniTask25ProbeSubsystem::TickRoundTrip()
{
	UWorld* World = GetTickableGameObjectWorld(); const bool bFront = World->GetMapName().EndsWith(TEXT("L_MiniFrontEnd"));
	const bool bArena = World->GetMapName().EndsWith(TEXT("L_MiniArena"));
	if (bFront ? !FrontEndReady() : !GameplayReady(bArena)) { return; }
	if (Old.bRetained)
	{
		if (!OldStateSilent() || !VerifyNewWorldIdentity()) { return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe OLD_STATE_SILENT: Cleanup=%d WaitSeconds=3.5 OldInput=0 OldHUD=0 OldWork=0 OldItemChanges=0"), CleanupCount);
		ReleaseOld();
	}
	if (!bFront && !Media(FName(*FString::Printf(TEXT("%s%d"), bArena ? TEXT("Arena") : TEXT("Practice"), bArena ? ArenaCount + 1 : PracticeCount + 1)))) { return; }
	const FObjectKey WorldKey(World); if (SeenWorlds.Contains(WorldKey)) { return; }
	AMiniPlayerController* PC = Task25LocalPC(World);
	if (SeenPlayerStates.Contains(FObjectKey(Task25PS(PC))) || SeenASCs.Contains(FObjectKey(Task25ASC(PC)))) { Fail(TEXT("round-trip World reused a previous PlayerState or ASC")); return; }
	SeenWorlds.Add(WorldKey); SeenPlayerStates.Add(FObjectKey(Task25PS(PC))); SeenASCs.Add(FObjectKey(Task25ASC(PC)));
	++WorldsCount; PracticeCount += !bFront && !bArena ? 1 : 0; ArenaCount += bArena ? 1 : 0;
	const TCHAR* Kind = bFront ? TEXT("FrontEnd") : bArena ? TEXT("Arena") : TEXT("Practice");
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe WORLD_READY: Generation=%d Kind=%s NewPlayerState=1 NewASC=1 LoadedLateOnce=1"), WorldsCount, Kind);
	if (WorldsCount == 15)
	{
		if (!bFront || CleanupCount != 14 || PracticeCount != 4 || ArenaCount != 3) { Fail(TEXT("three complete practice/arena round trips had incorrect World counts")); return; }
		Pass(TEXT("ROUNDTRIP_PASS"), TEXT("Cycles=3 Cleanups=14 Worlds=15 Practice=4 Arena=3 OldInput=0 OldHUD=0 OldWork=0")); return;
	}
	// FE,P,[FE,A,FE,P] x3,FE: every actual World is verified and every outgoing World is retained.
	const bool bNextArena = bFront && WorldsCount % 4 == 3;
	if (!bFront && !bArena && !PreparePracticeTravel()) { return; }
	if (!RetainWorld(!bFront)) { return; }
	UMiniTravelSubsystem* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
	bool bAccepted = true;
	if (bFront) { bAccepted = bNextArena ? Travel->HostArena() : Travel->StartPractice(); }
	else { Travel->ReturnToFrontEnd(); }
	if (!bAccepted) { Fail(TEXT("production round-trip Travel request rejected")); }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe TRAVEL_REQUEST: FromGeneration=%d Kind=%s ProductionAPI=1 ButtonProof=Task24"), WorldsCount, Kind);
}
bool UMiniTask25ProbeSubsystem::Click(UWidget* Widget, FName Action)
{
	if (!FSlateApplication::IsInitialized() || !Widget || !Widget->IsVisible() || !Widget->GetIsEnabled() || !Widget->GetCachedWidget().IsValid()) { return false; }
	const FGeometry& Geometry = Widget->GetCachedGeometry(); if (Geometry.GetLocalSize().X < 2 || Geometry.GetLocalSize().Y < 2) { return false; }
	FSlateApplication& Slate = FSlateApplication::Get(); TSharedPtr<SWindow> Window = Slate.FindWidgetWindow(Widget->GetCachedWidget().ToSharedRef());
	if (!Window.IsValid() || !Window->GetNativeWindow().IsValid()) { return false; }
	// Multiple independent peers cannot all own OS foreground focus. Restore the
	// normal background-input policy immediately after this opt-in mouse sequence.
	const bool bPreviousBackgroundInput = Slate.GetHandleDeviceInputWhenApplicationNotActive();
	Slate.SetHandleDeviceInputWhenApplicationNotActive(true);
	ON_SCOPE_EXIT { Slate.SetHandleDeviceInputWhenApplicationNotActive(bPreviousBackgroundInput); };
	const FVector2D At = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f), Previous = Slate.GetCursorPos();
	Slate.SetCursorPos(At); TSet<FKey> Pressed;
	Slate.ProcessMouseMoveEvent(FPointerEvent(FSlateApplication::CursorPointerIndex, At, Previous, Pressed, EKeys::Invalid, 0, FModifierKeysState()));
	if (Cast<UButton>(Widget) && !Widget->IsHovered()) { return false; }
	Pressed.Add(EKeys::LeftMouseButton);
	const bool bDown = Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), FPointerEvent(FSlateApplication::CursorPointerIndex, At, At, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState()));
	const UButton* Button = Cast<UButton>(Widget);
	const bool bTargetPressed = !Button || Button->IsPressed();
	const bool bTargetCaptured = !Button || Widget->GetCachedWidget()->HasMouseCapture();
	Pressed.Reset(); const bool bUp = Slate.ProcessMouseButtonUpEvent(FPointerEvent(FSlateApplication::CursorPointerIndex, At, At, Pressed, EKeys::LeftMouseButton, 0, FModifierKeysState()));
	if (!bDown || !bUp) { Fail(TEXT("routed Slate click was not handled")); return false; }
	if (!bTargetPressed || !bTargetCaptured) { LogWait(TEXT("SlateTargetPressedAndCaptured")); return false; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe UI_CLICK: Peer=%s Action=%s MouseMove=1 TargetHover=1 TargetPressed=1 TargetCapture=1 Down=1 Up=1 AppActive=%d ProbeBackgroundCapture=1"), *Peer, *Action.ToString(), Slate.IsActive() ? 1 : 0); return true;
}
bool UMiniTask25ProbeSubsystem::Media(FName Stage)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask25Media")) || MediaDone.Contains(Stage)) { return true; }
	if (!PendingMediaStage.IsNone())
	{
		if (PendingMediaStage != Stage) { return false; }
		if (IFileManager::Get().FileSize(*PendingMediaPath) > 1024)
		{
			MediaDone.Add(Stage); PendingMediaStage = NAME_None;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe SCREENSHOT_SAVED: Peer=%s Stage=%s Path=%s"), *Peer, *Stage.ToString(), *PendingMediaPath); return true;
		}
		if (FPlatformTime::Seconds() - MediaStartedAt > 30) { Fail(TEXT("actual rendered screenshot was not saved")); }
		return false;
	}
#if WITH_EDITOR
	if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { return false; }
#endif
	IFileManager::Get().MakeDirectory(*MediaDirectory, true);
	PendingMediaPath = FPaths::Combine(MediaDirectory, FString::Printf(TEXT("Task25-%s-%s-%s.png"), *Mode, *Peer, *Stage.ToString()));
	IFileManager::Get().Delete(*PendingMediaPath, false, true); PendingMediaStage = Stage; MediaStartedAt = FPlatformTime::Seconds();
	FScreenshotRequest::RequestScreenshot(PendingMediaPath, true, false); return false;
}
bool UMiniTask25ProbeSubsystem::WriteSignal(const TCHAR* Name) const
{
	if (SignalDirectory.IsEmpty()) { return false; }
	IFileManager::Get().MakeDirectory(*SignalDirectory, true);
	return FFileHelper::SaveStringToFile(TEXT("Actual lifecycle checkpoint observed."), *FPaths::Combine(SignalDirectory, FString(Name) + TEXT(".signal")));
}
void UMiniTask25ProbeSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* Driver, int32 Failure, const FString& Reason)
{
	if (!bInitialized || !World || World->GetGameInstance() != GetGameInstance()) { return; }
	++NetworkFailures;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe REAL_NETWORK_FAILURE: Peer=%s Type=%d Driver=%s Armed=%d Reason=%s"), *Peer, Failure, *GetNameSafe(Driver), bHostLossArmed ? 1 : 0, *Reason);
	if (Mode != TEXT("Stress") || !bHostLossArmed) { Fail(TEXT("unexpected real network failure before host-loss checkpoint")); }
}

void UMiniTask25ProbeSubsystem::TickClient()
{
	UWorld* World = GetTickableGameObjectWorld(); AMiniPlayerController* PC = Task25LocalPC(World);
	UMiniTravelSubsystem* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
	if (!PC || !Travel) { return; }
	if (World->GetNetMode() == NM_Client)
	{
		if (Old.bRetained && World != Old.World && !bHostLossArmed)
		{
			if (!GameplayReady(true) || !OldStateSilent() || !VerifyNewWorldIdentity()) { return; }
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe REJOIN_OLD_FRONT_RELEASED: Peer=%s OldInput=0 OldHUD=0 NewWorldPSASC=1"), *Peer);
			ReleaseOld();
		}
		if (ClientProbeWorld.Get() != World)
		{
			ClientProbeWorld = World; LastClientSerial = 0; LocalLife = FMiniTask25OwnerRecord(); LocalLife.PC = PC;
			bMovePressed = false; bMoveVerified = false; ArmedSerials.Reset(); ArmStep = 0;
		}
		AMiniTask25ProbeActor* Probe = LocalProbe(); if (!Probe) { return; }
		const bool bAck = Acknowledge(Probe);
		const EMiniTask25Command Command = Probe->GetCommand();
		if (!bLeaveRequested && (Command == EMiniTask25Command::LeaveAndRejoin || Command == EMiniTask25Command::LeaveOnly))
		{
			if (Mode == TEXT("Stress") && (ClientDeaths != 10 || ClientRespawns != 10)) { Fail(TEXT("leave command arrived before all ten owner death/respawn cycles")); return; }
			if (!RetainWorld(true)) { return; }
			bLeaveRequested = true; Travel->ReturnToFrontEnd();
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe CLIENT_LEAVE_REQUEST: Peer=%s ProductionAPI=1 ActualClientTravelLogout=1"), *Peer); return;
		}
		if (bAck && Probe->GetCheckpointName() == TEXT("Rejoined") && !bHostLossArmed)
		{
			if (ClientDeaths != 10 || ClientRespawns != 10 || !RetainWorld(false)) { Fail(TEXT("host loss was armed with incomplete owner lifecycle or observations")); return; }
			bHostLossArmed = true;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe HOST_LOSS_ARMED: Peer=%s Deaths=10 Respawns=10"), *Peer); return;
		}
		if (Mode == TEXT("RecoveryDeath") && Probe->GetCheckpointName() == TEXT("RecoveryRespawned") &&
			Probe->GetSerial() > 0 && LastClientSerial == Probe->GetSerial())
		{
			FMiniTask25Observation O;
			if (!Task25PS(PC) || Task25PS(PC) != Probe->GetExpectedPlayerState() || Task25Pawn(PC) != Probe->GetExpectedPawn() ||
				Task25PS(PC)->GetCurrentLifeId() != Probe->GetExpectedLife() || !MiniTask25SamePhase(Task25Phase(World), Probe->GetExpectedPhase()) ||
				!MiniTask25SameMatch(Task25Match(World), Probe->GetExpectedMatch()) || !ObserveOwner(PC, false, true, O)) { return; }
			if (!Media(TEXT("RecoveryRespawned"))) { return; }
			Pass(TEXT("RECOVERY_DEATH_CLIENT_PASS"), TEXT("SameASC=1 NewLifeReady=1")); return;
		}
		return;
	}
	if (!bLeaveRequested && !bHostLossArmed) { return; } // Temporary preconnection entry World.
	if (bHostLossArmed)
	{
		if (!FrontEndReady(!bDismissRequested) || !OldStateSilent() || !VerifyNewWorldIdentity()) { return; }
		if (!bDismissRequested)
		{
			if (NetworkFailures != 1 || (Travel->GetState().FailureCode != TEXT("MINI_CONNECTION_LOST") && Travel->GetState().FailureCode != TEXT("MINI_CONNECTION_TIMEOUT")))
			{ Fail(TEXT("host-loss recovery did not originate from exactly one actual driver failure")); return; }
			if (!Media(TEXT("HostLossError"))) { return; }
			if (FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask25Media")))
			{
				UMiniConnectionStatusWidget* Modal = Travel->GetConnectionStatusWidget(); if (!Modal || !Click(Modal->GetButton(TEXT("CloseButton")), TEXT("CloseButton"))) { return; }
			}
			else { Travel->DismissError(); } // Production API; Task24 separately verifies its UI routing.
			if (Travel->GetState().bHasError) { Fail(TEXT("actual Close action did not dismiss the host-loss error")); return; }
			bDismissRequested = true; return;
		}
		if (ClientDeaths != 10 || ClientRespawns != 10) { Fail(TEXT("host-loss final owner cycle totals incorrect")); return; }
		ReleaseOld(); Pass(TEXT("STRESS_CLIENT_PASS"), TEXT("Deaths=10 Respawns=10 HostLoss=1")); return;
	}
	if (!FrontEndReady() || !OldStateSilent() || !VerifyNewWorldIdentity()) { return; }
	if (Mode == TEXT("RecoveryLogout"))
	{
		if (!Media(TEXT("LogoutFrontEnd"))) { return; }
		ReleaseOld(); Pass(TEXT("RECOVERY_LOGOUT_CLIENT_PASS"), TEXT("NewFrontEnd=1 OldHUD=0")); return;
	}
	if (Mode == TEXT("Stress") && !bRejoinRequested)
	{
		ReleaseOld(); if (!RetainWorld(false)) { return; }
		if (!Travel->JoinAddress(Address)) { Fail(TEXT("production reconnect request rejected")); return; }
		bRejoinRequested = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe CLIENT_REJOIN_REQUEST: Peer=%s Address=%s ProductionAPI=1"), *Peer, *Address); return;
	}
}
void UMiniTask25ProbeSubsystem::MakeSpawnBlockers()
{
	if (SpawnBlockers.Num()) { return; }
	for (TActorIterator<APlayerStart> It(GetTickableGameObjectWorld()); It; ++It)
	{
		FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
		AActor* Blocker = GetTickableGameObjectWorld()->SpawnActor<AActor>(AActor::StaticClass(), It->GetActorLocation(), FRotator::ZeroRotator, Params);
		if (!Blocker) { Fail(TEXT("spawn blocker fixture failed")); return; }
		UBoxComponent* Box = NewObject<UBoxComponent>(Blocker); Blocker->SetRootComponent(Box); Blocker->AddInstanceComponent(Box);
		Box->SetBoxExtent(FVector(110, 110, 180)); Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
		Blocker->SetActorLocation(It->GetActorLocation()); SpawnBlockers.Add(Blocker);
	}
	if (SpawnBlockers.Num() != 8) { Fail(TEXT("did not block exactly eight real PlayerStarts")); }
}
void UMiniTask25ProbeSubsystem::RemoveSpawnBlockers()
{
	for (AActor* Actor : SpawnBlockers) { if (IsValid(Actor)) { Actor->Destroy(); } } SpawnBlockers.Reset();
}
bool UMiniTask25ProbeSubsystem::BeginPausedFall(AMiniPlayerController* PC)
{
	if (!Task25Live(PC)) { return false; }
	RecoveryPC = PC; RecoveryPawn = Task25Pawn(PC); RecoveryPS = Task25PS(PC); RecoveryASC = Task25ASC(PC);
	RecoveryEquipment = Task25Equipment(PC); RecoveryLife = RecoveryPS->GetCurrentLifeId(); RecoverySafeLocation = RecoveryPawn->GetActorLocation();
	MakeSpawnBlockers(); if (bFailed) { return false; }
	RecoveryPawn->GetCharacterMovement()->StopMovementImmediately();
	if (!RecoveryPawn->TeleportTo(FVector(1700, 1325, GetTickableGameObjectWorld()->GetWorldSettings()->KillZ - 200), RecoveryPawn->GetActorRotation(), false, true))
	{ Fail(TEXT("real KillZ fall fixture teleport failed")); return false; }
	RecoveryPawn->GetCharacterMovement()->SetMovementMode(MOVE_Falling); RecoveryPawn->ForceNetUpdate();
	StepStartedAt = FPlatformTime::Seconds();
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe RECOVERY_FALL_ARMED: Mode=%s PlayerId=%d Life=%u StartsBlocked=8 RealMovementTick=1"), *Mode, RecoveryPS->GetPlayerId(), RecoveryLife);
	return true;
}
bool UMiniTask25ProbeSubsystem::RevokeActualAction()
{
	UWorld* World = GetTickableGameObjectWorld(); UGameFeatureAction* Target = nullptr;
	for (UGameFeatureAction* Action : Task25Actions(World)) { if (Action && Action->GetFName() == TEXT("MiniArena_AddRules")) { Target = Action; } }
	const FWorldContext* Context = GEngine ? GEngine->GetWorldContextFromWorld(World) : nullptr;
	if (!Target || !Context) { Fail(TEXT("actual assembled Arena rules Action missing")); return false; }
	FGameFeatureDeactivatingContext Deactivation(TEXT("MiniTask25Probe"), [](FStringView) {});
	Deactivation.SetRequiredWorldContextHandle(Context->ContextHandle); Target->OnGameFeatureDeactivating(Deactivation);
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe RECOVERY_ACTION_REVOKED: ActualStockAction=1 WorldContext=%s NoPreCancel=1"), *Context->ContextHandle.ToString());
	return true;
}
void UMiniTask25ProbeSubsystem::TickRecovery()
{
	UWorld* World = GetTickableGameObjectWorld(); UMiniTravelSubsystem* Travel = GetGameInstance()->GetSubsystem<UMiniTravelSubsystem>();
	const double Now = FPlatformTime::Seconds();
	if (Mode == TEXT("RecoveryTravel") && Step == 4)
	{
		if (!FrontEndReady() || !OldStateSilent() || !VerifyNewWorldIdentity() || !Media(TEXT("RecoveryFrontEnd"))) { return; }
		ReleaseOld(); Pass(TEXT("RECOVERY_TRAVEL_PASS"), TEXT("ActualWorldCleanup=1 OldWork=0 NewFrontEnd=1")); return;
	}
	if (World->GetNetMode() != NM_ListenServer || !Task25Manager(World) || !Task25Manager(World)->IsExperienceLoaded() || !VerifyLateLoaded()) { return; }
	AMiniGameMode* GM = World->GetAuthGameMode<AMiniGameMode>(); AMiniGameState* GS = World->GetGameState<AMiniGameState>(); if (!GM || !GS) { return; }
	const bool bNeedsRemote = Mode == TEXT("RecoveryLogout") || Mode == TEXT("RecoveryDeath");
	if (Step == 0 || Step == 10)
	{
		if (!GameplayReady(true) || (bNeedsRemote && Task25Phase(World).PhaseTag != MiniGameplayTags::GamePhase_MiniArena_Playing)) { return; }
		DiscoverOwners(); AMiniPlayerController* Victim = Task25LocalPC(World);
		if (bNeedsRemote)
		{
			if (Task25Match(World).ConnectedPlayerCount != 2) { return; }
			FMiniTask25OwnerRecord* Remote = Owners.FindByPredicate([](const FMiniTask25OwnerRecord& R) { return R.Index == 1; });
			if (!Remote || !Task25Live(Remote->PC)) { return; } Victim = Remote->PC;
			if (Step == 10 && !CaptureLife(*Remote, 0)) { return; }
			if (!Task25ASC(Victim)->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected))
			{
				if (Step == 10) { Fail(TEXT("fresh recovery victim missed the natural protection window")); return; }
				RecoveryPawn = Task25Pawn(Victim);
				Remote->DeadPawn = RecoveryPawn; Remote->DeadEquipment = Task25Equipment(Victim);
				Remote->DeadItem = Remote->CurrentItem; Remote->bFrozenAmmo = false;
				if (!GM->TryApplyEnvironmentDamage(RecoveryPawn, Task25PS(Victim)->GetHealthSet()->GetHealth())) { return; }
				Step = 10; return;
			}
		}
		if (!BeginPausedFall(Victim)) { return; } Step = 1; return;
	}
	if (Step == 1)
	{
		if (!RecoveryPawn || Task25Pawn(RecoveryPC) != RecoveryPawn || RecoveryPawn->GetHealthComponent()->IsDead() || RecoveryPS->GetCurrentLifeId() != RecoveryLife)
		{ Fail(TEXT("paused fall changed the protected/Warmup life")); return; }
		if (GM->GetPendingOutOfWorldRecoveryCount() != 1 || RecoveryPawn->GetCharacterMovement()->MovementMode != MOVE_None) { return; }
		if (!RecoveryPawn->GetCharacterMovement()->IsComponentTickEnabled()) { Fail(TEXT("live paused recovery lost Movement Tick")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask25Probe RECOVERY_PAUSED: Mode=%s Pending=1 SameLife=1 Alive=1 MovementNone=1 MovementTick=1"), *Mode);
		if (Mode == TEXT("RecoveryAction"))
		{
			if (!RevokeActualAction()) { return; }
			if (GM->GetPendingOutOfWorldRecoveryCount() || RecoveryPawn->GetCharacterMovement()->MovementMode == MOVE_None || !RecoveryPawn->GetCharacterMovement()->IsComponentTickEnabled())
			{ Fail(TEXT("actual Action revoke did not release live movement pause")); return; }
			RemoveSpawnBlockers();
			// Return this same live Pawn to safe ground before its next movement tick can create NEW fall work.
			RecoveryPawn->SetActorLocation(RecoverySafeLocation, false, nullptr, ETeleportType::TeleportPhysics);
			RecoveryPawn->GetCharacterMovement()->StopMovementImmediately(); StepStartedAt = Now; Step = 3; return;
		}
		if (Mode == TEXT("RecoveryTravel"))
		{
			if (!RetainWorld(true)) { return; } Travel->ReturnToFrontEnd(); Step = 4; return;
		}
		if (!Checkpoint(TEXT("RecoveryPaused"), 1, false, true)) { return; }
		FMiniTask25OwnerRecord* Remote = Owners.FindByPredicate([](const FMiniTask25OwnerRecord& R) { return R.Index == 1; });
		if (Mode == TEXT("RecoveryLogout")) { Remote->Probe->SetCommand(EMiniTask25Command::LeaveOnly); StepStartedAt = Now; Step = 2; return; }
		Step = 5; return;
	}
	if (Step == 2)
	{
		if (Task25Match(World).ConnectedPlayerCount != 1 || GM->GetPendingOutOfWorldRecoveryCount() || GM->GetPendingRespawnCount() ||
			(RecoveryPC && RecoveryPC->HasActorBegunPlay()) || (RecoveryPawn && RecoveryPawn->HasActorBegunPlay())) { return; }
		if (Now - StepStartedAt < 3.5) { return; }
		for (const FMiniTask25OwnerRecord& R : Owners) { if (R.Index == 0 && (Task25PS(R.PC) != R.PS || Task25ASC(R.PC) != R.ASC)) { Fail(TEXT("recovery Logout replaced host identity")); return; } }
		RemoveSpawnBlockers(); Pass(TEXT("RECOVERY_LOGOUT_SERVER_PASS"), TEXT("ActualLogout=1 OldWork=0 OldLifeRespawn=0")); return;
	}
	if (Step == 3)
	{
		UMiniGamePhaseSubsystem* Phases = World->GetSubsystem<UMiniGamePhaseSubsystem>(); UMiniMatchSubsystem* Matches = World->GetSubsystem<UMiniMatchSubsystem>();
		int32 PhaseSpecs = 0; for (const FGameplayAbilitySpec& Spec : GS->GetPhaseAbilitySystemComponent()->GetActivatableAbilities()) { PhaseSpecs += Spec.Ability && Spec.Ability->IsA<UMiniGamePhaseAbility>() ? 1 : 0; }
		if (GM->GetPendingOutOfWorldRecoveryCount() || GM->GetPendingRespawnCount() || !RecoveryPawn || RecoveryPawn->GetHealthComponent()->IsDead() ||
			Task25Pawn(RecoveryPC) != RecoveryPawn || RecoveryPS->GetCurrentLifeId() != RecoveryLife || RecoveryPawn->GetCharacterMovement()->MovementMode == MOVE_None ||
			!RecoveryPawn->GetCharacterMovement()->IsComponentTickEnabled() || GS->FindComponentByClass<UMiniArenaRulesComponent>() || GS->FindComponentByClass<UMiniMatchRulesComponent>() ||
			Phases->HasArenaContext() || Phases->HasPendingPhase() || Matches->HasMatchContext() || PhaseSpecs)
		{ Fail(TEXT("actual Action teardown left old work, rule sources, phase specs or restored corpse movement")); return; }
		if (Now - StepStartedAt < 3.5) { return; }
		if (!RecoveryPawn->GetActorLocation().Equals(RecoverySafeLocation, 5.0)) { Fail(TEXT("cancelled recovery moved the same live Pawn after Action removal")); return; }
		if (!Media(TEXT("RecoveryAction"))) { return; }
		RemoveSpawnBlockers(); Pass(TEXT("RECOVERY_ACTION_PASS"), TEXT("ActualAction=1 OldWork=0 LiveMovementRestored=1")); return;
	}
	if (Step == 5)
	{
		// Let the actual two-second protection GE expire, rather than removing tags/effects.
		if (RecoveryASC->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected)) { return; }
		if (GM->GetPendingOutOfWorldRecoveryCount() != 1 || !GM->TryApplyEnvironmentDamage(RecoveryPawn, RecoveryPS->GetHealthSet()->GetHealth()) ||
			!RecoveryPawn->GetHealthComponent()->IsDead() || GM->GetPendingOutOfWorldRecoveryCount() ||
			RecoveryPawn->GetCharacterMovement()->MovementMode != MOVE_None || RecoveryPawn->GetCharacterMovement()->IsComponentTickEnabled())
		{ Fail(TEXT("real GE death restored the cancelled live recovery pause onto a corpse")); return; }
		FMiniTask25OwnerRecord* Remote = Owners.FindByPredicate([](const FMiniTask25OwnerRecord& R) { return R.Index == 1; });
		Remote->DeadPawn = RecoveryPawn; Remote->DeadEquipment = RecoveryEquipment; Remote->DeadItem = Remote->CurrentItem; Remote->bFrozenAmmo = false;
		Iteration = 1; bPublished = false; Step = 6; return;
	}
	if (Step == 6)
	{
		if (!Checkpoint(TEXT("RecoveryDead"), 1, true, false)) { return; }
		RemoveSpawnBlockers(); bPublished = false; Step = 7; return;
	}
	if (Step == 7)
	{
		if (!Task25Live(RecoveryPC) || Task25Pawn(RecoveryPC) == RecoveryPawn || RecoveryPS->GetCurrentLifeId() != RecoveryLife + 1 ||
			Task25ASC(RecoveryPC) != RecoveryASC || RecoveryASC->GetAvatarActor() != Task25Pawn(RecoveryPC) || GM->GetPendingRespawnCount() || GM->GetPendingOutOfWorldRecoveryCount()) { return; }
		FMiniTask25OwnerRecord* Remote = Owners.FindByPredicate([](const FMiniTask25OwnerRecord& R) { return R.Index == 1; });
		if (!CaptureLife(*Remote, 1) || !Checkpoint(TEXT("RecoveryRespawned"), 1)) { return; }
		Pass(TEXT("RECOVERY_DEATH_PASS"), TEXT("RealGE=1 OldWork=0 CorpseMovementNone=1 CorpseTick=0 NewLifeReady=1")); return;
	}
}
