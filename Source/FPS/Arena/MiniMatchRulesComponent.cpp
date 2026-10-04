#include "MiniMatchRulesComponent.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Arena/MiniArenaPhaseConfig.h"
#include "Arena/MiniArenaRulesComponent.h"
#include "Arena/MiniMatchRulesConfig.h"
#include "Arena/MiniMatchSubsystem.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "GameModes/MiniExperienceManagerComponent.h"
#include "GameModes/MiniGameMode.h"
#include "GameModes/MiniGamePhaseSubsystem.h"
#include "GameModes/MiniGameState.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniGameplayTags.h"
#include "System/MiniLogChannels.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

UMiniMatchRulesComponent::UMiniMatchRulesComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
}

bool UMiniMatchRulesComponent::IsAuthority() const
{
	return GetOwner() && GetOwner()->HasAuthority() && GetWorld() && GetWorld()->GetNetMode() != NM_Client;
}

void UMiniMatchRulesComponent::BeginPlay()
{
	Super::BeginPlay();
	++RulesGeneration;
	if (!MatchRules) { return; } // Task21's phase demonstration retains its existing contract.
	FString Error;
	bConfigValid = MatchRules->ValidateConfig(Error);
	if (!bConfigValid)
	{
		bContextAvailable = false;
		bStopped = true;
		UE_LOG(LogMiniExperience, Error, TEXT("MiniMatch CONFIG_REJECTED: %s"), *Error);
		return; // Configured FFA fails closed, rather than inheriting training damage rules.
	}
	// Initial replicated context can arrive before BeginPlay. Preserve a stopped
	// server context instead of resurrecting it during client initialization.
	if (IsAuthority() || !bReceivedContextState) { bContextAvailable = true; }
	if (!bContextAvailable) { return; }
	if (IsAuthority())
	{
		MatchState.MinPlayers = MatchRules->MinPlayers;
		MatchState.ScoreLimit = MatchRules->ScoreLimit;
	}
	UMiniMatchSubsystem* Matches = GetWorld()->GetSubsystem<UMiniMatchSubsystem>();
	if (!Matches || !Matches->RegisterMatchSource(this))
	{
		bContextAvailable = false;
		bStopped = true;
		UE_LOG(LogMiniExperience, Error, TEXT("MiniMatch SOURCE_REJECTED: %s"), *GetPathName());
		return;
	}
	if (!IsAuthority()) { return; }
	UMiniGamePhaseSubsystem* Phases = GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>();
	PhaseStateHandle = Phases->OnPhaseStateChanged.AddUObject(this, &ThisClass::HandlePhaseChanged);
	const uint32 ExpectedGeneration = RulesGeneration;
	AMiniGameState* State = Cast<AMiniGameState>(GetOwner());
	if (!State || !State->GetExperienceManagerComponent()) { StopMatchRules(); return; }
	State->GetExperienceManagerComponent()->CallOrRegister_OnExperienceFailed(FOnMiniExperienceFailed::FDelegate::CreateWeakLambda(this,
		[this](const FString&)
		{
			if (bContextAvailable) { StopMatchRules(); }
		}));
	State->GetExperienceManagerComponent()->CallOrRegister_OnExperienceLoaded(FOnMiniExperienceLoaded::FDelegate::CreateWeakLambda(this,
		[this, ExpectedGeneration](const UMiniExperienceDefinition*)
		{
			if (RulesGeneration == ExpectedGeneration && bContextAvailable) { HandleExperienceLoaded(); }
		}));
}

void UMiniMatchRulesComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bContextAvailable = false;
	if (UMiniMatchSubsystem* Matches = GetWorld() ? GetWorld()->GetSubsystem<UMiniMatchSubsystem>() : nullptr)
	{
		Matches->UnregisterMatchSource(this);
	}
	StopMatchRules();
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(TransitionTimer);
		GetWorld()->GetTimerManager().ClearTimer(InitializationTimer);
	}
	Super::EndPlay(EndPlayReason);
}

void UMiniMatchRulesComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMiniMatchRulesComponent, MatchState);
	DOREPLIFETIME_CONDITION_NOTIFY(UMiniMatchRulesComponent, bContextAvailable, COND_None, REPNOTIFY_Always);
}

bool UMiniMatchRulesComponent::BindPhaseRules()
{
	UMiniGamePhaseSubsystem* Phases = GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>();
	UMiniArenaRulesComponent* Rules = Phases ? Phases->GetStateSource() : nullptr;
	if (!Rules || Rules->GetOwner() != GetOwner() || !Rules->IsPhaseContextAvailable() || !Rules->PhaseConfig) { return false; }
	FString Error;
	if (!Rules->PhaseConfig->ValidateConfig(Error) || Rules->PhaseConfig->Phases[0].DurationSeconds != 0.0f ||
		Rules->PhaseConfig->Phases[1].DurationSeconds <= 0.0f || Rules->PhaseConfig->Phases[2].DurationSeconds <= 0.0f) { return false; }
	if (PhaseRules.Get() != Rules)
	{
		if (PhaseRules.IsValid()) { PhaseRules->OnPhaseCompleted.Remove(PhaseCompleteHandle); }
		PhaseRules = Rules;
		PhaseCompleteHandle = Rules->OnPhaseCompleted.AddUObject(this, &ThisClass::HandlePhaseCompleted);
	}
	return true;
}

void UMiniMatchRulesComponent::HandleExperienceLoaded()
{
	if (!IsAuthority() || bStopped || !bConfigValid || !bContextAvailable) { return; }
	if (!BindPhaseRules())
	{
		UMiniGamePhaseSubsystem* Phases = GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>();
		if (!bInitializationRetryUsed && (!Phases || !Phases->GetStateSource()))
		{
			// A stock Action can inject Match before Phase into an already Loaded
			// World. Give the paired component the rest of this call stack to arrive.
			bInitializationRetryUsed = true;
			const uint32 ExpectedGeneration = RulesGeneration;
			InitializationTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
				[this, ExpectedGeneration]()
				{
					if (RulesGeneration == ExpectedGeneration && bContextAvailable && !bStopped && !GetWorld()->bIsTearingDown)
					{ HandleExperienceLoaded(); }
				}));
			return;
		}
		UE_LOG(LogMiniExperience, Error, TEXT("MiniMatch ASSEMBLY_REJECTED: FFA needs one configured phase source with 0/positive/positive durations."));
		StopMatchRules();
		return;
	}
	bLoaded = true;
	NotifyRosterChanged();
	// Players may have initialized before this dynamic component arrived.
	for (TActorIterator<AMiniCharacter> It(GetWorld()); It; ++It)
	{
		if (UMiniHealthComponent* Health = It->GetHealthComponent()) { Health->TryApplySpawnProtectionForCurrentLife(); }
	}
}

bool UMiniMatchRulesComponent::IsConnectedParticipant(const AMiniPlayerState* Player) const
{
	const TWeakObjectPtr<AMiniPlayerState>* Connected = Player ? ConnectedPlayers.Find(Player->GetPlayerId()) : nullptr;
	return Connected && Connected->Get() == Player && Player->GetWorld() == GetWorld();
}

void UMiniMatchRulesComponent::RebuildRoster()
{
	ConnectedPlayers.Reset();
	for (FMiniMatchPlayerRow& Row : MatchState.Rows) { Row.bConnected = false; }
	for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
	{
		APlayerController* Controller = *It;
		AMiniPlayerState* Player = Controller->GetPlayerState<AMiniPlayerState>();
		if (!IsValid(Controller) || Controller->IsActorBeingDestroyed() || DepartedControllers.Contains(Controller) ||
			!Player || Player->IsOnlyASpectator() || Player->IsInactive()) { continue; }
		ConnectedPlayers.Add(Player->GetPlayerId(), Player);
		if (MatchState.RoundId > 0 && Player->GetMatchStats().RoundId != MatchState.RoundId)
		{
			Player->ResetMatchStats(MatchState.RoundId); // A late join starts with zero in this round.
		}
		FMiniMatchPlayerRow* Row = MatchState.Rows.FindByPredicate([Player](const FMiniMatchPlayerRow& Value) { return Value.PlayerId == Player->GetPlayerId(); });
		if (!Row) { Row = &MatchState.Rows.AddDefaulted_GetRef(); }
		const FMiniPlayerMatchStats& Stats = Player->GetMatchStats();
		Row->PlayerId = Player->GetPlayerId();
		Row->DisplayName = Player->GetPlayerName();
		Row->Kills = Stats.RoundId == MatchState.RoundId ? Stats.Kills : 0;
		Row->Deaths = Stats.RoundId == MatchState.RoundId ? Stats.Deaths : 0;
		Row->bConnected = true;
	}
	MatchState.ConnectedPlayerCount = ConnectedPlayers.Num();
	MatchState.Rows.Sort([](const FMiniMatchPlayerRow& A, const FMiniMatchPlayerRow& B) { return A.PlayerId < B.PlayerId; });
}

void UMiniMatchRulesComponent::NotifyRosterChanged()
{
	if (!IsAuthority() || !bLoaded || bStopped || !bConfigValid) { return; }
	RebuildRoster();
	CommitMatchState();
	EvaluateRoster();
	// GameMode's Loaded listener can run before this component and its Warmup are ready.
	if (AMiniGameMode* Mode = GetWorld()->GetAuthGameMode<AMiniGameMode>())
	{
		for (TActorIterator<APlayerController> It(GetWorld()); It; ++It)
		{
			if (!CanRespawnPlayer(*It)) { continue; }
			AMiniCharacter* Pawn = Cast<AMiniCharacter>(It->GetPawn());
			if (!It->GetPawn()) { Mode->RestartPlayer(*It); }
			else if (PhaseRules.IsValid() && PhaseRules->GetPhaseState().PhaseTag == MiniGameplayTags::GamePhase_MiniArena_Warmup &&
				Pawn && Pawn->GetHealthComponent() && Pawn->GetHealthComponent()->IsDead())
			{
				// The last player may have died just before a terminal event cancelled
				// their old timer. Waiting for another player must still be playable.
				Mode->ScheduleRespawn(Pawn);
			}
		}
	}
}

void UMiniMatchRulesComponent::NotifyPlayerLogout(AController* Controller)
{
	if (!IsAuthority() || !Controller || Controller->GetWorld() != GetWorld()) { return; }
	DepartedControllers.Add(Controller); // Logout is forwarded before PlayerArray/Controller destruction.
	NotifyRosterChanged();
}

void UMiniMatchRulesComponent::EvaluateRoster()
{
	if (!bLoaded || bStopped || !PhaseRules.IsValid()) { return; }
	const FGameplayTag Tag = PhaseRules->GetPhaseState().PhaseTag;
	if (Tag == MiniGameplayTags::GamePhase_MiniArena_Warmup)
	{
		if (MatchState.ConnectedPlayerCount >= MatchState.MinPlayers) { QueueStartRound(); }
		else if (bStartQueued)
		{
			GetWorld()->GetTimerManager().ClearTimer(TransitionTimer);
			bStartQueued = false;
		}
	}
	else if (Tag == MiniGameplayTags::GamePhase_MiniArena_Playing && MatchState.bAcceptingScores &&
		MatchState.ConnectedPlayerCount < MatchState.MinPlayers)
	{
		FreezeResult(EMiniMatchEndReason::InsufficientPlayers, true);
	}
}

void UMiniMatchRulesComponent::QueueStartRound()
{
	if (bStartQueued || !IsAuthority() || bStopped) { return; }
	bStartQueued = true;
	const uint32 ExpectedGeneration = RulesGeneration;
	const int32 ExpectedRound = MatchState.RoundId;
	const uint32 ExpectedPhaseRevision = PhaseRules->GetPhaseState().Revision;
	TransitionTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
		[this, ExpectedGeneration, ExpectedRound, ExpectedPhaseRevision]()
		{
			bStartQueued = false;
			if (!bContextAvailable || bStopped || RulesGeneration != ExpectedGeneration || MatchState.RoundId != ExpectedRound ||
				!PhaseRules.IsValid() || PhaseRules->GetPhaseState().Revision != ExpectedPhaseRevision) { return; }
			StartRound();
		}));
}

void UMiniMatchRulesComponent::StartRound()
{
	if (!IsAuthority() || !bLoaded || bStopped || !bConfigValid || !PhaseRules.IsValid() ||
		PhaseRules->GetPhaseState().PhaseTag != MiniGameplayTags::GamePhase_MiniArena_Warmup || GetWorld()->bIsTearingDown) { return; }
	RebuildRoster();
	if (MatchState.ConnectedPlayerCount < MatchState.MinPlayers) { CommitMatchState(); return; }
	if (!PhaseRules->JumpToPhase(MiniGameplayTags::GamePhase_MiniArena_Playing))
	{
		UE_LOG(LogMiniExperience, Error, TEXT("MiniMatch START_REJECTED: phase transition failed"));
		return;
	}
	++RulesGeneration;
	++MatchState.RoundId;
	SettledDeaths.Reset();
	MatchState.Rows.Reset();
	for (const auto& Pair : ConnectedPlayers)
	{
		if (AMiniPlayerState* Player = Pair.Value.Get()) { Player->ResetMatchStats(MatchState.RoundId); }
	}
	MatchState.bHasResult = false;
	MatchState.EndReason = EMiniMatchEndReason::None;
	MatchState.bIsDraw = false;
	MatchState.ResultRows.Reset();
	MatchState.WinnerPlayerIds.Reset();
	MatchState.bAcceptingScores = true;
	RebuildRoster();
	CommitMatchState();
	if (AMiniGameMode* Mode = GetWorld()->GetAuthGameMode<AMiniGameMode>()) { Mode->ResetPlayersForRound(); }
	UE_LOG(LogMiniExperience, Display, TEXT("MiniMatch ROUND_STARTED: Round=%d Players=%d ScoreLimit=%d"),
		MatchState.RoundId, MatchState.ConnectedPlayerCount, MatchState.ScoreLimit);
}

bool UMiniMatchRulesComponent::IsScoringWindowOpen() const
{
	if (!IsAuthority() || !bContextAvailable || !bLoaded || bStopped || !bConfigValid || !MatchState.bAcceptingScores ||
		MatchState.bHasResult || !PhaseRules.IsValid()) { return false; }
	const FMiniGamePhaseState& Phase = PhaseRules->GetPhaseState();
	const AMiniGameState* State = Cast<AMiniGameState>(GetOwner());
	return PhaseRules->IsPhaseContextAvailable() && Phase.PhaseTag == MiniGameplayTags::GamePhase_MiniArena_Playing &&
		State && Phase.PhaseEndTimeServer > 0.0 && State->GetServerWorldTimeSeconds() < Phase.PhaseEndTimeServer;
}

bool UMiniMatchRulesComponent::CanApplyPlayerDamage(AMiniCharacter* VictimPawn) const
{
	if (!MatchRules) { return true; }
	const AMiniPlayerState* Victim = VictimPawn ? VictimPawn->GetPlayerState<AMiniPlayerState>() : nullptr;
	return IsScoringWindowOpen() && IsValid(VictimPawn) && VictimPawn->GetWorld() == GetWorld() &&
		IsConnectedParticipant(Victim) && Victim->GetCurrentLifeId() != 0 && Victim->GetCurrentLifePawn() == VictimPawn &&
		Victim->GetMiniAbilitySystemComponent()->GetAvatarActor() == VictimPawn &&
		VictimPawn->GetHealthComponent() && !VictimPawn->GetHealthComponent()->IsDead() &&
		!Victim->GetMiniAbilitySystemComponent()->HasMatchingGameplayTag(MiniGameplayTags::State_SpawnProtected);
}

bool UMiniMatchRulesComponent::CanRespawnPlayer(AController* Controller) const
{
	if (!MatchRules) { return true; }
	const AMiniPlayerState* Player = Controller ? Controller->GetPlayerState<AMiniPlayerState>() : nullptr;
	if (!IsAuthority() || bStopped || !bLoaded || !bConfigValid || !PhaseRules.IsValid() || !IsConnectedParticipant(Player)) { return false; }
	return PhaseRules->GetPhaseState().PhaseTag == MiniGameplayTags::GamePhase_MiniArena_Warmup || IsScoringWindowOpen();
}

float UMiniMatchRulesComponent::GetRespawnDelaySeconds() const
{
	return MatchRules && bConfigValid ? MatchRules->RespawnDelaySeconds : 3.0f;
}

float UMiniMatchRulesComponent::GetSpawnProtectionSeconds() const
{
	return MatchRules && bConfigValid && !bStopped ? MatchRules->SpawnProtectionSeconds : 0.0f;
}

bool UMiniMatchRulesComponent::NotifyPlayerDeath(const FMiniPlayerDeathInfo& Info)
{
	AMiniPlayerState* Victim = Info.VictimPlayerState.Get();
	AMiniCharacter* Pawn = Info.VictimPawn.Get();
	if (!IsScoringWindowOpen() || Info.RoundId != MatchState.RoundId || !IsConnectedParticipant(Victim) ||
		!Pawn || Pawn->GetWorld() != GetWorld() || Pawn->GetPlayerState<AMiniPlayerState>() != Victim ||
		Victim->GetCurrentLifePawn() != Pawn || Info.VictimLifeId == 0 || Victim->GetCurrentLifeId() != Info.VictimLifeId ||
		Victim->GetMiniAbilitySystemComponent()->GetAvatarActor() != Pawn || !Pawn->GetHealthComponent()->IsDead()) { return false; }
	const uint64 DeathKey = (uint64(uint32(Victim->GetPlayerId())) << 32) | Info.VictimLifeId;
	if (SettledDeaths.Contains(DeathKey)) { return false; }
	SettledDeaths.Add(DeathKey); // Commit dedup before any externally observable PlayerState notifications.
	Victim->RecordMatchDeath(MatchState.RoundId);
	// PlayerState listeners can synchronously stop the rules or end this round.
	// The death is settled, but a later part of it must not mutate another round.
	if (!IsScoringWindowOpen() || Info.RoundId != MatchState.RoundId) { return true; }
	AMiniPlayerState* Killer = Info.InstigatorPlayerState.Get();
	const bool bPlayerKill = Info.Cause == EMiniPlayerDeathCause::Player && Killer != Victim && IsConnectedParticipant(Killer);
	if (bPlayerKill) { Killer->RecordMatchKill(MatchState.RoundId); }
	if (!IsScoringWindowOpen() || Info.RoundId != MatchState.RoundId) { return true; }
	RebuildRoster();
	if (bPlayerKill && Killer->GetMatchStats().Kills >= MatchState.ScoreLimit) { FreezeResult(EMiniMatchEndReason::ScoreLimit, true); }
	else { CommitMatchState(); }
	UE_LOG(LogMiniExperience, Display, TEXT("MiniMatch DEATH_ACCEPTED: Round=%d Victim=%d Life=%u Killer=%d Cause=%d"),
		MatchState.RoundId, Victim->GetPlayerId(), Info.VictimLifeId, bPlayerKill ? Killer->GetPlayerId() : INDEX_NONE, int32(Info.Cause));
	return true;
}

void UMiniMatchRulesComponent::FreezeResult(EMiniMatchEndReason Reason, bool bRequestPostMatch)
{
	if (!IsAuthority() || bStopped || !bLoaded || MatchState.bHasResult || !MatchState.bAcceptingScores) { return; }
	MatchState.bAcceptingScores = false;
	RebuildRoster();
	MatchState.bHasResult = true;
	MatchState.EndReason = Reason;
	MatchState.ResultRows = MatchState.Rows;
	MatchState.WinnerPlayerIds.Reset();
	if (Reason != EMiniMatchEndReason::InsufficientPlayers)
	{
		int32 HighestKills = -1;
		for (const FMiniMatchPlayerRow& Row : MatchState.ResultRows)
		{
			if (!Row.bConnected) { continue; }
			if (Row.Kills > HighestKills) { HighestKills = Row.Kills; MatchState.WinnerPlayerIds.Reset(); }
			if (Row.Kills == HighestKills) { MatchState.WinnerPlayerIds.Add(Row.PlayerId); }
		}
	}
	MatchState.bIsDraw = MatchState.WinnerPlayerIds.Num() > 1;
	if (AMiniGameMode* Mode = GetWorld()->GetAuthGameMode<AMiniGameMode>()) { Mode->CancelPendingRespawnsForMatch(); }
	CommitMatchState();
	UE_LOG(LogMiniExperience, Display, TEXT("MiniMatch RESULT_FROZEN: Round=%d Reason=%d Draw=%d Winners=%d"),
		MatchState.RoundId, int32(Reason), MatchState.bIsDraw ? 1 : 0, MatchState.WinnerPlayerIds.Num());
	if (bRequestPostMatch) { QueuePhaseTransition(MiniGameplayTags::GamePhase_MiniArena_PostMatch); }
}

void UMiniMatchRulesComponent::QueuePhaseTransition(FGameplayTag Tag)
{
	if (!IsAuthority() || bStopped || !PhaseRules.IsValid()) { return; }
	GetWorld()->GetTimerManager().ClearTimer(TransitionTimer);
	bStartQueued = false;
	const uint32 ExpectedGeneration = RulesGeneration;
	const int32 ExpectedRound = MatchState.RoundId;
	const uint32 ExpectedPhaseRevision = PhaseRules->GetPhaseState().Revision;
	TransitionTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,
		[this, Tag, ExpectedGeneration, ExpectedRound, ExpectedPhaseRevision]()
		{
			if (!IsAuthority() || !bContextAvailable || bStopped || RulesGeneration != ExpectedGeneration ||
				MatchState.RoundId != ExpectedRound || !PhaseRules.IsValid() || GetWorld()->bIsTearingDown ||
				PhaseRules->GetPhaseState().Revision != ExpectedPhaseRevision) { return; }
			if (!PhaseRules->JumpToPhase(Tag)) { UE_LOG(LogMiniExperience, Error, TEXT("MiniMatch PHASE_TRANSITION_REJECTED: %s"), *Tag.ToString()); }
		}));
}

void UMiniMatchRulesComponent::HandlePhaseCompleted(FGameplayTag Tag, EMiniGamePhaseEndReason Reason)
{
	if (!IsAuthority() || bStopped || !bLoaded || Reason != EMiniGamePhaseEndReason::Completed) { return; }
	if (Tag == MiniGameplayTags::GamePhase_MiniArena_Playing) { FreezeResult(EMiniMatchEndReason::TimeLimit, false); }
	else if (Tag == MiniGameplayTags::GamePhase_MiniArena_PostMatch) { QueuePhaseTransition(MiniGameplayTags::GamePhase_MiniArena_Warmup); }
}

void UMiniMatchRulesComponent::HandlePhaseChanged(const FMiniGamePhaseState& State)
{
	if (!IsAuthority() || bStopped || !bLoaded) { return; }
	if (!BindPhaseRules()) { StopMatchRules(); return; }
	if (State.PhaseTag == MiniGameplayTags::GamePhase_MiniArena_Warmup) { NotifyRosterChanged(); }
	else if (State.PhaseTag == MiniGameplayTags::GamePhase_MiniArena_PostMatch && MatchState.bAcceptingScores)
	{
		MatchState.bAcceptingScores = false;
		if (AMiniGameMode* Mode = GetWorld()->GetAuthGameMode<AMiniGameMode>()) { Mode->CancelPendingRespawnsForMatch(); }
		CommitMatchState();
	}
}

void UMiniMatchRulesComponent::CommitMatchState()
{
	if (!IsAuthority()) { return; }
	++MatchState.Revision;
	GetOwner()->ForceNetUpdate();
	OnRep_MatchState();
}

void UMiniMatchRulesComponent::OnRep_MatchState()
{
	if (bContextAvailable && GetWorld())
	{
		if (UMiniMatchSubsystem* Matches = GetWorld()->GetSubsystem<UMiniMatchSubsystem>()) { Matches->PublishMatchState(this, MatchState); }
	}
}

void UMiniMatchRulesComponent::OnRep_MatchContext()
{
	bReceivedContextState = true;
	UMiniMatchSubsystem* Matches = GetWorld() ? GetWorld()->GetSubsystem<UMiniMatchSubsystem>() : nullptr;
	if (!Matches) { return; }
	if (!bContextAvailable) { Matches->UnregisterMatchSource(this); }
	else if (HasBegunPlay() && bConfigValid && Matches->GetMatchRulesComponent() != this)
	{
		Matches->RegisterMatchSource(this);
	}
}

void UMiniMatchRulesComponent::StopMatchRules()
{
	if (!IsAuthority() || bStopped) { return; }
	bStopped = true;
	bLoaded = false;
	bContextAvailable = false;
	OnRep_MatchContext();
	MatchState.bAcceptingScores = false;
	++RulesGeneration;
	GetWorld()->GetTimerManager().ClearTimer(TransitionTimer);
	GetWorld()->GetTimerManager().ClearTimer(InitializationTimer);
	bStartQueued = false;
	if (UMiniGamePhaseSubsystem* Phases = GetWorld()->GetSubsystem<UMiniGamePhaseSubsystem>()) { Phases->OnPhaseStateChanged.Remove(PhaseStateHandle); }
	PhaseStateHandle.Reset();
	if (PhaseRules.IsValid()) { PhaseRules->OnPhaseCompleted.Remove(PhaseCompleteHandle); }
	PhaseCompleteHandle.Reset();
	if (AMiniGameMode* Mode = GetWorld()->GetAuthGameMode<AMiniGameMode>()) { Mode->CancelPendingRespawnsForMatch(); }
	for (TActorIterator<AMiniCharacter> It(GetWorld()); It; ++It)
	{
		if (UMiniHealthComponent* Health = It->GetHealthComponent()) { Health->RemoveSpawnProtectionForCurrentLife(); }
	}
	if (PhaseRules.IsValid()) { PhaseRules->StopArenaPhases(); }
	PhaseRules.Reset();
	ConnectedPlayers.Reset();
	SettledDeaths.Reset();
	CommitMatchState();
}
