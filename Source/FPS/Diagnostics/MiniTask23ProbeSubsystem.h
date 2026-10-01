#pragma once

#include "Diagnostics/MiniTask23ProbeActor.h"
#include "Subsystems/WorldSubsystem.h"
#include "MiniTask23ProbeSubsystem.generated.h"

class AMiniPlayerController;
class UMiniEquipmentInstance;

/** Development-only four-player map, fall recovery and continuous-round acceptance. */
UCLASS()
class FPS_API UMiniTask23ProbeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
private:
	void TickServer();
	void TickClient();
	void TickPractice();
	void Fail(const TCHAR* Reason);
	void DiscoverOwners();
	bool Checkpoint(FName Name, bool bLivePawn = true);
	bool VerifyMap(bool bArena, bool bRoutes);
	bool VerifyPlayerGround(AMiniPlayerController* PC) const;
	bool BeginFall(AMiniPlayerController* PC);
	bool AwaitSafeRecovery();
	bool AwaitFallDeath();
	bool AwaitRespawn();
	bool ProtectionExpired();
	bool KillByGE(AMiniPlayerController* Victim);
	void MakeSpawnBlockers();
	void RemoveSpawnBlockers();
	bool RequestMedia(AMiniTask23ProbeActor* Probe);
	void BeginOverview();
	void LogClientWait(const TCHAR* Reason, AMiniTask23ProbeActor* Probe);
	TArray<TWeakObjectPtr<AMiniTask23ProbeActor>> OwnerProbes;
	TArray<TWeakObjectPtr<AActor>> SpawnBlockers;
	TArray<TWeakObjectPtr<AMiniPlayerController>> Participants;
	TArray<uint32> PreviousRoundLives;
	TWeakObjectPtr<AMiniPlayerController> VictimController;
	TWeakObjectPtr<AMiniCharacter> SavedPawn;
	TWeakObjectPtr<AMiniCharacter> ReplacementPawn;
	TWeakObjectPtr<UMiniEquipmentInstance> SavedEquipment;
	FMiniPlayerMatchStats SavedStats;
	FMiniMatchState PublishedMatch;
	FMiniGamePhaseState PublishedPhase;
	FName PublishedName;
	double StepStartedAt = 0.0;
	double DeathAt = 0.0;
	double SpawnObservedAt = 0.0;
	float WaitSeconds = 0.0f;
	float SavedHealth = 0.0f;
	uint32 SavedLifeId = 0;
	int32 SavedHostKills = 0;
	int32 ServerStep = 0;
	int32 CheckpointSerial = 0;
	int32 ClientLastSerial = 0;
	int32 EnvironmentDeaths = 0;
	int32 CombatKills = 0;
	int32 CompletedRounds = 0;
	int32 InitialRound = 0;
	bool bDone = false;
	bool bMapVerified = false;
	bool bClientMapVerified = false;
	bool bFallProtected = false;
	bool bBlockedCleanupVerified = false;
	bool bOverviewPending = false;
	double OverviewStartedAt = 0.0;
	double ClientWaitLoggedAt = -5.0;
};
