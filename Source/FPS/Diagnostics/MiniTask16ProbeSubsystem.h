#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MiniTask16ProbeSubsystem.generated.h"

class AMiniCharacter;
class AMiniPlayerController;
class AActor;

/** Three-process rifle acceptance probe, enabled only by -MiniProbeTask16. */
UCLASS()
class FPS_API UMiniTask16ProbeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	enum class EStage : uint8
	{
		WaitPlayers, WaitClientShot, WaitWallDelay, WaitMuzzleDelay,
		WaitLethalDelay, WaitClientDeath, Done
	};

	void TickServer(float DeltaTime);
	void TickClient(float DeltaTime);
	void Fail(const TCHAR* Reason);
	void Advance(EStage NewStage);
	bool CheckState(int32 Shots, int32 Ammo, float Health) const;
	bool BuildMuzzleOnlyBarrier();
	bool FireAccepted(uint32 Sequence, int32 ExpectedShots, int32 ExpectedAmmo, float ExpectedHealth);

	EStage Stage = EStage::WaitPlayers;
	float StageSeconds = 0.0f;
	bool bFailed = false;
	TWeakObjectPtr<AMiniCharacter> Shooter;
	TWeakObjectPtr<AMiniCharacter> Target;
	TWeakObjectPtr<AActor> Barrier;
	FVector TestOrigin = FVector::ZeroVector;
	FVector TestDirection = FVector::ZeroVector;
	uint32 NextSequence = 5;
	int32 LethalShots = 0;
	bool bClientPressed = false;
	bool bClientReleased = false;
	bool bClientDeathLogged = false;
	bool bClientRoleLogged = false;
	float ClientReadySeconds = 0.0f;
	float ClientPressedSeconds = 0.0f;
};
