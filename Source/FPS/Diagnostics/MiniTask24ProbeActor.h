#pragma once

#include "Arena/MiniMatchTypes.h"
#include "GameFramework/Actor.h"
#include "GameModes/MiniGamePhaseTypes.h"
#include "MiniTask24ProbeActor.generated.h"

class AMiniCharacter;

UENUM()
enum class EMiniTask24Command : uint8
{
	None,
	LeaveAndRejoin,
	ReleaseSlot,
	QuitClient
};

FPS_API bool MiniTask24SameMatch(const FMiniMatchState& A, const FMiniMatchState& B);
FPS_API bool MiniTask24SamePhase(const FMiniGamePhaseState& A, const FMiniGamePhaseState& B);

/** Opt-in network checkpoint only; all requested UI actions still use routed Slate input. */
UCLASS(NotBlueprintable, Transient)
class FPS_API AMiniTask24ProbeActor : public AActor
{
	GENERATED_BODY()
public:
	AMiniTask24ProbeActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void InitializeServer(int32 Index);
	void SetCheckpoint(int32 Serial, FName Name, const FMiniGamePhaseState& Phase, const FMiniMatchState& Match);
	void SetCommand(EMiniTask24Command Command);
	int32 GetOwnerIndex() const { return OwnerIndex; }
	int32 GetSerial() const { return Serial; }
	FName GetNameForCheckpoint() const { return CheckpointName; }
	bool HasAcknowledged() const { return Serial > 0 && AcknowledgedSerial == Serial; }
	EMiniTask24Command GetCommand() const { return Command; }
	uint32 GetExpectedLifeId() const { return ExpectedLifeId; }
	AMiniCharacter* GetExpectedPawn() const { return ExpectedPawn; }
	const FMiniGamePhaseState& GetExpectedPhase() const { return ExpectedPhase; }
	const FMiniMatchState& GetExpectedMatch() const { return ExpectedMatch; }
	UFUNCTION(Server, Reliable)
	void ServerAcknowledge(int32 InSerial, FName Experience, const FMiniGamePhaseState& Phase,
		const FMiniMatchState& Match, const FMiniPlayerMatchStats& Stats, uint32 LifeId, AMiniCharacter* Pawn, double ServerClock,
		bool bHUDReady, bool bUIGateClear);
private:
	UPROPERTY(Replicated) int32 OwnerIndex = 0;
	UPROPERTY(Replicated) int32 Serial = 0;
	UPROPERTY(Replicated) FName CheckpointName;
	UPROPERTY(Replicated) FMiniGamePhaseState ExpectedPhase;
	UPROPERTY(Replicated) FMiniMatchState ExpectedMatch;
	UPROPERTY(Replicated) uint32 ExpectedLifeId = 0;
	UPROPERTY(Replicated) TObjectPtr<AMiniCharacter> ExpectedPawn;
	UPROPERTY(Replicated) EMiniTask24Command Command = EMiniTask24Command::None;
	int32 AcknowledgedSerial = 0;
};
