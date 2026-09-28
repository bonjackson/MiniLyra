#pragma once

#include "UObject/Object.h"
#include "MiniCameraMode.generated.h"

/** A small, data-driven camera mode. PawnData selects the normal and aiming classes. */
UCLASS(Abstract, Blueprintable)
class FPS_API UMiniCameraMode : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Camera")
	FVector CameraOffset = FVector(-300.0f, 65.0f, 35.0f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Camera", meta = (ClampMin = "30.0", ClampMax = "170.0"))
	float FieldOfView = 85.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Camera", meta = (ClampMin = "0.0"))
	float RotationLagSpeed = 15.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Camera", meta = (ClampMin = "0.0"))
	float RecoveryLagSpeed = 8.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Camera", meta = (ClampMin = "0.0"))
	float BlendSpeed = 10.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Mini|Camera", meta = (ClampMin = "1.0"))
	float CollisionProbeRadius = 12.0f;
};

UCLASS()
class FPS_API UMiniCameraMode_ThirdPerson : public UMiniCameraMode
{
	GENERATED_BODY()

public:
	UMiniCameraMode_ThirdPerson();
};

UCLASS()
class FPS_API UMiniCameraMode_Aim : public UMiniCameraMode
{
	GENERATED_BODY()

public:
	UMiniCameraMode_Aim();
};
