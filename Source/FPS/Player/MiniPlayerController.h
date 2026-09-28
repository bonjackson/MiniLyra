#pragma once

#include "ModularPlayerController.h"
#include "TimerManager.h"
#include "MiniPlayerController.generated.h"

class AMiniCharacter;

/** Extension receiver for a local or remote Mini player. */
UCLASS()
class FPS_API AMiniPlayerController : public AModularPlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRep_PlayerState() override;
	virtual void PostProcessInput(const float DeltaTime, const bool bGamePaused) override;
	void SetMiniInputBlocked(bool bBlocked);
	bool IsMiniInputBlocked() const { return bMiniInputBlocked; }

private:
	UFUNCTION(Server, Reliable)
	void ServerAdvanceTask10Probe(int32 CompletedCycle);
	void FinishTask10ProbeRespawn();
	void AdvanceTask10Probe();

	enum class ETask10ProbeStage : uint8
	{
		WaitReady, WaitMove, WaitFireActive, WaitFireEnded, WaitUnpossessed, WaitNextPawn,
		WaitMenuFireActive, WaitMenuJumpActive, WaitMenuBlocked, WaitMenuNoFire, WaitMenuResumed,
		WaitMenuReactivated, WaitMenuReleased, WaitActionFireActive,
		WaitActionSuspended, WaitActionNoFire, WaitActionResumed,
		WaitActionReactivated, WaitActionReleased, Complete
	};

	bool bMiniInputBlocked = false;
	bool bTask10ProbeEnabled = false;
	int32 Task10ProbeCycle = 0;
	int32 Task10ServerCompletedCycle = 0;
	TWeakObjectPtr<AMiniCharacter> Task10ServerOldPawn;
	ETask10ProbeStage Task10ProbeStage = ETask10ProbeStage::WaitReady;
	TWeakObjectPtr<AMiniCharacter> Task10ProbePawn;
	FVector Task10ProbeStartLocation = FVector::ZeroVector;
	float Task10ProbeOriginalJumpMaxHoldTime = 0.0f;
	FTimerHandle Task10ProbeTimer;
	FTimerHandle Task10RespawnTimer;
};
