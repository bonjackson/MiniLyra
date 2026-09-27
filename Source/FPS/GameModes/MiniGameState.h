#pragma once

#include "ModularGameState.h"
#include "MiniGameState.generated.h"

class UMiniExperienceDefinition;
class UMiniExperienceManagerComponent;

// Replicated home for the Experience manager; match state and player data come later.
UCLASS()
class FPS_API AMiniGameState : public AModularGameStateBase
{
	GENERATED_BODY()

public:
	AMiniGameState(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	UMiniExperienceManagerComponent* GetExperienceManagerComponent() const { return ExperienceManagerComponent; }

	virtual void BeginPlay() override;

private:
	void HandleFlowProbeLoaded(const UMiniExperienceDefinition* Experience);
	void HandleFlowProbeFailed(const FString& Reason);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mini|Experience", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMiniExperienceManagerComponent> ExperienceManagerComponent;

	bool bFlowProbeLateSubscriberCalled = false;
};
