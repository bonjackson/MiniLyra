#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MiniTask26NetworkProbeSubsystem.generated.h"

class AMiniCharacter;
class AMiniTask26NetworkProbeActor;

/** Four independent peers, three real owning-client weapon suites, late join and profile recovery. */
UCLASS()
class FPS_API UMiniTask26NetworkProbeSubsystem final : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
private:
	void Fail(const TCHAR* Reason);
	TArray<TWeakObjectPtr<AMiniTask26NetworkProbeActor>> OwnerProbes;
	TWeakObjectPtr<AMiniCharacter> OriginalHostPawn;
	float WaitSeconds = 0;
	double DeathAt = 0;
	double FinishedAt = 0;
	int32 ActiveOwner = 0;
	int32 Step = 0;
	uint32 OriginalHostLife = 0;
	bool bLateJoinPublished = false;
	bool bDone = false;
};
