#pragma once

#include "Arena/MiniMatchTypes.h"
#include "GameFramework/Actor.h"
#include "GameModes/MiniGamePhaseTypes.h"
#include "MiniTask23ProbeActor.generated.h"

class AMiniCharacter;

FPS_API bool MiniTask23SameMatch(const FMiniMatchState& A, const FMiniMatchState& B);
FPS_API bool MiniTask23SamePhase(const FMiniGamePhaseState& A, const FMiniGamePhaseState& B);

/** Opt-in owner bridge: clients acknowledge their real map, HUD and replicated state. */
UCLASS(NotBlueprintable, Transient)
class FPS_API AMiniTask23ProbeActor : public AActor
{
	GENERATED_BODY()
public:
	AMiniTask23ProbeActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void InitializeServer(int32 Index);
	void SetCheckpoint(int32 Serial, FName Name, const FMiniGamePhaseState& Phase,
		const FMiniMatchState& Match, bool bLivePawn);
	int32 GetOwnerIndex() const { return OwnerIndex; }
	int32 GetCheckpointSerial() const { return CheckpointSerial; }
	FName GetCheckpointName() const { return CheckpointName; }
	bool HasAcknowledged() const { return CheckpointSerial > 0 && AcknowledgedSerial == CheckpointSerial; }
	bool RequiresLivePawn() const { return bRequireLivePawn; }
	uint32 GetExpectedLifeId() const { return ExpectedLifeId; }
	AMiniCharacter* GetExpectedPawn() const { return ExpectedPawn; }
	const FMiniGamePhaseState& GetExpectedPhase() const { return ExpectedPhase; }
	const FMiniMatchState& GetExpectedMatch() const { return ExpectedMatch; }
	UFUNCTION(Server, Reliable)
	void ServerAcknowledge(int32 Serial, const FMiniGamePhaseState& Phase, const FMiniMatchState& Match,
		const FMiniPlayerMatchStats& Stats, uint32 LifeId, AMiniCharacter* Pawn,
		double ServerClock, bool bHUDMatches, bool bMapMatches);
private:
	UPROPERTY(Replicated) int32 OwnerIndex = 0;
	UPROPERTY(Replicated) int32 CheckpointSerial = 0;
	UPROPERTY(Replicated) FName CheckpointName;
	UPROPERTY(Replicated) FMiniGamePhaseState ExpectedPhase;
	UPROPERTY(Replicated) FMiniMatchState ExpectedMatch;
	UPROPERTY(Replicated) uint32 ExpectedLifeId = 0;
	UPROPERTY(Replicated) TObjectPtr<AMiniCharacter> ExpectedPawn;
	UPROPERTY(Replicated) bool bRequireLivePawn = true;
	int32 AcknowledgedSerial = 0;
};
