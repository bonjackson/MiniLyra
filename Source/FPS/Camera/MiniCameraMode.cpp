#include "MiniCameraMode.h"

UMiniCameraMode_ThirdPerson::UMiniCameraMode_ThirdPerson()
{
	CameraOffset = FVector(-300.0f, 65.0f, 35.0f);
	FieldOfView = 85.0f;
	RotationLagSpeed = 15.0f;
	RecoveryLagSpeed = 8.0f;
	BlendSpeed = 10.0f;
	CollisionProbeRadius = 12.0f;
}

UMiniCameraMode_Aim::UMiniCameraMode_Aim()
{
	CameraOffset = FVector(-180.0f, 48.0f, 18.0f);
	FieldOfView = 65.0f;
	RotationLagSpeed = 20.0f;
	RecoveryLagSpeed = 10.0f;
	BlendSpeed = 12.0f;
	CollisionProbeRadius = 12.0f;
}
