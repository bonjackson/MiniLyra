#include "MiniTask21ProbeSubsystem.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Arena/MiniArenaPhaseConfig.h"
#include "Arena/MiniArenaRulesComponent.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHeroComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/MiniEquipmentInstance.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Equipment/MiniQuickBarComponent.h"
#include "GameFeatureAction.h"
#include "GameFeaturesSubsystem.h"
#include "GameModes/MiniExperienceActionSet.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGamePhaseAbility.h"
#include "GameModes/MiniGamePhaseSubsystem.h"
#include "GameModes/MiniGameState.h"
#include "HAL/FileManager.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Inventory/MiniInventoryManagerComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "Practice/MiniPracticeSupply.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"
#include "Training/MiniPracticeTarget.h"
#include "UI/MiniHUDLayout.h"
#include "UI/MiniHUDViewModel.h"
#include "UI/MiniPrimaryGameLayout.h"
#include "UnrealClient.h"
#include "Widgets/CommonActivatableWidgetContainer.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif

namespace
{
AMiniPlayerController* Task21LocalController(UWorld* World)
{
	for (TActorIterator<AMiniPlayerController> It(World); It; ++It)
	{
		if (It->IsLocalController()) { return *It; }
	}
	return nullptr;
}

UMiniHUDViewModel* Task21HUD(AMiniPlayerController* PC)
{
	UMiniPrimaryGameLayout* Root = PC && PC->GetLocalPlayer()
		? Cast<UMiniPrimaryGameLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(PC->GetLocalPlayer())) : nullptr;
	UCommonActivatableWidgetContainerBase* Layer = Root ? Root->GetLayerWidget(Root->GetGameLayerTag()) : nullptr;
	if (!Layer) { return nullptr; }
	for (UCommonActivatableWidget* Widget : Layer->GetWidgetList())
	{
		if (UMiniHUDLayout* HUD = Cast<UMiniHUDLayout>(Widget)) { return HUD->GetViewModel(); }
	}
	return nullptr;
}

bool Task21PlayerReady(AMiniPlayerController* PC)
{
	AMiniCharacter* Pawn = PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr;
	const AMiniPlayerState* PS = PC ? PC->GetPlayerState<AMiniPlayerState>() : nullptr;
	const UMiniAbilitySystemComponent* ASC = PS ? PS->GetMiniAbilitySystemComponent() : nullptr;
	return Pawn && ASC && ASC->GetOwnerActor() == PS && ASC->GetAvatarActor() == Pawn &&
		Pawn->GetHeroComponent() && (!PC->IsLocalController() || Pawn->GetHeroComponent()->IsInputActive()) &&
		Pawn->GetEquipmentManager() && Pawn->GetEquipmentManager()->GetCurrentEquipment();
}

bool Task21PlayersHaveNoPhase(UWorld* World, const UMiniAbilitySystemComponent* PhaseASC)
{
	for (TActorIterator<AMiniPlayerState> It(World); It; ++It)
	{
		const UMiniAbilitySystemComponent* ASC = It->GetMiniAbilitySystemComponent();
		if (!ASC || ASC == PhaseASC) { return false; }
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			if (Spec.Ability && Spec.Ability->IsA<UMiniGamePhaseAbility>()) { return false; }
		}
	}
	return true;
}

int32 Task21TimerCount(UMiniAbilitySystemComponent* ASC)
{
	int32 Count = 0;
	if (ASC)
	{
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			const UMiniGamePhaseAbility* Phase = Cast<UMiniGamePhaseAbility>(Spec.GetPrimaryInstance());
			Count += Phase && Phase->HasPhaseTimer() ? 1 : 0;
		}
	}
	return Count;
}

UMiniGamePhaseAbility* Task21ActiveAbility(UMiniAbilitySystemComponent* ASC)
{
	if (!ASC) { return nullptr; }
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (UMiniGamePhaseAbility* Phase = Cast<UMiniGamePhaseAbility>(Spec.GetPrimaryInstance())) { return Phase; }
	}
	return nullptr;
}

int32 Task21PhaseIndex(FGameplayTag Tag)
{
	if (Tag == MiniGameplayTags::GamePhase_MiniArena_Warmup) { return 1; }
	if (Tag == MiniGameplayTags::GamePhase_MiniArena_Playing) { return 2; }
	if (Tag == MiniGameplayTags::GamePhase_MiniArena_PostMatch) { return 3; }
	return 0;
}

bool Task21SameState(const FMiniGamePhaseState& A, const FMiniGamePhaseState& B)
{
	return A.PhaseTag == B.PhaseTag && A.Revision == B.Revision &&
		FMath::Abs(A.PhaseStartTimeServer - B.PhaseStartTimeServer) < 0.01 &&
		FMath::Abs(A.PhaseEndTimeServer - B.PhaseEndTimeServer) < 0.01;
}
}

bool UMiniTask21ProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	FString Mode;
	return Super::ShouldCreateSubsystem(Outer) && FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask21="), Mode);
#else
	return false;
#endif
}

TStatId UMiniTask21ProbeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask21ProbeSubsystem, STATGROUP_Tickables);
}

void UMiniTask21ProbeSubsystem::Fail(const TCHAR* Reason)
{
	bDone = true;
	UE_LOG(LogMiniInit, Error, TEXT("MiniTask21Probe FAIL: Reason=%s"), Reason);
}

void UMiniTask21ProbeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bDone || !GetWorld() || !GetWorld()->HasBegunPlay()) { return; }
	WaitSeconds += DeltaTime;
	if (WaitSeconds > 100) { Fail(TEXT("phase acceptance timed out")); return; }
	FString Mode;
	FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask21="), Mode);
	if (Mode == TEXT("Practice")) { TickPractice(); }
	else if (Mode == TEXT("Core") || Mode == TEXT("Cancel"))
	{
		if (GetWorld()->GetNetMode() == NM_Client) { TickClient(); }
		else { TickServer(Mode); }
	}
	else { Fail(TEXT("unknown Task21 probe mode")); }
}

bool UMiniTask21ProbeSubsystem::AllAcknowledged(EMiniTask21Checkpoint Checkpoint) const
{
	if (OwnerProbes.Num() != 2) { return false; }
	for (const TWeakObjectPtr<AMiniTask21ProbeActor>& Probe : OwnerProbes)
	{
		if (!Probe.IsValid() || !Probe->HasAcknowledged(Checkpoint)) { return false; }
	}
	return true;
}

void UMiniTask21ProbeSubsystem::PublishCheckpoint(EMiniTask21Checkpoint Checkpoint)
{
	const FMiniGamePhaseState State = GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>()->GetCurrentPhaseState();
	for (const TWeakObjectPtr<AMiniTask21ProbeActor>& Probe : OwnerProbes)
	{
		if (Probe.IsValid()) { Probe->SetCheckpoint(Checkpoint, State); }
	}
}

void UMiniTask21ProbeSubsystem::TickServer(const FString& Mode)
{
	UWorld* World = GetWorld();
	AMiniGameState* GS = World->GetGameState<AMiniGameState>();
	UMiniGamePhaseSubsystem* Phases = World->GetSubsystem<UMiniGamePhaseSubsystem>();
	UMiniArenaRulesComponent* Rules = GS ? GS->FindComponentByClass<UMiniArenaRulesComponent>() : nullptr;
	UMiniAbilitySystemComponent* ASC = GS ? GS->GetPhaseAbilitySystemComponent() : nullptr;
	if (!GS || !Phases || !ASC) { return; }
	if (ASC->GetOwnerActor() != GS || ASC->GetAvatarActor() != GS || !Task21PlayersHaveNoPhase(World, ASC))
	{ Fail(TEXT("phase ASC owner/avatar differs from GameState or player has phase ability")); return; }
	const FMiniGamePhaseState State = Phases->GetCurrentPhaseState();
	const int32 Index = Task21PhaseIndex(State.PhaseTag);
	if (ServerStep < 2 && (!Rules || !Rules->PhaseConfig || Rules->PhaseConfig->GetName() != TEXT("DA_MiniArenaDiagnosticsPhaseConfig"))) { return; }
	if (!bReadyLogged)
	{
		if (Index != 1 || !Task21PlayerReady(Task21LocalController(World))) { return; }
		UMiniHUDViewModel* VM = Task21HUD(Task21LocalController(World));
		if (!VM || !VM->IsRunning() || VM->GetSnapshot().PhaseTag != State.PhaseTag) { return; }
		bReadyLogged = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask21Probe SERVER_READY: Warmup=8 Playing=20 PostMatch=3 GameStateASCIndependent=1 PlayerPhaseSpecs=0 HostHUD=1"));
	}
	if (Mode == TEXT("Cancel"))
	{
		if (ServerStep == 0)
		{
			if (Index != 1 || ASC->GetActivatableAbilities().Num() != 1 || Task21TimerCount(ASC) != 1) { return; }
			FGameplayAbilitySpecHandle Duplicate;
			const FGameplayAbilitySpecHandle Original = Phases->GetActivePhaseHandle();
			if (!Phases->StartPhase(Rules, UMiniGamePhaseAbility_Warmup::StaticClass(), 99.0f, FOnMiniGamePhaseComplete(), Duplicate) ||
				Duplicate != Original || !Task21SameState(State, Phases->GetCurrentPhaseState()))
			{ Fail(TEXT("same phase request reset deadline or granted another spec")); return; }
			FGameplayAbilitySpecHandle Invalid;
			if (Phases->StartPhase(Rules, UMiniGamePhaseAbility_Playing::StaticClass(), -1.0f, FOnMiniGamePhaseComplete(), Invalid) ||
				Invalid.IsValid() || !Task21SameState(State, Phases->GetCurrentPhaseState()))
			{ Fail(TEXT("invalid phase request disturbed existing phase")); return; }
			CancelledDeadline = State.PhaseEndTimeServer;
			CancelledAbility = Task21ActiveAbility(ASC);
			if (!Phases->CancelPhaseForSource(Rules)) { Fail(TEXT("authority cancel rejected active source")); return; }
			CancelledRevision = Phases->GetCurrentPhaseState().Revision;
			ServerStep = 1;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask21Probe SERVER_CANCEL: BeforeSpecs=1 BeforeTimer=1 SameTagIdempotent=1 InvalidRequestPreservesPhase=1 OldDeadline=%.3f"), CancelledDeadline);
			return;
		}
		if (State.PhaseTag.IsValid() || ASC->GetActivatableAbilities().Num() || Task21TimerCount(ASC) ||
			(CancelledAbility.IsValid() && CancelledAbility->HasPhaseTimer()) || Phases->HasPendingPhase() ||
			State.Revision != CancelledRevision)
		{ Fail(TEXT("cancel retained phase/spec/timer or advanced from an old callback")); return; }
		if (GS->GetServerWorldTimeSeconds() <= CancelledDeadline + 1.0) { return; }
		bDone = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask21Probe CANCEL_PASS: Specs=0 Timer=0 Pending=0 OldDeadlinePassed=1 NoAdvance=1 SameTagIdempotent=1 InvalidRequestPreservesPhase=1"));
		return;
	}
	if (World->GetNetMode() != NM_ListenServer) { Fail(TEXT("Core requires a real listen server")); return; }
	if (ServerStep == 0 && Index > StartedPhaseCount)
	{
		const double Duration = Index == 1 ? 8.0 : Index == 2 ? 20.0 : 3.0;
		if (Index != StartedPhaseCount + 1 || FMath::Abs(State.PhaseEndTimeServer - State.PhaseStartTimeServer - Duration) > 0.1 ||
			(Index > 1 && State.PhaseStartTimeServer < PreviousDeadline - 0.1))
		{ Fail(TEXT("phase order/duration did not match the configured real timer")); return; }
		StartedPhaseCount = Index;
		PreviousDeadline = State.PhaseEndTimeServer;
		PublishCheckpoint(static_cast<EMiniTask21Checkpoint>(Index - 1));
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask21Probe SERVER_PHASE: Index=%d Tag=%s Revision=%u Start=%.3f Deadline=%.3f Duration=%.1f"),
			Index, *State.PhaseTag.ToString(), State.Revision, State.PhaseStartTimeServer, State.PhaseEndTimeServer, Duration);
		if (Index == 2) { UE_LOG(LogMiniInit, Display, TEXT("MiniTask21Probe SERVER_PLAYING: LateJoinNow=1 Deadline=%.3f"), State.PhaseEndTimeServer); }
	}
	if (Index && (ASC->GetActivatableAbilities().Num() != 1 || Task21TimerCount(ASC) != 1 ||
		!Phases->IsPhaseActive(State.PhaseTag) || !Task21SameState(State, Rules->GetPhaseState())))
	{ Fail(TEXT("active snapshot, exact query, single phase spec or timer inconsistent")); return; }
	for (TActorIterator<AMiniPlayerController> It(World); It && OwnerProbes.Num() < 2; ++It)
	{
		AMiniPlayerController* PC = *It;
		if (PC->IsLocalController() || !Task21PlayerReady(PC)) { continue; }
		const bool bKnown = OwnerProbes.ContainsByPredicate([PC](const TWeakObjectPtr<AMiniTask21ProbeActor>& P)
			{ return P.IsValid() && P->GetOwner() == PC; });
		if (bKnown) { continue; }
		const int32 OwnerIndex = OwnerProbes.Num() + 1;
		if ((OwnerIndex == 1 && Index != 1) || (OwnerIndex == 2 && Index != 2))
		{ Fail(TEXT("first client missed Warmup or late join was outside Playing")); return; }
		FActorSpawnParameters Params;
		Params.Owner = PC;
		AMiniTask21ProbeActor* Probe = World->SpawnActor<AMiniTask21ProbeActor>(Params);
		if (!Probe) { Fail(TEXT("owner checkpoint actor spawn failed")); return; }
		Probe->InitializeServer(OwnerIndex);
		Probe->SetCheckpoint(static_cast<EMiniTask21Checkpoint>(Index - 1), State);
		OwnerProbes.Add(Probe);
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask21Probe SERVER_JOIN: Owner=%d PhaseIndex=%d Deadline=%.3f"), OwnerIndex, Index, State.PhaseEndTimeServer);
	}
	if (ServerStep == 0 && !Index && StartedPhaseCount == 3)
	{
		if (GS->GetServerWorldTimeSeconds() < PreviousDeadline - 0.1 || ASC->GetActivatableAbilities().Num() || Task21TimerCount(ASC))
		{ Fail(TEXT("PostMatch ended early or retained spec/timer")); return; }
		PublishCheckpoint(EMiniTask21Checkpoint::None);
		if (!AllAcknowledged(EMiniTask21Checkpoint::None)) { return; }
		if (!OwnerProbes[0]->HasAcknowledged(EMiniTask21Checkpoint::Warmup) ||
			!AllAcknowledged(EMiniTask21Checkpoint::Playing) || !AllAcknowledged(EMiniTask21Checkpoint::PostMatch))
		{ Fail(TEXT("normal cycle lacked early/late client checkpoint evidence")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask21Probe SERVER_CYCLE: WarmupPlayingPostMatchNone=1 Clients=2 SameDeadline=1 ClockErrorLE1=1 MutationRejected=1"));
		if (!Rules->StartArenaPhases()) { Fail(TEXT("formal restart rejected completed arena")); return; }
		ServerStep = 1;
		return;
	}
	if (ServerStep == 1)
	{
		if (Index != 1) { return; }
		PublishCheckpoint(EMiniTask21Checkpoint::Restart);
		if (!AllAcknowledged(EMiniTask21Checkpoint::Restart)) { return; }
		CancelledDeadline = State.PhaseEndTimeServer;
		CancelledAbility = Task21ActiveAbility(ASC);
		RemovedRules = Rules;
		UMiniExperienceManagerComponent* Manager = GS->FindComponentByClass<UMiniExperienceManagerComponent>();
		const UMiniExperienceDefinition* Experience = Manager ? Manager->GetCurrentExperience() : nullptr;
		UGameFeatureAction* Action = nullptr;
		if (Experience)
		{
			for (const UMiniExperienceActionSet* Set : Experience->ActionSets)
			{
				if (!Set) { continue; }
				for (UGameFeatureAction* Candidate : Set->Actions)
				{
					if (Candidate && Candidate->GetFName() == TEXT("MiniArena_AddRules")) { Action = Candidate; }
				}
			}
		}
		const FWorldContext* Context = GEngine ? GEngine->GetWorldContextFromWorld(World) : nullptr;
		if (!Action || !Context) { Fail(TEXT("assembled Arena component Action/world context missing")); return; }
		FGameFeatureDeactivatingContext Deactivation(TEXT("MiniTask21Probe"), [](FStringView) {});
		Deactivation.SetRequiredWorldContextHandle(Context->ContextHandle);
		Action->OnGameFeatureDeactivating(Deactivation);
		ServerStep = 2;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask21Probe SERVER_ACTION_REVOKED: ActualArenaAction=1 ActiveWarmup=1 OldDeadline=%.3f"), CancelledDeadline);
		return;
	}
	if (ServerStep == 2)
	{
		if (Rules || State.PhaseTag.IsValid() || ASC->GetActivatableAbilities().Num() || Task21TimerCount(ASC) ||
			(CancelledAbility.IsValid() && CancelledAbility->HasPhaseTimer()) || Phases->HasPendingPhase())
		{ Fail(TEXT("Arena Action revoke retained dynamic rules/phase/spec/timer")); return; }
		if (RemovedRules.IsValid() && RemovedRules->HasBegunPlay()) { Fail(TEXT("removed Rules still has begun play")); return; }
		PublishCheckpoint(EMiniTask21Checkpoint::Removed);
		if (!AllAcknowledged(EMiniTask21Checkpoint::Removed) || GS->GetServerWorldTimeSeconds() <= CancelledDeadline + 1.0) { return; }
		bDone = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask21Probe CORE_PASS: Listen=1 Clients=2 LateJoinPlaying=1 SameDeadline=1 ClockErrorLE1=1 ClientMutationsRejected=1 Order=1 Durations=1 IndependentASC=1 PlayerPhaseSpecs=0 RealActionRevoke=1 ClientRulesGone=1 Specs=0 Timer=0 OldDeadlinePassed=1"));
	}
}

void UMiniTask21ProbeSubsystem::TickClient()
{
	AMiniPlayerController* PC = Task21LocalController(GetWorld());
	AMiniGameState* GS = GetWorld()->GetGameState<AMiniGameState>();
	UMiniGamePhaseSubsystem* Phases = GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>();
	UMiniAbilitySystemComponent* ASC = GS ? GS->GetPhaseAbilitySystemComponent() : nullptr;
	UMiniArenaRulesComponent* Rules = GS ? GS->FindComponentByClass<UMiniArenaRulesComponent>() : nullptr;
	AMiniTask21ProbeActor* Probe = nullptr;
	for (TActorIterator<AMiniTask21ProbeActor> It(GetWorld()); It; ++It)
	{
		if (It->GetOwner() == PC && It->GetOwnerIndex() > 0) { Probe = *It; break; }
	}
	UMiniHUDViewModel* VM = Task21HUD(PC);
	if (!PC || !GS || !Phases || !ASC || !Probe || !Task21PlayerReady(PC) || !VM || !VM->IsRunning()) { return; }
	const EMiniTask21Checkpoint Checkpoint = Probe->GetCheckpoint();
	if (ClientLastCheckpoint == static_cast<int32>(Checkpoint)) { return; }
	const FMiniGamePhaseState State = Phases->GetCurrentPhaseState();
	const bool bRemoved = Checkpoint == EMiniTask21Checkpoint::Removed;
	if ((!bRemoved && (!Rules || !Task21SameState(State, Probe->GetExpectedState()))) ||
		(bRemoved && (Rules || State.PhaseTag.IsValid()))) { return; }
	if (ASC->GetOwnerActor() != GS || ASC->GetAvatarActor() != GS || ASC->GetActivatableAbilities().Num() ||
		!Task21PlayersHaveNoPhase(GetWorld(), ASC)) { Fail(TEXT("client GameState/player phase ASC isolation failed")); return; }
	if (State.PhaseTag.IsValid())
	{
		const double Remaining = FMath::Max(0.0, State.PhaseEndTimeServer - GS->GetServerWorldTimeSeconds());
		if (!VM->GetSnapshot().bHasPhaseData || VM->GetSnapshot().PhaseTag != State.PhaseTag ||
			FMath::Abs(VM->GetSnapshot().PhaseRemainingSeconds - FMath::CeilToInt(Remaining)) > 1 || !VM->IsPhaseCountdownRunning()) { return; }
	}
	else if (VM->IsPhaseCountdownRunning() || VM->GetSnapshot().PhaseTag.IsValid() ||
		VM->GetSnapshot().bHasPhaseData != (Rules != nullptr)) { return; }
	if (ClientReadyCheckpoint != static_cast<int32>(Checkpoint))
	{
		ClientReadyCheckpoint = static_cast<int32>(Checkpoint);
		ClientCheckpointReadyAt = GetWorld()->GetTimeSeconds();
	}
	if (GetWorld()->GetTimeSeconds() - ClientCheckpointReadyAt < 0.5) { return; }
	if (Checkpoint == EMiniTask21Checkpoint::Playing && FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask21Media")))
	{
#if WITH_EDITOR
		if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { return; }
#endif
		FString Directory;
		if (!FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask21MediaOutput="), Directory))
		{
			Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"));
		}
		IFileManager::Get().MakeDirectory(*Directory, true);
		const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("Task21-Owner%d-Playing.png"), Probe->GetOwnerIndex()));
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask21Probe CLIENT_SCREENSHOT: Owner=%d PlayingHUD=1 Path=%s"), Probe->GetOwnerIndex(), *Path);
	}
	if (!bClientMutationRejected)
	{
		if (!Rules) { return; }
		const uint32 Generation = Rules->GetSourceGeneration();
		FGameplayAbilitySpecHandle Handle;
		const bool bStarted = Phases->StartPhase(Rules, UMiniGamePhaseAbility_PostMatch::StaticClass(), 100.0f, FOnMiniGamePhaseComplete(), Handle);
		const bool bCancelled = Phases->CancelPhaseForSource(Rules);
		const bool bRulesStarted = Rules->StartArenaPhases();
		Rules->StopArenaPhases();
		bClientMutationRejected = !bStarted && !bCancelled && !bRulesStarted && !Handle.IsValid() &&
			Generation == Rules->GetSourceGeneration() && Task21SameState(State, Phases->GetCurrentPhaseState()) &&
			Task21SameState(State, Rules->GetPhaseState());
		if (!bClientMutationRejected) { Fail(TEXT("real client mutation API changed local replicated state")); return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask21Probe CLIENT_MUTATIONS_REJECTED: Owner=%d Start=0 Cancel=0 RulesStart=0 RulesStopStateUnchanged=1"), Probe->GetOwnerIndex());
	}
	// Initial replication and server-time synchronization must settle before the
	// owner RPC measures it. No state/deadline is written by this client.
	if (GS->GetServerWorldTimeSeconds() <= 0.0) { return; }
	ClientLastCheckpoint = static_cast<int32>(Checkpoint);
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask21Probe CLIENT_CHECKPOINT: Owner=%d Checkpoint=%d Tag=%s Revision=%u Deadline=%.3f ServerClock=%.3f HUD=1 Countdown=%d RulesPresent=%d"),
		Probe->GetOwnerIndex(), static_cast<int32>(Checkpoint), *State.PhaseTag.ToString(), State.Revision,
		State.PhaseEndTimeServer, GS->GetServerWorldTimeSeconds(), VM->IsPhaseCountdownRunning() ? 1 : 0, Rules ? 1 : 0);
	Probe->ServerAcknowledge(Checkpoint, State, GS->GetServerWorldTimeSeconds(), bClientMutationRejected);
	if (bRemoved) { bDone = true; }
}

void UMiniTask21ProbeSubsystem::TickPractice()
{
	if (GetWorld()->GetNetMode() != NM_Standalone) { Fail(TEXT("Practice probe expects standalone")); return; }
	AMiniGameState* GS = GetWorld()->GetGameState<AMiniGameState>();
	UMiniGamePhaseSubsystem* Phases = GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>();
	UMiniAbilitySystemComponent* ASC = GS ? GS->GetPhaseAbilitySystemComponent() : nullptr;
	AMiniPlayerController* PC = Task21LocalController(GetWorld());
	UMiniHUDViewModel* VM = Task21HUD(PC);
	if (!GS || !Phases || !ASC || !Task21PlayerReady(PC) || !VM || !VM->IsRunning()) { return; }
	UMiniExperienceManagerComponent* Manager = GS->FindComponentByClass<UMiniExperienceManagerComponent>();
	const UMiniExperienceDefinition* Experience = Manager ? Manager->GetCurrentExperience() : nullptr;
	if (!Experience) { return; }
	if (Experience->GetName() != TEXT("DA_MiniPracticeExperience") || GS->FindComponentByClass<UMiniArenaRulesComponent>() ||
		Phases->GetCurrentPhaseState().PhaseTag.IsValid() || Phases->HasPendingPhase() ||
		Phases->GetRemainingSeconds() != -1.0 || ASC->GetActivatableAbilities().Num() || Task21TimerCount(ASC) ||
		!Task21PlayersHaveNoPhase(GetWorld(), ASC) || VM->GetSnapshot().bHasPhaseData || VM->IsPhaseCountdownRunning())
	{ Fail(TEXT("Practice acquired arena rules, phase ability or phase HUD/timer")); return; }
	int32 Targets = 0, Supplies = 0;
	for (TActorIterator<AMiniPracticeTarget> It(GetWorld()); It; ++It) { ++Targets; }
	for (TActorIterator<AMiniPracticeSupply> It(GetWorld()); It; ++It) { ++Supplies; }
	if (Targets != 3 || Supplies != 1) { return; }
	UMiniQuickBarComponent* Bar = PC->GetQuickBar();
	UMiniInventoryItemInstance* Rifle = Bar ? Bar->GetSlotItem(0) : nullptr;
	UMiniInventoryItemInstance* Pistol = Bar ? Bar->GetSlotItem(1) : nullptr;
	AMiniCharacter* Pawn = Cast<AMiniCharacter>(PC->GetPawn());
	UMiniEquipmentInstance* Equipment = Pawn->GetEquipmentManager()->GetCurrentEquipment();
	if (!Rifle || !Pistol || PC->GetInventoryManager()->GetEntries().Num() != 2 || Bar->GetActiveSlotIndex() != 0 ||
		!Equipment || Equipment->GetSourceItemId() != Rifle->GetInstanceId() || !VM->GetSnapshot().bAmmoReady ||
		VM->GetSnapshot().MagazineAmmo != 30 || VM->GetSnapshot().ReserveAmmo != 90 || !VM->GetSnapshot().bHealthReady)
	{ Fail(TEXT("Practice weapon/loadout/HUD was disturbed by phase integration")); return; }
	bDone = true;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask21Probe PRACTICE_PASS: Rules=0 Phase=0 Specs=0 PhaseTimer=0 HUDCountdown=0 PlayerPhaseSpecs=0 Weapons=2 Rifle=30/90 HUD=1 Input=1 Targets=3 Supply=1"));
}
