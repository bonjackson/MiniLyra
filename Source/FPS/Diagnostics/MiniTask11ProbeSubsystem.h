#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MiniTask11ProbeSubsystem.generated.h"

class AActor;
class AMiniCharacter;

/** Non-shipping, two-process acceptance probe for the Task 11 camera and animation path. */
UCLASS()
class FPS_API UMiniTask11ProbeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	enum class ECameraStage : uint8
	{
		WaitFirstPawn, WaitAim, WaitAimRelease, CheckCollision, Done
	};

	void TickServer(float DeltaTime);
	void TickClient(float DeltaTime);
	void TickCamera(class AMiniPlayerController* Controller, AMiniCharacter* Pawn);
	void TickAnimation(AMiniCharacter* LocalPawn, float DeltaTime);
	void TickRemote(AMiniCharacter* LocalPawn, float DeltaTime);
	void Fail(const TCHAR* Reason);

	bool bFailed = false;
	bool bPassed = false;
	int32 CameraCycle = 0;
	FString PreviousPawnPath;
	TWeakObjectPtr<AMiniCharacter> PreviousPawn;
	ECameraStage CameraStage = ECameraStage::WaitFirstPawn;
	TWeakObjectPtr<AActor> CollisionObstacle;
	bool bAimPassed = false;
	bool bCollisionPassed = false;
	bool bLocalMovingPassed = false;
	bool bJumpPassed = false;
	bool bLandPassed = false;
	bool bRemoteMovingPassed = false;
	bool bRemoteIdlePassed = false;
	bool bRemotePassed = false;
	TWeakObjectPtr<AMiniCharacter> RemotePawn;
	FVector RemoteStartLocation = FVector::ZeroVector;
	float RemoteStartYaw = 0.0f;
	float ServerClientWaitSeconds = 0.0f;
	float ServerDriveSeconds = 0.0f;
	TWeakObjectPtr<AMiniCharacter> ServerDrivenPawn;
	FVector ServerDriveStart = FVector::ZeroVector;
	bool bServerDriveDone = false;
};
