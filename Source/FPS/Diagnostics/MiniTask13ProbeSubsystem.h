#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MiniTask13ProbeSubsystem.generated.h"

class AMiniCharacter;
class UMiniAbilitySystemComponent;

/** Two-process acceptance probe for damage, delayed respawns, and forced Pawn replacement. */
UCLASS()
class FPS_API UMiniTask13ProbeSubsystem : public UTickableWorldSubsystem
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
		WaitNonlethal,
		WaitFirstDeath,
		WaitFirstRespawn,
		WaitFirstAim,
		WaitFirstAimReleased,
		WaitFirstStable,
		WaitSecondDeath,
		WaitSecondRespawn,
		WaitSecondAim,
		WaitSecondAimReleased,
		WaitSecondStable,
		WaitForcedRespawn,
		WaitForcedAim,
		WaitForcedAimReleased,
		WaitForcedStable,
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
	int32 OriginalAbilityCount = INDEX_NONE;
	FString LastPawnPath;
	TWeakObjectPtr<AMiniCharacter> LastPawn;
	TWeakObjectPtr<UMiniAbilitySystemComponent> OriginalASC;
};
