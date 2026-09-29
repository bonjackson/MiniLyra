#pragma once

#include "ModularGameMode.h"
#include "MiniGameMode.generated.h"

class AController;
class APlayerController;
class APawn;
class UMiniExperienceDefinition;
class UMiniExperienceManagerComponent;
class UMiniPawnData;
class AMiniCharacter;

// The server selects an Experience; the GameState component loads it on each peer.
UCLASS()
class FPS_API AMiniGameMode : public AModularGameModeBase
{
	GENERATED_BODY()

public:
	AMiniGameMode(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void InitGameState() override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;
	/** Server-only combat damage. Source and target must be live, distinct player avatars. */
	bool TryApplyDamage(AMiniCharacter* SourcePawn, AMiniCharacter* Target, float Amount);
	/** Server-only training damage entry; callers never modify Health directly. */
	bool TryApplyTestDamage(AController* InstigatorController, AMiniCharacter* Target, float Amount);
	/** Called once by a dead Pawn's HealthComponent. */
	void ScheduleRespawn(AMiniCharacter* DeadPawn);

private:
	void HandleMatchAssignmentIfNotExpectingOne();
	void HandleExperienceLoaded(const UMiniExperienceDefinition* Experience);
	UMiniExperienceManagerComponent* GetExperienceManager() const;
	const UMiniPawnData* GetPawnDataForController(const AController* Controller) const;
	void FinishRespawn(TWeakObjectPtr<AController> DeadController, TWeakObjectPtr<AMiniCharacter> DeadPawn);
	void QueueRespawnRetry(TWeakObjectPtr<AController> DeadController,
		TWeakObjectPtr<AMiniCharacter> DeadPawn, float Delay);
	TSet<TWeakObjectPtr<AMiniCharacter>> PendingRespawns;
	TMap<TWeakObjectPtr<AMiniCharacter>, int32> PendingAvatarBindingChecks;
};
