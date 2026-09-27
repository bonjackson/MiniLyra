#pragma once

#include "Components/ActorComponent.h"
#include "MiniFeatureMarkerComponent.generated.h"

/**
 * A visible, disposable contribution from MiniShooterCore.
 *
 * Add this to AMiniGameState with the engine's Add Components GameFeatureAction.
 * No gameplay state is replicated by the marker itself.
 */
UCLASS(ClassGroup = (Mini), meta = (BlueprintSpawnableComponent))
class FPS_API UMiniFeatureMarkerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMiniFeatureMarkerComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** True between BeginPlay and EndPlay; useful when inspecting a running PIE world. */
	UFUNCTION(BlueprintPure, Category = "Mini|Game Feature")
	bool IsFeatureActive() const { return bFeatureActive; }

private:
	UPROPERTY(VisibleInstanceOnly, Category = "Mini|Game Feature")
	bool bFeatureActive = false;
};

/** Separate observable contribution for an action embedded in an Experience. */
UCLASS(ClassGroup = (Mini), meta = (BlueprintSpawnableComponent))
class FPS_API UMiniExperienceActionMarkerComponent final : public UMiniFeatureMarkerComponent
{
	GENERATED_BODY()
};

/** Separate Action target for verifying late-spawned modular characters. */
UCLASS(ClassGroup = (Mini), meta = (BlueprintSpawnableComponent))
class FPS_API UMiniCharacterFeatureMarkerComponent final : public UMiniFeatureMarkerComponent
{
	GENERATED_BODY()
};
