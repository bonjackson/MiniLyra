#pragma once

#include "Diagnostics/MiniTask22ProbeActor.h"
#include "Subsystems/WorldSubsystem.h"
#include "MiniTask22ProbeSubsystem.generated.h"

class AMiniPlayerController;
class UMiniMatchRulesComponent;

/** Development-only real GE, lifecycle and network acceptance. Never writes scores or attributes. */
UCLASS()
class FPS_API UMiniTask22ProbeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
private:
	void TickServer(const FString& Mode);
	void TickClient(const FString& Mode);
	void TickLegacy();
	void Fail(const TCHAR* Reason);
	void Pass(const FString& Mode, const TCHAR* Evidence);
	void DiscoverOwners();
	bool Checkpoint(FName Name, bool bRemoved = false);
	AMiniPlayerController* FirstRemote() const;
	bool Kill(AMiniPlayerController* Victim, EMiniPlayerDeathCause Cause);
	bool AwaitRespawn(AMiniPlayerController* Victim);
	bool CheckRejectedDamage(AMiniPlayerController* Victim);
	bool RevokeActualAction();
	TArray<TWeakObjectPtr<AMiniTask22ProbeActor>> OwnerProbes;
	TWeakObjectPtr<AMiniPlayerController> SavedVictimController;
	TWeakObjectPtr<AMiniCharacter> DeadPawn;
	TWeakObjectPtr<AMiniCharacter> ReplacementPawn;
	TWeakObjectPtr<AMiniCharacter> WaitingDeadPawn;
	TWeakObjectPtr<UMiniMatchRulesComponent> RemovedMatchRules;
	FMiniPlayerDeathInfo LastDeath;
	FMiniMatchState FrozenResult;
	FMiniGamePhaseState PublishedPhase;
	FMiniMatchState PublishedMatch;
	FName PublishedName;
	double StepStartedAt = -1.0;
	double DeathAt = 0.0;
	double SpawnObservedAt = 0.0;
	double RevokedDeadline = 0.0;
	float WaitSeconds = 0.0f;
	int32 ServerStep = 0;
	int32 CheckpointSerial = 0;
	int32 ClientLastSerial = 0;
	int32 NextOwnerIndex = 1;
	int32 CombatKills = 0;
	int32 SavedRoundId = 0;
	uint32 InitialLifeId = 0;
	uint32 WaitingDeadLifeId = 0;
	bool bDone = false;
	bool bWaitingLogged = false;
	bool bMutationRejected = false;
	bool bProtectedRespawnChecked = false;
	bool bClientSawPlaying = false;
	bool bDeferredTravelIssued = false;
	bool bNullMatchInjected = false;
	bool bLatePlayingAcknowledged = false;
};
