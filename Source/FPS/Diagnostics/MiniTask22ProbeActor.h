#pragma once

#include "Arena/MiniMatchTypes.h"
#include "GameFramework/Actor.h"
#include "GameModes/MiniGamePhaseTypes.h"
#include "MiniTask22ProbeActor.generated.h"

FPS_API bool MiniTask22SameMatchState(const FMiniMatchState& A, const FMiniMatchState& B);
FPS_API bool MiniTask22SamePhaseState(const FMiniGamePhaseState& A, const FMiniGamePhaseState& B);

/** Opt-in diagnostic owner bridge. Acknowledgements contain the client's real replicated state. */
UCLASS(NotBlueprintable, Transient)
class FPS_API AMiniTask22ProbeActor : public AActor
{
	GENERATED_BODY()
public:
	AMiniTask22ProbeActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void InitializeServer(int32 Index);
	void SetCheckpoint(int32 Serial, FName Name, const FMiniGamePhaseState& Phase,
		const FMiniMatchState& Match, bool bRemoved);
	int32 GetOwnerIndex() const { return OwnerIndex; }
	int32 GetCheckpointSerial() const { return CheckpointSerial; }
	FName GetCheckpointName() const { return CheckpointName; }
	bool HasAcknowledged() const { return CheckpointSerial > 0 && AcknowledgedSerial == CheckpointSerial; }
	bool ExpectsRemovedRules() const { return bRulesRemoved; }
	const FMiniGamePhaseState& GetExpectedPhase() const { return ExpectedPhase; }
	const FMiniMatchState& GetExpectedMatch() const { return ExpectedMatch; }
	UFUNCTION(Server, Reliable)
	void ServerAcknowledge(int32 Serial, const FMiniGamePhaseState& Phase, const FMiniMatchState& Match,
		const FMiniPlayerMatchStats& Stats, double ServerClock, bool bHUDMatches, bool bMutationRejected);
	UFUNCTION(Client, Reliable)
	void ClientLeaveServer();
private:
	UPROPERTY(Replicated) int32 OwnerIndex = 0;
	UPROPERTY(Replicated) int32 CheckpointSerial = 0;
	UPROPERTY(Replicated) FName CheckpointName;
	UPROPERTY(Replicated) FMiniGamePhaseState ExpectedPhase;
	UPROPERTY(Replicated) FMiniMatchState ExpectedMatch;
	UPROPERTY(Replicated) bool bRulesRemoved = false;
	int32 AcknowledgedSerial = 0;
};
