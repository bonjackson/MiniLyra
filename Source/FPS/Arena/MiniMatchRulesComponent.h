#pragma once

#include "Arena/MiniMatchTypes.h"
#include "Components/GameStateComponent.h"
#include "GameModes/MiniGamePhaseTypes.h"
#include "TimerManager.h"
#include "MiniMatchRulesComponent.generated.h"

class AController;
class UMiniMatchRulesConfig;
class UMiniArenaRulesComponent;

/** Authoritative FFA rules. Phase abilities remain generic timed server work. */
UCLASS(Blueprintable, ClassGroup = (Mini), meta = (BlueprintSpawnableComponent))
class FPS_API UMiniMatchRulesComponent : public UGameStateComponent
{
	GENERATED_BODY()
public:
	UMiniMatchRulesComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Match")
	TObjectPtr<UMiniMatchRulesConfig> MatchRules;
	const FMiniMatchState& GetMatchState() const { return MatchState; }
	int32 GetRoundId() const { return MatchState.RoundId; }
	uint32 GetRulesGeneration() const { return RulesGeneration; }
	bool IsFFAConfigured() const { return MatchRules != nullptr; }
	bool IsMatchContextAvailable() const { return bContextAvailable; }
	bool CanApplyPlayerDamage(AMiniCharacter* VictimPawn) const;
	bool CanRespawnPlayer(AController* Controller) const;
	float GetRespawnDelaySeconds() const;
	float GetSpawnProtectionSeconds() const;
	bool NotifyPlayerDeath(const FMiniPlayerDeathInfo& Info);
	void NotifyRosterChanged();
	void NotifyPlayerLogout(AController* Controller);
	void StopMatchRules();

private:
	UFUNCTION() void OnRep_MatchState();
	UFUNCTION() void OnRep_MatchContext();
	bool IsAuthority() const;
	bool BindPhaseRules();
	void HandleExperienceLoaded();
	void HandlePhaseChanged(const FMiniGamePhaseState& State);
	void HandlePhaseCompleted(FGameplayTag Tag, EMiniGamePhaseEndReason Reason);
	void RebuildRoster();
	void CommitMatchState();
	void EvaluateRoster();
	void QueueStartRound();
	void StartRound();
	void FreezeResult(EMiniMatchEndReason Reason, bool bRequestPostMatch);
	void QueuePhaseTransition(FGameplayTag Tag);
	bool IsScoringWindowOpen() const;
	bool IsConnectedParticipant(const AMiniPlayerState* Player) const;

	UPROPERTY(ReplicatedUsing = OnRep_MatchState)
	FMiniMatchState MatchState;
	TWeakObjectPtr<UMiniArenaRulesComponent> PhaseRules;
	TMap<int32, TWeakObjectPtr<AMiniPlayerState>> ConnectedPlayers;
	TSet<TWeakObjectPtr<AController>> DepartedControllers;
	// Round identity is checked first; this set is cleared only when a new round starts.
	TSet<uint64> SettledDeaths;
	FDelegateHandle PhaseStateHandle;
	FDelegateHandle PhaseCompleteHandle;
	FTimerHandle TransitionTimer;
	FTimerHandle InitializationTimer;
	uint32 RulesGeneration = 0;
	UPROPERTY(ReplicatedUsing = OnRep_MatchContext)
	bool bContextAvailable = false;
	bool bReceivedContextState = false;
	bool bInitializationRetryUsed = false;
	bool bConfigValid = false;
	bool bLoaded = false;
	bool bStopped = false;
	bool bStartQueued = false;
};
