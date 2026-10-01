#pragma once

#include "GameFramework/Actor.h"
#include "GameModes/MiniGamePhaseTypes.h"
#include "MiniTask21ProbeActor.generated.h"

UENUM()
enum class EMiniTask21Checkpoint : uint8
{
	Warmup, Playing, PostMatch, None, Restart, Removed
};

/** Opt-in owner checkpoints report an actual client's local replicated phase and clock. */
UCLASS(NotBlueprintable, Transient)
class FPS_API AMiniTask21ProbeActor : public AActor
{
	GENERATED_BODY()
public:
	AMiniTask21ProbeActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void InitializeServer(int32 Index);
	void SetCheckpoint(EMiniTask21Checkpoint Value, const FMiniGamePhaseState& State);
	bool HasAcknowledged(EMiniTask21Checkpoint Value) const;
	int32 GetOwnerIndex() const { return OwnerIndex; }
	EMiniTask21Checkpoint GetCheckpoint() const { return Checkpoint; }
	const FMiniGamePhaseState& GetExpectedState() const { return ExpectedState; }
	UFUNCTION(Server, Reliable)
	void ServerAcknowledge(EMiniTask21Checkpoint Value, const FMiniGamePhaseState& ClientState,
		double ClientServerClock, bool bMutationRejected);
private:
	UPROPERTY(Replicated) int32 OwnerIndex = 0;
	UPROPERTY(Replicated) EMiniTask21Checkpoint Checkpoint = EMiniTask21Checkpoint::Warmup;
	UPROPERTY(Replicated) FMiniGamePhaseState ExpectedState;
	uint8 AcknowledgedMask = 0;
};
