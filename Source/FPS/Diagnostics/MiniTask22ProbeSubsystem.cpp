#include "MiniTask22ProbeSubsystem.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "AbilitySystem/MiniHealthSet.h"
#include "Arena/MiniArenaPhaseConfig.h"
#include "Arena/MiniArenaRulesComponent.h"
#include "Arena/MiniMatchRulesComponent.h"
#include "Arena/MiniMatchRulesConfig.h"
#include "Arena/MiniMatchSubsystem.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniHeroComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "GameFeatureAction.h"
#include "GameFeaturesSubsystem.h"
#include "GameModes/MiniExperienceActionSet.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGameMode.h"
#include "GameModes/MiniGamePhaseAbility.h"
#include "GameModes/MiniGamePhaseSubsystem.h"
#include "GameModes/MiniGameState.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
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
AMiniPlayerController* LocalController(UWorld* World)
{
	for (TActorIterator<AMiniPlayerController> It(World); It; ++It)
	{
		if (It->IsLocalController()) { return *It; }
	}
	return nullptr;
}

AMiniCharacter* PlayerPawn(AMiniPlayerController* PC)
{
	return PC ? Cast<AMiniCharacter>(PC->GetPawn()) : nullptr;
}

AMiniPlayerState* PlayerState(AMiniPlayerController* PC)
{
	return PC ? PC->GetPlayerState<AMiniPlayerState>() : nullptr;
}

UMiniAbilitySystemComponent* PlayerASC(AMiniPlayerController* PC)
{
	AMiniPlayerState* PS = PlayerState(PC);
	return PS ? PS->GetMiniAbilitySystemComponent() : nullptr;
}

UMiniHUDViewModel* HUD(AMiniPlayerController* PC)
{
	UMiniPrimaryGameLayout* Root = PC && PC->GetLocalPlayer()
		? Cast<UMiniPrimaryGameLayout>(UPrimaryGameLayout::GetPrimaryGameLayout(PC->GetLocalPlayer())) : nullptr;
	UCommonActivatableWidgetContainerBase* Layer = Root ? Root->GetLayerWidget(Root->GetGameLayerTag()) : nullptr;
	if (!Layer) { return nullptr; }
	for (UCommonActivatableWidget* Widget : Layer->GetWidgetList())
	{
		if (UMiniHUDLayout* Layout = Cast<UMiniHUDLayout>(Widget)) { return Layout->GetViewModel(); }
	}
	return nullptr;
}

bool PlayerReady(AMiniPlayerController* PC)
{
	AMiniCharacter* Pawn = PlayerPawn(PC);
	UMiniAbilitySystemComponent* ASC = PlayerASC(PC);
	return Pawn && ASC && ASC->GetOwnerActor() == PlayerState(PC) && ASC->GetAvatarActor() == Pawn &&
		Pawn->GetHealthComponent() && !Pawn->GetHealthComponent()->IsDead() &&
		Pawn->GetHeroComponent() && (!PC->IsLocalController() || Pawn->GetHeroComponent()->IsInputActive()) &&
		Pawn->GetEquipmentManager() && Pawn->GetEquipmentManager()->GetCurrentEquipment();
}

bool FrozenSame(const FMiniMatchState& A, const FMiniMatchState& B)
{
	FMiniMatchState Left = A, Right = B;
	Left.Revision = Right.Revision = 0;
	Left.ConnectedPlayerCount = Right.ConnectedPlayerCount = 0;
	Left.Rows.Reset(); Right.Rows.Reset();
	return MiniTask22SameMatchState(Left, Right);
}

int32 PhaseSpecCount(const UMiniAbilitySystemComponent* ASC)
{
	int32 Count = 0;
	if (ASC)
	{
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			Count += Spec.Ability && Spec.Ability->IsA<UMiniGamePhaseAbility>() ? 1 : 0;
		}
	}
	return Count;
}
}

bool UMiniTask22ProbeSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if !UE_BUILD_SHIPPING
	FString Mode;
	return Super::ShouldCreateSubsystem(Outer) && FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask22="), Mode);
#else
	return false;
#endif
}

TStatId UMiniTask22ProbeSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMiniTask22ProbeSubsystem, STATGROUP_Tickables);
}

void UMiniTask22ProbeSubsystem::Fail(const TCHAR* Reason)
{
	bDone = true;
	UE_LOG(LogMiniInit, Error, TEXT("MiniTask22Probe FAIL: Reason=%s Step=%d"), Reason, ServerStep);
}

void UMiniTask22ProbeSubsystem::Pass(const FString& Mode, const TCHAR* Evidence)
{
	bDone = true;
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask22Probe %s_PASS: %s"), *Mode.ToUpper(), Evidence);
}

void UMiniTask22ProbeSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bDone || !GetWorld() || !GetWorld()->HasBegunPlay()) { return; }
	FString Mode;
	FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask22="), Mode);
	// Preload the late-joining process before the short, production PostMatch.
	// The file authorizes only ClientTravel; no replicated gameplay state is supplied.
	FString URL, Signal;
	if (GetWorld()->GetNetMode() == NM_Standalone && Mode != TEXT("Legacy"))
	{
		if (!bDeferredTravelIssued && FParse::Value(FCommandLine::Get(), TEXT("MiniTask22DeferredURL="), URL) &&
			FParse::Value(FCommandLine::Get(), TEXT("MiniTask22JoinSignal="), Signal) &&
			IFileManager::Get().FileExists(*Signal) && PlayerReady(LocalController(GetWorld())))
		{
			bDeferredTravelIssued = true;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask22Probe CLIENT_DEFERRED_TRAVEL: URL=%s"), *URL);
			LocalController(GetWorld())->ClientTravel(URL, TRAVEL_Absolute);
		}
		return; // A deliberately disconnected Logout client also returns to standalone.
	}
	WaitSeconds += DeltaTime;
	int32 Timeout = 240;
	FParse::Value(FCommandLine::Get(), TEXT("MiniTask22TimeoutSeconds="), Timeout);
	if (WaitSeconds > Timeout) { Fail(TEXT("network/lifecycle acceptance timed out")); return; }
	if (Mode == TEXT("Legacy")) { TickLegacy(); }
	else if (Mode == TEXT("Score") || Mode == TEXT("TimeDraw") || Mode == TEXT("TimeWin") ||
		Mode == TEXT("Logout") || Mode == TEXT("Revoke"))
	{
		if (GetWorld()->GetNetMode() == NM_Client) { TickClient(Mode); }
		else { TickServer(Mode); }
	}
	else { Fail(TEXT("unknown Task22 probe mode")); }
}

AMiniPlayerController* UMiniTask22ProbeSubsystem::FirstRemote() const
{
	for (const TWeakObjectPtr<AMiniTask22ProbeActor>& Probe : OwnerProbes)
	{
		if (Probe.IsValid())
		{
			if (AMiniPlayerController* PC = Cast<AMiniPlayerController>(Probe->GetOwner()))
			{
				if (PC->GetNetConnection()) { return PC; }
			}
		}
	}
	return nullptr;
}

void UMiniTask22ProbeSubsystem::DiscoverOwners()
{
	for (TActorIterator<AMiniPlayerController> It(GetWorld()); It; ++It)
	{
		AMiniPlayerController* PC = *It;
		// A genuine PostMatch late join has a PlayerState but intentionally no Pawn.
		if (PC->IsLocalController() || !PlayerState(PC) || !PC->GetNetConnection()) { continue; }
		if (OwnerProbes.ContainsByPredicate([PC](const TWeakObjectPtr<AMiniTask22ProbeActor>& P)
			{ return P.IsValid() && P->GetOwner() == PC; })) { continue; }
		FActorSpawnParameters Params;
		Params.Owner = PC;
		AMiniTask22ProbeActor* Probe = GetWorld()->SpawnActor<AMiniTask22ProbeActor>(Params);
		if (!Probe) { Fail(TEXT("owner checkpoint spawn failed")); return; }
		Probe->InitializeServer(NextOwnerIndex++);
		OwnerProbes.Add(Probe);
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask22Probe SERVER_JOIN: Owner=%d PlayerId=%d"),
			Probe->GetOwnerIndex(), PlayerState(PC)->GetPlayerId());
		PublishedName = NAME_None; // Include the real new owner in the next checkpoint.
	}
}

bool UMiniTask22ProbeSubsystem::Checkpoint(FName Name, bool bRemoved)
{
	UMiniGamePhaseSubsystem* Phases = GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>();
	UMiniMatchSubsystem* Matches = GetWorld()->GetSubsystem<UMiniMatchSubsystem>();
	if (!Phases || !Matches) { return false; }
	const FMiniGamePhaseState Phase = Phases->GetCurrentPhaseState();
	const FMiniMatchState Match = Matches->GetCurrentMatchState();
	bool bAny = false, bAll = true;
	if (PublishedName != Name || !MiniTask22SamePhaseState(Phase, PublishedPhase) ||
		!MiniTask22SameMatchState(Match, PublishedMatch))
	{
		PublishedName = Name;
		PublishedPhase = Phase;
		PublishedMatch = Match;
		++CheckpointSerial;
		for (const TWeakObjectPtr<AMiniTask22ProbeActor>& Probe : OwnerProbes)
		{
			if (Probe.IsValid() && IsValid(Probe->GetOwner()))
			{
				Probe->SetCheckpoint(CheckpointSerial, Name, Phase, Match, bRemoved);
			}
		}
	}
	for (const TWeakObjectPtr<AMiniTask22ProbeActor>& Probe : OwnerProbes)
	{
		AMiniPlayerController* PC = Probe.IsValid() ? Cast<AMiniPlayerController>(Probe->GetOwner()) : nullptr;
		if (!PC || !PC->GetNetConnection()) { continue; }
		bAny = true;
		bAll &= Probe->HasAcknowledged();
	}
	UMiniHUDViewModel* VM = HUD(LocalController(GetWorld()));
	if (!VM || !VM->IsRunning() || VM->GetSnapshot().bHasMatchData != !bRemoved ||
		(!bRemoved && !MiniTask22SameMatchState(VM->GetSnapshot().MatchState, Match))) { return false; }
	return bAny && bAll;
}

bool UMiniTask22ProbeSubsystem::CheckRejectedDamage(AMiniPlayerController* Victim)
{
	AMiniGameMode* GM = GetWorld()->GetAuthGameMode<AMiniGameMode>();
	AMiniPlayerState* PS = PlayerState(Victim);
	AMiniCharacter* Pawn = PlayerPawn(Victim);
	if (!GM || !PS || !PS->GetHealthSet() || !Pawn) { return false; }
	const float Before = PS->GetHealthSet()->GetHealth();
	const bool bApplied = GM->TryApplyEnvironmentDamage(Pawn, 25.0f);
	if (bApplied || !FMath::IsNearlyEqual(Before, PS->GetHealthSet()->GetHealth(), 0.01f))
	{ Fail(TEXT("shared IncomingDamage GE bypassed phase/protection/frozen gate")); return false; }
	return true;
}

bool UMiniTask22ProbeSubsystem::Kill(AMiniPlayerController* Victim, EMiniPlayerDeathCause Cause)
{
	AMiniGameMode* GM = GetWorld()->GetAuthGameMode<AMiniGameMode>();
	AMiniPlayerController* Host = LocalController(GetWorld());
	AMiniPlayerState* VictimPS = PlayerState(Victim);
	AMiniCharacter* Pawn = PlayerPawn(Victim);
	UMiniMatchRulesComponent* Rules = GetWorld()->GetGameState()->FindComponentByClass<UMiniMatchRulesComponent>();
	if (!GM || !Rules || !PlayerReady(Victim) || !PlayerReady(Host) || !VictimPS) { return false; }
	const FMiniPlayerMatchStats Before = VictimPS->GetMatchStats();
	const int32 BeforeKills = PlayerState(Host)->GetMatchStats().Kills;
	LastDeath = FMiniPlayerDeathInfo();
	LastDeath.VictimPawn = Pawn;
	LastDeath.VictimPlayerState = VictimPS;
	LastDeath.InstigatorPlayerState = Cause == EMiniPlayerDeathCause::Player ? PlayerState(Host) :
		Cause == EMiniPlayerDeathCause::Suicide ? VictimPS : nullptr;
	LastDeath.InstigatorPawn = Cause == EMiniPlayerDeathCause::Player ? PlayerPawn(Host) :
		Cause == EMiniPlayerDeathCause::Suicide ? Pawn : nullptr;
	LastDeath.RoundId = Rules->GetRoundId();
	LastDeath.VictimLifeId = VictimPS->GetCurrentLifeId();
	LastDeath.Cause = Cause;
	DeadPawn = Pawn;
	SavedVictimController = Victim;
	DeathAt = GetWorld()->GetTimeSeconds();
	ReplacementPawn.Reset();
	bProtectedRespawnChecked = false;
	const bool bApplied = Cause == EMiniPlayerDeathCause::Player ? GM->TryApplyTestDamage(Host, Pawn, 150.0f) :
		Cause == EMiniPlayerDeathCause::Suicide ? GM->TryApplySuicideDamage(Pawn, 150.0f) :
		GM->TryApplyEnvironmentDamage(Pawn, 150.0f);
	if (!bApplied || !Pawn->GetHealthComponent()->IsDead() || VictimPS->GetHealthSet()->GetHealth() > 0.01f ||
		VictimPS->GetMatchStats().Deaths != Before.Deaths + 1 ||
		PlayerState(Host)->GetMatchStats().Kills != BeforeKills + (Cause == EMiniPlayerDeathCause::Player ? 1 : 0))
	{ Fail(TEXT("real GE death did not submit correct victim/killer statistics")); return false; }
	const FMiniMatchState After = Rules->GetMatchState();
	FMiniPlayerDeathInfo WrongRound = LastDeath;
	WrongRound.RoundId += 100;
	if (Rules->NotifyPlayerDeath(LastDeath) || Rules->NotifyPlayerDeath(WrongRound) ||
		GM->TryApplyEnvironmentDamage(Pawn, 20.0f) || !MiniTask22SameMatchState(After, Rules->GetMatchState()))
	{ Fail(TEXT("duplicate/old-round death or repeat dead damage changed scores")); return false; }
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask22Probe SERVER_GE_DEATH: Cause=%d Life=%u Kills=%d Deaths=%d DuplicateRejected=1 OldRoundRejected=1"),
		static_cast<int32>(Cause), LastDeath.VictimLifeId, PlayerState(Host)->GetMatchStats().Kills, VictimPS->GetMatchStats().Deaths);
	return true;
}

bool UMiniTask22ProbeSubsystem::AwaitRespawn(AMiniPlayerController* Victim)
{
	if (!PlayerReady(Victim) || PlayerPawn(Victim) == DeadPawn.Get())
	{
		if (GetWorld()->GetTimeSeconds() - DeathAt > 9.0) { Fail(TEXT("actual respawn did not complete")); }
		return false;
	}
	AMiniPlayerState* PS = PlayerState(Victim);
	AMiniCharacter* Pawn = PlayerPawn(Victim);
	UMiniAbilitySystemComponent* ASC = PlayerASC(Victim);
	UMiniMatchRulesComponent* Rules = GetWorld()->GetGameState()->FindComponentByClass<UMiniMatchRulesComponent>();
	if (!ReplacementPawn.IsValid())
	{
		SpawnObservedAt = GetWorld()->GetTimeSeconds();
		if (SpawnObservedAt - DeathAt < 2.9 || PS->GetCurrentLifeId() <= LastDeath.VictimLifeId ||
			PS->GetCurrentLifePawn() != Pawn || !FMath::IsNearlyEqual(PS->GetHealthSet()->GetHealth(), 100.0f, 0.01f) ||
			!ASC->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected) || !Rules ||
			Rules->NotifyPlayerDeath(LastDeath))
		{ Fail(TEXT("respawn delay/life identity/health/protection/old-life guard failed")); return false; }
		ReplacementPawn = Pawn;
		if (!CheckRejectedDamage(Victim)) { return false; }
		Pawn->GetHealthComponent()->InitializeWithAbilitySystem(ASC); // Repeated initialization must not extend the GE.
		bProtectedRespawnChecked = true;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask22Probe SERVER_RESPAWN: OldLife=%u NewLife=%u Delay=%.3f ProtectionGE=1 DuplicateInit=1 OldLifeRejected=1"),
			LastDeath.VictimLifeId, PS->GetCurrentLifeId(), SpawnObservedAt - DeathAt);
	}
	if (ASC->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected))
	{
		if (GetWorld()->GetTimeSeconds() - SpawnObservedAt > 2.5) { Fail(TEXT("duplicate initialization refreshed spawn protection")); }
		return false;
	}
	if (!bProtectedRespawnChecked || GetWorld()->GetTimeSeconds() - SpawnObservedAt < 1.8)
	{ Fail(TEXT("spawn protection expired before its configured two seconds")); return false; }
	return true;
}

bool UMiniTask22ProbeSubsystem::RevokeActualAction()
{
	AMiniGameState* GS = GetWorld()->GetGameState<AMiniGameState>();
	UMiniExperienceManagerComponent* Manager = GS ? GS->GetExperienceManagerComponent() : nullptr;
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
	const FWorldContext* Context = GEngine ? GEngine->GetWorldContextFromWorld(GetWorld()) : nullptr;
	if (!Action || !Context) { Fail(TEXT("real assembled Arena AddComponents Action missing")); return false; }
	RemovedMatchRules = GS->FindComponentByClass<UMiniMatchRulesComponent>();
	RevokedDeadline = GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>()->GetCurrentPhaseState().PhaseEndTimeServer;
	FGameFeatureDeactivatingContext Deactivation(TEXT("MiniTask22Probe"), [](FStringView) {});
	Deactivation.SetRequiredWorldContextHandle(Context->ContextHandle);
	Action->OnGameFeatureDeactivating(Deactivation);
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask22Probe SERVER_ACTION_REVOKED: ActualAddComponents=1 PendingDeath=1 OldDeadline=%.3f"), RevokedDeadline);
	return true;
}

void UMiniTask22ProbeSubsystem::TickServer(const FString& Mode)
{
	UWorld* World = GetWorld();
	if (World->GetNetMode() != NM_ListenServer) { Fail(TEXT("FFA acceptance requires a real listen server")); return; }
	AMiniGameState* GS = World->GetGameState<AMiniGameState>();
	AMiniGameMode* GM = World->GetAuthGameMode<AMiniGameMode>();
	UMiniGamePhaseSubsystem* Phases = World->GetSubsystem<UMiniGamePhaseSubsystem>();
	UMiniMatchSubsystem* Matches = World->GetSubsystem<UMiniMatchSubsystem>();
	UMiniArenaRulesComponent* Arena = GS ? GS->FindComponentByClass<UMiniArenaRulesComponent>() : nullptr;
	UMiniMatchRulesComponent* Rules = GS ? GS->FindComponentByClass<UMiniMatchRulesComponent>() : nullptr;
	AMiniPlayerController* Host = LocalController(World);
	if (!GS || !GM || !Phases || !Matches || !Host) { return; }
	const double Now = World->GetTimeSeconds();
	const FMiniGamePhaseState Phase = Phases->GetCurrentPhaseState();
	const FMiniMatchState Match = Matches->GetCurrentMatchState();
	if (ServerStep < 40 && (!Rules || !Rules->MatchRules || !Arena || !Arena->PhaseConfig)) { return; }
	if (ServerStep < 40)
	{
		const UMiniMatchRulesConfig* Config = Rules->MatchRules;
		FString Error;
		const float PlayingSeconds = Mode == TEXT("Score") ? 300.0f : 20.0f;
		const float PostSeconds = Mode == TEXT("Score") ? 5.0f : 3.0f;
		if (!Config->ValidateConfig(Error) || Config->MinPlayers != 2 || Config->ScoreLimit != 10 ||
			!FMath::IsNearlyEqual(Config->SpawnProtectionSeconds, 2.0f) ||
			!FMath::IsNearlyEqual(Config->RespawnDelaySeconds, 3.0f) || Arena->PhaseConfig->Phases.Num() != 3 ||
			Arena->PhaseConfig->Phases[0].DurationSeconds != 0.0f ||
			Arena->PhaseConfig->Phases[1].DurationSeconds != PlayingSeconds ||
			Arena->PhaseConfig->Phases[2].DurationSeconds != PostSeconds)
		{ Fail(TEXT("fixture changed production match rules or required phase durations")); return; }
	}
	DiscoverOwners();
	if (bDone) { return; }
	AMiniPlayerController* Victim = FirstRemote();
	if (ServerStep == 0)
	{
		UMiniHUDViewModel* VM = HUD(Host);
		if (!PlayerReady(Host) || !VM || !VM->IsRunning() || !VM->GetSnapshot().bHasMatchData) { return; }
		if (Phase.PhaseTag != MiniGameplayTags::GamePhase_MiniArena_Warmup || Phase.PhaseEndTimeServer != 0.0 ||
			Match.ConnectedPlayerCount != 1 || Match.RoundId != 0 || Match.bAcceptingScores ||
			PhaseSpecCount(GS->GetPhaseAbilitySystemComponent()) != 1 || VM->GetSnapshot().PhaseRemainingSeconds != -1)
		{ Fail(TEXT("single human did not remain in real indefinite Warmup")); return; }
		if (!bWaitingLogged)
		{
			bWaitingLogged = true;
			StepStartedAt = Now;
			InitialLifeId = PlayerState(Host)->GetCurrentLifeId();
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask22Probe SERVER_READY: Mode=%s MinPlayers=2 ScoreLimit=10 Protection=2 Respawn=3 HostHUD=1"), *Mode);
		}
		if (!CheckRejectedDamage(Host)) { return; }
		const double RequiredWait = Mode == TEXT("Score") ? 11.0 : 1.0;
		if (Now - StepStartedAt < RequiredWait) { return; }
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask22Probe SERVER_SINGLE_WAIT_PASS: Seconds=%.3f Warmup=1 Deadline=0 Count=1 GERejected=1 JoinNow=1"), Now - StepStartedAt);
		ServerStep = 1;
		return;
	}
	if (ServerStep == 1)
	{
		if (!Victim || !PlayerReady(Victim) || !PlayerReady(Host) ||
			Phase.PhaseTag != MiniGameplayTags::GamePhase_MiniArena_Playing) { return; }
		const double Duration = Mode == TEXT("Score") ? 300.0 : 20.0;
		if (Match.RoundId != 1 || Match.ConnectedPlayerCount != 2 || !Match.bAcceptingScores ||
			Match.bHasResult || FMath::Abs(Phase.PhaseEndTimeServer - Phase.PhaseStartTimeServer - Duration) > 0.1 ||
			PhaseSpecCount(GS->GetPhaseAbilitySystemComponent()) != 1)
		{ Fail(TEXT("second participant did not start exactly one configured real round")); return; }
		if (SavedRoundId == 0)
		{
			const FMiniGamePhaseState Before = Phase;
			Rules->NotifyRosterChanged(); Rules->NotifyRosterChanged();
			if (Rules->GetRoundId() != 1 || !MiniTask22SamePhaseState(Before, Phases->GetCurrentPhaseState()) ||
				PlayerState(Host)->GetMatchStats().Kills != 0 || PlayerState(Victim)->GetMatchStats().Deaths != 0)
			{ Fail(TEXT("duplicate roster notification restarted phase or scores")); return; }
			SavedRoundId = Match.RoundId;
		}
		if (!Checkpoint(TEXT("Playing"))) { return; }
		SavedRoundId = Match.RoundId;
		ServerStep = 2;
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask22Probe SERVER_PLAYING: Mode=%s Round=%d Duration=%.1f Deadline=%.3f Clients=1 HUD=1 DuplicateRoster=1"),
			*Mode, Match.RoundId, Duration, Phase.PhaseEndTimeServer);
		return;
	}
	if (Mode == TEXT("Score"))
	{
		if (ServerStep == 2 || ServerStep == 4 || ServerStep == 6)
		{
			if (!PlayerReady(Victim) || PlayerASC(Victim)->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected)) { return; }
			if (ServerStep == 2 || ServerStep == 4)
			{
				if (!GM->TryApplyTestDamage(Host, PlayerPawn(Victim), 25.0f))
				{ Fail(TEXT("real nonlethal GE before self/environment death failed")); return; }
			}
			const EMiniPlayerDeathCause Cause = ServerStep == 2 ? EMiniPlayerDeathCause::Suicide :
				ServerStep == 4 ? EMiniPlayerDeathCause::Environment : EMiniPlayerDeathCause::Player;
			if (!Kill(Victim, Cause)) { return; }
			if (Cause == EMiniPlayerDeathCause::Player) { ++CombatKills; }
			ServerStep = CombatKills == 10 ? 8 : ServerStep + 1;
			return;
		}
		if (ServerStep == 3 || ServerStep == 5 || ServerStep == 7)
		{
			if (!AwaitRespawn(SavedVictimController.Get())) { return; }
			if (!Checkpoint(TEXT("Respawn"))) { return; }
			ServerStep = ServerStep == 7 ? 6 : ServerStep + 1;
			return;
		}
		if (ServerStep == 8)
		{
			if (Phase.PhaseTag != MiniGameplayTags::GamePhase_MiniArena_PostMatch) { return; }
			if (!Match.bHasResult || Match.bAcceptingScores || Match.EndReason != EMiniMatchEndReason::ScoreLimit ||
				Match.bIsDraw || Match.WinnerPlayerIds.Num() != 1 ||
				Match.WinnerPlayerIds[0] != PlayerState(Host)->GetPlayerId() ||
				PlayerState(Host)->GetMatchStats().Kills != 10 || PlayerState(Victim)->GetMatchStats().Deaths != 12 ||
				GM->GetPendingRespawnCount() != 0 || !CheckRejectedDamage(Host))
			{ Fail(TEXT("ten real GE kills did not freeze one winner and cancel pending respawn")); return; }
			FrozenResult = Match;
			StepStartedAt = Now;
			ServerStep = 9;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask22Probe SERVER_FROZEN: Mode=Score Kills=10 Deaths=12 SelfKill=0 EnvironmentKill=0 ResultLateJoinNow=1"));
			return;
		}
		if (ServerStep == 9)
		{
			if (!FrozenSame(FrozenResult, Match) || Rules->NotifyPlayerDeath(LastDeath) ||
				GM->GetPendingRespawnCount() != 0 || !CheckRejectedDamage(Host))
			{ Fail(TEXT("frozen result changed or old death respawn survived finalization")); return; }
			if (Match.ConnectedPlayerCount != 3 || OwnerProbes.Num() != 2) { return; }
			if (Phase.PhaseTag != MiniGameplayTags::GamePhase_MiniArena_PostMatch)
			{ Fail(TEXT("result late join missed the production PostMatch window")); return; }
			if (!Checkpoint(TEXT("FrozenResult"))) { return; }
			if (Now - DeathAt < 3.1) { return; }
			if (PlayerPawn(SavedVictimController.Get()) != DeadPawn.Get())
			{ Fail(TEXT("old respawn timer created a Pawn after score result")); return; }
			Pass(Mode, TEXT("Listen=1 Clients=2 SingleWaitGT10=1 RealGEKills=10 SelfEnvironment=1 Dedup=1 Protection2=1 Respawn3=1 Frozen=1 LateJoinPostMatch=1 HUD=1 OldRespawnCancelled=1"));
			return;
		}
	}
	else if (Mode == TEXT("TimeDraw") || Mode == TEXT("TimeWin"))
	{
		if (Mode == TEXT("TimeDraw") && ServerStep == 2 && !Match.bHasResult && Match.ConnectedPlayerCount == 3)
		{
			bLatePlayingAcknowledged = Checkpoint(TEXT("LatePlaying"));
		}
		if (ServerStep == 2 && Mode == TEXT("TimeWin"))
		{
			if (!PlayerReady(Victim) || PlayerASC(Victim)->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected)) { return; }
			if (!Kill(Victim, EMiniPlayerDeathCause::Player)) { return; }
			ServerStep = 3;
			return;
		}
		if (ServerStep == 3)
		{
			if (!AwaitRespawn(Victim) || !Checkpoint(TEXT("Lead"))) { return; }
			ServerStep = 4;
		}
		if (ServerStep == 2 || ServerStep == 4)
		{
			if (!Match.bHasResult || Phase.PhaseTag != MiniGameplayTags::GamePhase_MiniArena_PostMatch) { return; }
			const bool bDraw = Mode == TEXT("TimeDraw");
			if (Match.EndReason != EMiniMatchEndReason::TimeLimit || Match.bAcceptingScores || Match.bIsDraw != bDraw ||
				Match.WinnerPlayerIds.Num() != (bDraw ? 3 : 1) || (bDraw && !bLatePlayingAcknowledged) ||
				(!bDraw && Match.WinnerPlayerIds[0] != PlayerState(Host)->GetPlayerId()) ||
				!CheckRejectedDamage(Host) || GM->GetPendingRespawnCount() != 0)
			{ Fail(TEXT("real Playing timer did not freeze expected timeout winner/draw")); return; }
			if (!Checkpoint(TEXT("TimeoutResult"))) { return; }
			Pass(Mode, bDraw ? TEXT("RealTimer20=1 ZeroScoreDraw=1 Winners=3 LateJoinPlaying=1 SameDeadline=1 Frozen=1 GERejected=1 ClientResult=1 HUD=1") :
				TEXT("RealTimer20=1 RealGEWinner=1 Dedup=1 Protection2=1 Respawn3=1 Frozen=1 ClientResult=1 HUD=1"));
			return;
		}
	}
	else if (Mode == TEXT("Logout"))
	{
		if (ServerStep == 2)
		{
			if (!PlayerReady(Victim) || PlayerASC(Victim)->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected)) { return; }
			if (!Kill(Victim, EMiniPlayerDeathCause::Player)) { return; }
			ServerStep = 3;
			return;
		}
		if (ServerStep == 3)
		{
			if (!Checkpoint(TEXT("DeathBeforeLogout"))) { return; }
			// Both players are dead when the remote leaves. The sole remaining
			// host must recover in Warmup even though finalization cancels timers.
			WaitingDeadPawn = PlayerPawn(Host);
			WaitingDeadLifeId = PlayerState(Host)->GetCurrentLifeId();
			const int32 HostKills = PlayerState(Host)->GetMatchStats().Kills;
			if (!GM->TryApplyEnvironmentDamage(WaitingDeadPawn.Get(), 150.0f) ||
				!WaitingDeadPawn->GetHealthComponent()->IsDead() || PlayerState(Host)->GetMatchStats().Kills != HostKills)
			{ Fail(TEXT("remaining host environmental death was not accepted before Logout")); return; }
			for (const TWeakObjectPtr<AMiniTask22ProbeActor>& Probe : OwnerProbes)
			{
				if (Probe.IsValid() && Probe->GetOwner() == SavedVictimController.Get()) { Probe->ClientLeaveServer(); }
			}
			ServerStep = 4;
			return;
		}
		if (ServerStep == 4)
		{
			if (Match.ConnectedPlayerCount != 1 || !Match.bHasResult) { return; }
			if (Match.EndReason != EMiniMatchEndReason::InsufficientPlayers || Match.bIsDraw ||
				Match.WinnerPlayerIds.Num() || Match.bAcceptingScores)
			{ Fail(TEXT("real Logout did not abort with no winner and cancel dead player timer")); return; }
			if (Phase.PhaseTag == MiniGameplayTags::GamePhase_MiniArena_PostMatch && GM->GetPendingRespawnCount() != 0)
			{ Fail(TEXT("old respawn timer survived insufficient-player finalization")); return; }
			if (Phase.PhaseTag != MiniGameplayTags::GamePhase_MiniArena_Warmup || Now - DeathAt < 3.5) { return; }
			if (!PlayerReady(Host)) { return; }
			if (GM->GetPendingRespawnCount() != 0 || PlayerPawn(Host) == WaitingDeadPawn.Get() ||
				PlayerState(Host)->GetCurrentLifeId() <= WaitingDeadLifeId)
			{ Fail(TEXT("sole dead player did not restore a new life in waiting Warmup")); return; }
			if (Phase.PhaseEndTimeServer != 0.0 || Match.RoundId != SavedRoundId || !CheckRejectedDamage(Host))
			{ Fail(TEXT("insufficient-player result did not return to indefinite gated waiting")); return; }
			ServerStep = 5;
			UE_LOG(LogMiniInit, Display, TEXT("MiniTask22Probe SERVER_WAITING_AGAIN: Count=1 Winner=0 OldRespawnCancelled=1 LastDeadPlayerRestored=1 RejoinNow=1"));
			return;
		}
		if (ServerStep == 5)
		{
			if (!Victim || !PlayerReady(Victim) || Phase.PhaseTag != MiniGameplayTags::GamePhase_MiniArena_Playing ||
				Match.ConnectedPlayerCount != 2) { return; }
			if (Match.RoundId != SavedRoundId + 1 || Match.bHasResult || !Match.bAcceptingScores ||
				PlayerState(Host)->GetMatchStats().Kills || PlayerState(Host)->GetMatchStats().Deaths ||
				PlayerState(Victim)->GetMatchStats().Kills || PlayerState(Victim)->GetMatchStats().Deaths ||
				Rules->NotifyPlayerDeath(LastDeath) || GM->GetPendingRespawnCount() != 0)
			{ Fail(TEXT("real rejoin failed to start a fresh round or accepted prior-round death")); return; }
			if (!Checkpoint(TEXT("RejoinRound"))) { return; }
			Pass(Mode, TEXT("ActualDisconnect=1 InsufficientPlayers=1 Winner=0 IndefiniteWaiting=1 LastDeadPlayerRestored=1 RejoinRound=1 ScoresReset=1 OldRoundRejected=1 OldRespawnCancelled=1 HUD=1"));
			return;
		}
	}
	else if (Mode == TEXT("Revoke"))
	{
		if (ServerStep == 2)
		{
			if (!PlayerReady(Victim) || PlayerASC(Victim)->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected)) { return; }
			if (!Kill(Victim, EMiniPlayerDeathCause::Player)) { return; }
			ServerStep = 3;
			return;
		}
		if (ServerStep == 3)
		{
			if (!Checkpoint(TEXT("DeathBeforeRevoke"))) { return; }
			if (!RevokeActualAction()) { return; }
			ServerStep = 40;
			return;
		}
		if (ServerStep == 40)
		{
			if (Rules || Arena || Matches->HasMatchContext() || Phases->HasArenaContext() ||
				Phase.PhaseTag.IsValid() || Phases->HasPendingPhase() ||
				PhaseSpecCount(GS->GetPhaseAbilitySystemComponent()) || GM->GetPendingRespawnCount() ||
				(RemovedMatchRules.IsValid() && RemovedMatchRules->HasBegunPlay()) ||
				PlayerASC(Host)->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected))
			{ Fail(TEXT("Action revoke retained rules/source/spec/protection or pending respawn")); return; }
			if (!Checkpoint(TEXT("Removed"), true)) { return; }
			if (GS->GetServerWorldTimeSeconds() <= RevokedDeadline + 1.0 || Now - DeathAt <= 3.5) { return; }
			if (PlayerPawn(SavedVictimController.Get()) != DeadPawn.Get())
			{ Fail(TEXT("old callback respawned the victim after Action revoke")); return; }
			Pass(Mode, TEXT("ActualAction=1 ServerRulesGone=1 ClientRulesGone=1 PhaseSource=0 PhaseSpecs=0 MatchSource=0 Protection=0 PendingRespawn=0 OldDeadlinePassed=1 HUD=1"));
			return;
		}
	}
}

void UMiniTask22ProbeSubsystem::TickClient(const FString& Mode)
{
	UWorld* World = GetWorld();
	AMiniPlayerController* PC = LocalController(World);
	AMiniPlayerState* PS = PlayerState(PC);
	AMiniGameState* GS = World->GetGameState<AMiniGameState>();
	UMiniGamePhaseSubsystem* Phases = World->GetSubsystem<UMiniGamePhaseSubsystem>();
	UMiniMatchSubsystem* Matches = World->GetSubsystem<UMiniMatchSubsystem>();
	UMiniHUDViewModel* VM = HUD(PC);
	AMiniTask22ProbeActor* Probe = nullptr;
	for (TActorIterator<AMiniTask22ProbeActor> It(World); It; ++It)
	{
		if (It->GetOwner() == PC && It->GetOwnerIndex() > 0) { Probe = *It; break; }
	}
	if (!PC || !PS || !GS || !Phases || !Matches || !VM || !VM->IsRunning() || !Probe ||
		Probe->GetCheckpointSerial() <= 0 || Probe->GetCheckpointSerial() == ClientLastSerial) { return; }
	const bool bRemoved = Probe->ExpectsRemovedRules();
	UMiniMatchRulesComponent* Rules = GS->FindComponentByClass<UMiniMatchRulesComponent>();
	UMiniArenaRulesComponent* Arena = GS->FindComponentByClass<UMiniArenaRulesComponent>();
	const FMiniGamePhaseState Phase = Phases->GetCurrentPhaseState();
	const FMiniMatchState Match = Matches->GetCurrentMatchState();
	const FMiniHUDSnapshot& Snapshot = VM->GetSnapshot();
	if (!MiniTask22SamePhaseState(Phase, Probe->GetExpectedPhase()) ||
		!MiniTask22SameMatchState(Match, Probe->GetExpectedMatch()) ||
		Snapshot.bHasMatchData != !bRemoved || (!bRemoved && !MiniTask22SameMatchState(Snapshot.MatchState, Match))) { return; }
	if ((bRemoved && (Rules || Arena || Matches->HasMatchContext() || Phases->HasArenaContext() || VM->IsPhaseCountdownRunning())) ||
		(!bRemoved && (!Rules || !Arena || !Matches->HasMatchContext() || Snapshot.PhaseTag != Phase.PhaseTag))) { return; }
	const FMiniMatchPlayerRow* Row = Match.Rows.FindByPredicate([PS](const FMiniMatchPlayerRow& Entry)
		{ return Entry.PlayerId == PS->GetPlayerId(); });
	const FMiniPlayerMatchStats& Stats = PS->GetMatchStats();
	if (!bRemoved && (!Row || Stats.RoundId != Match.RoundId || Stats.Kills != Row->Kills || Stats.Deaths != Row->Deaths ||
		Snapshot.Score != Row->Kills || Snapshot.Deaths != Row->Deaths)) { return; }
	if (Phase.PhaseEndTimeServer > 0.0)
	{
		const int32 Remaining = FMath::CeilToInt(FMath::Max(0.0, Phase.PhaseEndTimeServer - GS->GetServerWorldTimeSeconds()));
		if (!VM->IsPhaseCountdownRunning() || FMath::Abs(Snapshot.PhaseRemainingSeconds - Remaining) > 1) { return; }
	}
	if (!bMutationRejected && Rules && Arena)
	{
		const uint32 Generation = Rules->GetRulesGeneration();
		const uint32 PhaseGeneration = Arena->GetSourceGeneration();
		const FMiniPlayerMatchStats BeforeStats = PS->GetMatchStats();
		const bool bStatsAccepted = PS->RecordMatchKill(BeforeStats.RoundId) || PS->RecordMatchDeath(BeforeStats.RoundId) ||
			PS->ResetMatchStats(BeforeStats.RoundId + 1);
		FMiniPlayerDeathInfo Empty;
		const bool bAccepted = Rules->NotifyPlayerDeath(Empty);
		Rules->NotifyRosterChanged();
		Rules->StopMatchRules();
		const bool bJumped = Arena->JumpToPhase(MiniGameplayTags::GamePhase_MiniArena_PostMatch);
		bMutationRejected = !bAccepted && !bJumped && !bStatsAccepted &&
			BeforeStats.RoundId == Stats.RoundId && BeforeStats.Kills == Stats.Kills && BeforeStats.Deaths == Stats.Deaths && BeforeStats.Revision == Stats.Revision &&
			Generation == Rules->GetRulesGeneration() &&
			PhaseGeneration == Arena->GetSourceGeneration() && MiniTask22SameMatchState(Match, Rules->GetMatchState()) &&
			MiniTask22SamePhaseState(Phase, Arena->GetPhaseState());
		if (!bMutationRejected) { Fail(TEXT("client mutation APIs changed replicated rules")); return; }
	}
	if (!bMutationRejected || GS->GetServerWorldTimeSeconds() <= 0.0) { return; }
	if (Probe->GetCheckpointName() == TEXT("Playing")) { bClientSawPlaying = true; }
	if (Probe->GetOwnerIndex() == 2 && Mode == TEXT("Score") && bClientSawPlaying)
	{ Fail(TEXT("result late join incorrectly acknowledged an earlier Playing checkpoint")); return; }
	if (FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask22Media")) &&
		(Probe->GetCheckpointName() == TEXT("Playing") || Match.bHasResult))
	{
#if WITH_EDITOR
		if (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) { return; }
#endif
		FString Directory;
		if (!FParse::Value(FCommandLine::Get(), TEXT("MiniProbeTask22MediaOutput="), Directory))
		{ Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots")); }
		IFileManager::Get().MakeDirectory(*Directory, true);
		const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("Task22-%s-Owner%d-%s.png"),
			*Mode, Probe->GetOwnerIndex(), *Probe->GetCheckpointName().ToString()));
		FScreenshotRequest::RequestScreenshot(Path, true, false);
		UE_LOG(LogMiniInit, Display, TEXT("MiniTask22Probe CLIENT_SCREENSHOT: Path=%s"), *Path);
	}
	ClientLastSerial = Probe->GetCheckpointSerial();
	UE_LOG(LogMiniInit, Display, TEXT("MiniTask22Probe CLIENT_CHECKPOINT: Owner=%d Checkpoint=%s Round=%d Kills=%d Deaths=%d Result=%d HUD=1 SameDeadline=1"),
		Probe->GetOwnerIndex(), *Probe->GetCheckpointName().ToString(), Match.RoundId, Stats.Kills, Stats.Deaths, Match.bHasResult ? 1 : 0);
	Probe->ServerAcknowledge(ClientLastSerial, Phase, Match, Stats, GS->GetServerWorldTimeSeconds(), true, bMutationRejected);
}

void UMiniTask22ProbeSubsystem::TickLegacy()
{
	if (GetWorld()->GetNetMode() != NM_Standalone) { Fail(TEXT("Legacy expects real standalone")); return; }
	AMiniGameState* GS = GetWorld()->GetGameState<AMiniGameState>();
	AMiniGameMode* GM = GetWorld()->GetAuthGameMode<AMiniGameMode>();
	AMiniPlayerController* Host = LocalController(GetWorld());
	UMiniHUDViewModel* VM = HUD(Host);
	UMiniMatchSubsystem* Matches = GetWorld()->GetSubsystem<UMiniMatchSubsystem>();
	if (!GS || !GM || !Matches || !PlayerReady(Host) || !VM || !VM->IsRunning()) { return; }
	UMiniMatchRulesComponent* Rules = GS->FindComponentByClass<UMiniMatchRulesComponent>();
	if (!bNullMatchInjected && !Rules && GS->FindComponentByClass<UMiniArenaRulesComponent>())
	{
		// Explicitly exercise a real component with a null config, independently
		// of the training case where there is no component at all.
		UMiniMatchRulesComponent* NullFixture = NewObject<UMiniMatchRulesComponent>(GS, TEXT("MiniTask22NullMatchFixture"), RF_Transient);
		GS->AddInstanceComponent(NullFixture);
		NullFixture->RegisterComponent();
		bNullMatchInjected = true;
		if (!NullFixture->HasBegunPlay() || NullFixture->IsFFAConfigured()) { Fail(TEXT("null MatchRules fixture did not initialize")); }
		return;
	}
	if ((Rules && Rules->MatchRules) || Matches->HasMatchContext() || VM->GetSnapshot().bHasMatchData ||
		PlayerASC(Host)->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected))
	{ Fail(TEXT("no/null MatchRules acquired FFA context, HUD or spawn protection")); return; }
	if (!bWaitingLogged)
	{
		AMiniPlayerState* PS = PlayerState(Host);
		const float Health = PS->GetHealthSet()->GetHealth();
		if (!GM->TryApplyEnvironmentDamage(PlayerPawn(Host), 25.0f) ||
			!FMath::IsNearlyEqual(PS->GetHealthSet()->GetHealth(), Health - 25.0f, 0.01f) ||
			PS->GetMatchStats().Kills || PS->GetMatchStats().Deaths)
		{ Fail(TEXT("legacy real GE was blocked by FFA or changed match statistics")); return; }
		bWaitingLogged = true;
		StepStartedAt = GetWorld()->GetTimeSeconds();
	}
	UMiniArenaRulesComponent* Arena = GS->FindComponentByClass<UMiniArenaRulesComponent>();
	if (Arena)
	{
		const FMiniGamePhaseState Phase = Arena->GetPhaseState();
		if (GetWorld()->GetTimeSeconds() - StepStartedAt < 10.0 || Phase.PhaseTag != MiniGameplayTags::GamePhase_MiniArena_Playing) { return; }
		if (FMath::Abs(Phase.PhaseEndTimeServer - Phase.PhaseStartTimeServer - 20.0) > 0.1)
		{ Fail(TEXT("Task21 timed sequence was changed by disabled FFA")); return; }
	}
	Pass(TEXT("Legacy"), Arena ? TEXT("Task21OnlyPhaseRules=1 NullMatchRules=1 RealGEInWarmup=1 TimedPlaying20=1 FFAHUD=0 Protection=0") :
		TEXT("Practice=1 MatchRules=0 RealGE=1 FFAHUD=0 Protection=0"));
}
