#pragma once

#include "Camera/CameraComponent.h"
#include "MiniCameraComponent.generated.h"

class UMiniCameraMode;

/** Evaluates the PawnData-selected mode and keeps a third-person view outside walls. */
UCLASS(ClassGroup = (Mini), meta = (BlueprintSpawnableComponent))
class FPS_API UMiniCameraComponent : public UCameraComponent
{
	GENERATED_BODY()

public:
	UMiniCameraComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void GetCameraView(float DeltaTime, FMinimalViewInfo& DesiredView) override;

	void ResetCamera();
	TSubclassOf<UMiniCameraMode> GetCurrentCameraMode() const { return CurrentCameraMode; }
	bool WasCollisionBlocked() const { return bLastCollisionBlocked; }
	FVector GetLastIdealLocation() const { return LastIdealLocation; }

private:
	TSubclassOf<UMiniCameraMode> CurrentCameraMode;
	FVector SmoothedLocation = FVector::ZeroVector;
	FRotator SmoothedRotation = FRotator::ZeroRotator;
	float SmoothedFOV = 85.0f;
	FVector LastIdealLocation = FVector::ZeroVector;
	bool bHasView = false;
	bool bLastCollisionBlocked = false;
};
