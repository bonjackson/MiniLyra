#pragma once

#include "Animation/AnimInstance.h"
#include "MiniAnimInstance.generated.h"

/** Replication-safe movement values consumed by the small Manny AnimGraph. */
UCLASS(Blueprintable)
class FPS_API UMiniAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Mini|Animation")
	float GroundSpeed = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Mini|Animation")
	bool bIsMoving = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Mini|Animation")
	bool bIsInAir = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Mini|Animation")
	bool bIsAscending = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Mini|Animation")
	bool bRecentlyLanded = false;

private:
	float LandingTimeRemaining = 0.0f;
};
