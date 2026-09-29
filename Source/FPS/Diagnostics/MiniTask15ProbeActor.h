#pragma once

#include "GameFramework/Actor.h"
#include "MiniTask15ProbeActor.generated.h"

class AMiniCharacter;

/** Synchronized checkpoints for the two owner-only Task 15 probe actors. */
UENUM()
enum class EMiniTask15Phase : uint8
{
	Initial,
	RequestQ,
	Pistol,
	RifleAgain,
	FirstDead,
	FirstRespawn,
	SecondDead,
	SecondRespawn,
	Complete
};

/** Private RPC bridge; Pawn references allow each client to inspect both public appearances. */
UCLASS(NotBlueprintable, Transient)
class FPS_API AMiniTask15ProbeActor : public AActor
{
	GENERATED_BODY()

public:
	AMiniTask15ProbeActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitializeServer(int32 InOwnerIndex, AMiniCharacter* InOwnerPawn, AMiniCharacter* InPeerPawn);
	void SetServerPawns(AMiniCharacter* InOwnerPawn, AMiniCharacter* InPeerPawn);
	void SetServerPhase(EMiniTask15Phase InPhase);
	bool HasAcknowledged(EMiniTask15Phase InPhase) const;

	int32 GetOwnerIndex() const { return OwnerIndex; }
	AMiniCharacter* GetOwnerPawn() const { return OwnerPawn; }
	AMiniCharacter* GetPeerPawn() const { return PeerPawn; }
	EMiniTask15Phase GetPhase() const { return Phase; }

	UFUNCTION(Server, Reliable)
	void ServerAcknowledge(EMiniTask15Phase AcknowledgedPhase);

private:
	UPROPERTY(Replicated)
	int32 OwnerIndex = 0;

	UPROPERTY(Replicated)
	TObjectPtr<AMiniCharacter> OwnerPawn;

	UPROPERTY(Replicated)
	TObjectPtr<AMiniCharacter> PeerPawn;

	UPROPERTY(Replicated)
	EMiniTask15Phase Phase = EMiniTask15Phase::Initial;

	int32 LastAcknowledgedPhase = INDEX_NONE;
};
