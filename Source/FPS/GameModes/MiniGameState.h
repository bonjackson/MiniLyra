#pragma once

#include "ModularGameState.h"
#include "TimerManager.h"
#include "MiniGameState.generated.h"

class UMiniExperienceDefinition;
class UMiniExperienceManagerComponent;
class AMiniCharacter;
class AMiniPlayerState;
class UMiniAbilitySystemComponent;

// Replicated home for the Experience manager; match state and player data come later.
UCLASS()
class FPS_API AMiniGameState : public AModularGameStateBase
{
	GENERATED_BODY()

public:
	AMiniGameState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UMiniExperienceManagerComponent* GetExperienceManagerComponent() const { return ExperienceManagerComponent; }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void HandleFlowProbeLoaded(const UMiniExperienceDefinition* Experience);
	void HandleFlowProbeFailed(const FString& Reason);
	void LogPlayerSpawnProbeSnapshot();
	void LogInitStateProbeSnapshot();
	void LogAbilityProbeSnapshot();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mini|Experience", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMiniExperienceManagerComponent> ExperienceManagerComponent;

	bool bFlowProbeLateSubscriberCalled = false;
	bool bProbeInitStates = false;
	bool bProbeAbilities = false;
	uint8 AbilityProbeStage = 0;
	uint8 AbilityProbeSuspendedTicks = 0;
	TWeakObjectPtr<AMiniPlayerState> AbilityProbeRespawnState;
	TWeakObjectPtr<UMiniAbilitySystemComponent> AbilityProbeRespawnASC;
	FString InitProbeOrder;
	TMap<TWeakObjectPtr<AMiniCharacter>, uint8> InitProbeStages;
	TSet<TWeakObjectPtr<AMiniCharacter>> RepeatedNotificationPawns;
	FTimerHandle PlayerSpawnProbeTimer;
};
