#pragma once

#include "Arena/MiniMatchTypes.h"
#include "GameFramework/Actor.h"
#include "GameModes/MiniGamePhaseTypes.h"
#include "MiniTask25ProbeActor.generated.h"

class AMiniCharacter;
class AMiniPlayerState;

UENUM()
enum class EMiniTask25Command : uint8 { None, ArmFire, ArmReload, LeaveAndRejoin, LeaveOnly };

/** Actual owner measurements. Server-only grant arrays are never used as client evidence. */
USTRUCT()
struct FMiniTask25Observation
{
	GENERATED_BODY()
	UPROPERTY() int32 InputBindings = -1;
	UPROPERTY() int32 HeldInput = -1;
	UPROPERTY() int32 SavedMoves = -1;
	UPROPERTY() int32 EquipmentSpecs = -1;
	UPROPERTY() int32 OldEquipmentSpecs = -1;
	UPROPERTY() int32 HUDWidgets = -1;
	UPROPERTY() int32 VMBindings = -1;
	UPROPERTY() bool bMovementTick = false;
	UPROPERTY() bool bMapping = false;
	UPROPERTY() bool bHUDMatches = false;
	UPROPERTY() bool bFreshInventory = false;
	UPROPERTY() FGuid RifleId;
	UPROPERTY() FGuid PistolId;
};

FPS_API bool MiniTask25SameMatch(const FMiniMatchState& A, const FMiniMatchState& B);
FPS_API bool MiniTask25SamePhase(const FMiniGamePhaseState& A, const FMiniGamePhaseState& B);

/** Opt-in owner-relevant checkpoints. No production state is changed by an ACK. */
UCLASS(NotBlueprintable, Transient)
class FPS_API AMiniTask25ProbeActor : public AActor
{
	GENERATED_BODY()
public:
	AMiniTask25ProbeActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void InitializeServer(int32 Index);
	void SetCheckpoint(int32 InSerial, FName Name, int32 InIteration, bool bDead, bool bFresh,
		const FMiniGamePhaseState& Phase, const FMiniMatchState& Match);
	void SetCommand(EMiniTask25Command Value);
	int32 GetOwnerIndex() const { return OwnerIndex; }
	int32 GetSerial() const { return Serial; }
	int32 GetIteration() const { return Iteration; }
	FName GetCheckpointName() const { return CheckpointName; }
	bool ExpectsDead() const { return bExpectDead; }
	bool ExpectsFreshInventory() const { return bExpectFresh; }
	bool HasAcknowledged() const { return Serial > 0 && AcknowledgedSerial == Serial; }
	EMiniTask25Command GetCommand() const { return Command; }
	uint32 GetExpectedLife() const { return ExpectedLife; }
	AMiniCharacter* GetExpectedPawn() const { return ExpectedPawn; }
	AMiniPlayerState* GetExpectedPlayerState() const { return ExpectedPlayerState; }
	const FMiniGamePhaseState& GetExpectedPhase() const { return ExpectedPhase; }
	const FMiniMatchState& GetExpectedMatch() const { return ExpectedMatch; }
	UFUNCTION(Server, Reliable)
	void ServerAcknowledge(int32 InSerial, const FMiniGamePhaseState& Phase, const FMiniMatchState& Match,
		const FMiniPlayerMatchStats& Stats, uint32 Life, AMiniCharacter* Pawn, AMiniPlayerState* PlayerState,
		double ServerClock, const FMiniTask25Observation& Observation);
private:
	UPROPERTY(Replicated) int32 OwnerIndex = 0;
	UPROPERTY(Replicated) int32 Serial = 0;
	UPROPERTY(Replicated) int32 Iteration = 0;
	UPROPERTY(Replicated) FName CheckpointName;
	UPROPERTY(Replicated) bool bExpectDead = false;
	UPROPERTY(Replicated) bool bExpectFresh = true;
	UPROPERTY(Replicated) uint32 ExpectedLife = 0;
	UPROPERTY(Replicated) TObjectPtr<AMiniCharacter> ExpectedPawn;
	UPROPERTY(Replicated) TObjectPtr<AMiniPlayerState> ExpectedPlayerState;
	UPROPERTY(Replicated) FMiniGamePhaseState ExpectedPhase;
	UPROPERTY(Replicated) FMiniMatchState ExpectedMatch;
	UPROPERTY(Replicated) EMiniTask25Command Command = EMiniTask25Command::None;
	int32 AcknowledgedSerial = 0;
};
