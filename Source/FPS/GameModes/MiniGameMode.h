#pragma once

#include "ModularGameMode.h"
#include "Combat/MiniDamageResult.h"
#include "Arena/MiniMatchTypes.h"
#include "TimerManager.h"
#include "MiniGameMode.generated.h"

class AController;
class AActor;
class APlayerController;
class APawn;
class UMiniExperienceDefinition;
class UMiniExperienceManagerComponent;
class UMiniPawnData;
class AMiniCharacter;
class AMiniPlayerState;
class UMiniMatchRulesComponent;

// The server selects an Experience; the GameState component loads it on each peer.
UCLASS()
class FPS_API AMiniGameMode : public AModularGameModeBase
{
	GENERATED_BODY()

public:
	AMiniGameMode(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual AActor* FindPlayerStart_Implementation(AController* Player, const FString& IncomingName = TEXT("")) override;
	virtual bool ShouldSpawnAtStartSpot(AController* Player) override;
	/** Server-only combat damage. Source and target must be live, distinct player avatars. */
	bool TryApplyDamage(AMiniCharacter* SourcePawn, AMiniCharacter* Target, float Amount);
	/** Actor receiver for ranged combat; preserves player rules and target classification. */
	bool TryApplyDamageToActor(AMiniCharacter* SourcePawn, AActor* Target, float Amount,
		FMiniDamageResult& OutResult);
	/** Server-only training damage entry; callers never modify Health directly. */
	bool TryApplyTestDamage(AController* InstigatorController, AMiniCharacter* Target, float Amount);
	/** Server GE entries for suicide and unowned environmental damage; use the same damage gate. */
	bool TryApplySuicideDamage(AMiniCharacter* Target, float Amount);
	bool TryApplyEnvironmentDamage(AMiniCharacter* Target, float Amount, AActor* EffectCauser = nullptr);
	/** Authority handles a current player's fall through GE death or same-life recovery. */
	bool HandlePlayerFellOutOfWorld(AMiniCharacter* Pawn);
	/** Pawn lifecycle cleanup; never changes a replacement Pawn or a dead life. */
	void CancelPendingOutOfWorldRecovery(AMiniCharacter* Pawn);
	int32 GetPendingOutOfWorldRecoveryCount() const { return PendingOutOfWorldRecoveries.Num(); }
	void NotifyPlayerDeath(const FMiniPlayerDeathInfo& DeathInfo);
	/** Called once by a dead Pawn's HealthComponent. */
	void ScheduleRespawn(AMiniCharacter* DeadPawn);
	void ResetPlayersForRound();
	void CancelPendingRespawnsForMatch();
	int32 GetPendingRespawnCount() const { return PendingRespawns.Num(); }

private:
	void HandleMatchAssignmentIfNotExpectingOne();
	void HandleExperienceLoaded(const UMiniExperienceDefinition* Experience);
	UMiniExperienceManagerComponent* GetExperienceManager() const;
	const UMiniPawnData* GetPawnDataForController(const AController* Controller) const;
	UMiniMatchRulesComponent* GetMatchRules() const;
	bool ApplyPlayerDamageEffect(AMiniPlayerState* SourceState, AMiniCharacter* SourcePawn,
		AMiniCharacter* Target, float Amount, AActor* EnvironmentCauser = nullptr);
	bool IsSpawnLocationClear(AController* Player, const FVector& Location, const FQuat& Rotation,
		const AActor* IgnoreActor = nullptr) const;
	void FinishRespawn(TWeakObjectPtr<AController> DeadController, uint32 WorkSerial);
	void QueueRespawnRetry(TWeakObjectPtr<AController> DeadController, uint32 WorkSerial, float Delay);
	void QueueSpawnRetry(AController* Controller);
	void CancelPendingRespawn(AController* Controller);
	void DestroyPawnForRestart(AController* Controller);
	struct FPendingOutOfWorldRecovery
	{
		FTimerHandle Timer;
		TWeakObjectPtr<AMiniCharacter> Pawn;
		TWeakObjectPtr<AController> Controller;
		TWeakObjectPtr<AMiniPlayerState> PlayerState;
		TWeakObjectPtr<UMiniMatchRulesComponent> Match;
		uint32 WorkSerial = 0;
		uint32 LifeId = 0;
		uint32 RulesGeneration = 0;
		int32 RoundId = 0;
		uint8 SavedMovementMode = 0;
		uint8 SavedCustomMovementMode = 0;
		bool bFFA = false;
		bool bMovementSuspended = false;
	};
	bool IsCurrentRecoveryLife(const FPendingOutOfWorldRecovery& Work) const;
	bool IsRecoveryContextCurrent(const FPendingOutOfWorldRecovery& Work) const;
	bool IsOutOfWorldRecoveryLocationSafe(AMiniCharacter* Pawn, AController* Controller,
		const FVector& Location, const FQuat& Rotation) const;
	void FinishOutOfWorldRecovery(TWeakObjectPtr<AMiniCharacter> Pawn, uint32 WorkSerial);
	void QueueOutOfWorldRecoveryRetry(TWeakObjectPtr<AMiniCharacter> Pawn, uint32 WorkSerial);
	void RestoreOutOfWorldRecoveryMovement(const FPendingOutOfWorldRecovery& Work);
	void CancelOutOfWorldRecovery(TWeakObjectPtr<AMiniCharacter> Pawn);
	void CancelOutOfWorldRecoveriesForController(AController* Controller);
	void CancelPendingOutOfWorldRecoveriesForMatch();
	TMap<TWeakObjectPtr<AMiniCharacter>, FPendingOutOfWorldRecovery> PendingOutOfWorldRecoveries;
	uint32 NextOutOfWorldRecoveryWorkSerial = 0;
	struct FPendingRespawn
	{
		FTimerHandle Timer;
		TWeakObjectPtr<AMiniCharacter> DeadPawn;
		TWeakObjectPtr<AMiniCharacter> ReplacementPawn;
		TWeakObjectPtr<AMiniPlayerState> PlayerState;
		TWeakObjectPtr<UMiniMatchRulesComponent> Match;
		uint32 WorkSerial = 0;
		uint32 VictimLifeId = 0;
		uint32 RulesGeneration = 0;
		int32 RoundId = 0;
		int32 AvatarBindingChecks = 0;
		bool bFFA = false;
	};
	TMap<TWeakObjectPtr<AController>, FPendingRespawn> PendingRespawns;
	uint32 NextRespawnWorkSerial = 0;
	int32 NextSpawnSelectionIndex = 0;
};
