#pragma once

#include "Subsystems/WorldSubsystem.h"
#include "MiniTask21ProbeActor.h"
#include "MiniTask21ProbeSubsystem.generated.h"

class UMiniArenaRulesComponent;
class UMiniGamePhaseAbility;

/** Development-only acceptance: queries normal gameplay and uses only its public phase APIs. */
UCLASS()
class FPS_API UMiniTask21ProbeSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
private:
	void TickServer(const FString& Mode);
	void TickClient();
	void TickPractice();
	void Fail(const TCHAR* Reason);
	bool AllAcknowledged(EMiniTask21Checkpoint Checkpoint) const;
	void PublishCheckpoint(EMiniTask21Checkpoint Checkpoint);
	float WaitSeconds = 0;
	float StableSeconds = 0;
	bool bDone = false;
	bool bReadyLogged = false;
	bool bClientMutationRejected = false;
	int32 StartedPhaseCount = 0;
	int32 ServerStep = 0;
	int32 ClientLastCheckpoint = INDEX_NONE;
	int32 ClientReadyCheckpoint = INDEX_NONE;
	double ClientCheckpointReadyAt = -1.0;
	double CancelledDeadline = 0;
	double PreviousDeadline = 0;
	uint32 CancelledRevision = 0;
	TArray<TWeakObjectPtr<AMiniTask21ProbeActor>> OwnerProbes;
	TWeakObjectPtr<UMiniArenaRulesComponent> RemovedRules;
	TWeakObjectPtr<UMiniGamePhaseAbility> CancelledAbility;
};
