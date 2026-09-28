#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MiniTask12ProbeSubsystem.generated.h"

class AMiniCharacter;
class AMiniPlayerController;
class AMiniTask12ProbeActor;
class UMiniAbilitySystemComponent;

/** Two-process, input-driven acceptance probe for Task 12 abilities and blocking tags. */
UCLASS()
class FPS_API UMiniTask12ProbeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	enum class EStage : uint8
	{
		WaitReady,
		WaitJumpActive, WaitJumpReleased,
		WaitAimActive, WaitAimSingle, WaitAimReleased,
		WaitAimForReload, WaitReloadCancel, WaitReloadJumpReady, WaitReloadJump,
		WaitReloadJumpReleased, WaitReloadClear,
		WaitJumpForDead, WaitDeadCancel, WaitDeadClear,
		WaitAimForInputBlock, WaitInputBlockCancel, WaitInputBlockClear,
		WaitAimForRespawn, WaitUnpossessed, WaitNewPawn, WaitNewAimActive, WaitNewAimReleased,
		Done
	};

	void TickServer();
	void TickClient(float DeltaTime);
	void SetStage(EStage NewStage);
	void Fail(const TCHAR* Reason);

	EStage Stage = EStage::WaitReady;
	float StageSeconds = 0.0f;
	bool bFailed = false;
	bool bActorSpawned = false;
	FString OldPawnPath;
	TWeakObjectPtr<AMiniCharacter> OldPawn;
	TWeakObjectPtr<UMiniAbilitySystemComponent> OriginalASC;
};
