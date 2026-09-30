#pragma once

#include "GameFramework/Actor.h"
#include "MiniTask17ProbeActor.generated.h"

class AMiniCharacter;

/** Owner-only checkpoints for a two-client ammo and reload acceptance run. */
UENUM()
enum class EMiniTask17Phase : uint8
{
	Initial,
	RifleShot,
	FullReload,
	EmptyFire,
	RifleReload,
	PartialReload,
	NoReserveReload,
	RifleReloadCancelStart,
	RifleReloadCancelSwitch,
	PistolShot,
	PistolReload,
	PistolReloadCancelStart,
	Death,
	Complete
};

UCLASS(NotBlueprintable, Transient)
class FPS_API AMiniTask17ProbeActor : public AActor
{
	GENERATED_BODY()

public:
	AMiniTask17ProbeActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitializeServer(int32 InOwnerIndex, AMiniCharacter* InOwnerPawn, AMiniCharacter* InPeerPawn);
	void SetServerPhase(EMiniTask17Phase InPhase);
	bool HasAcknowledged(EMiniTask17Phase InPhase) const;

	int32 GetOwnerIndex() const { return OwnerIndex; }
	AMiniCharacter* GetOwnerPawn() const { return OwnerPawn; }
	AMiniCharacter* GetPeerPawn() const { return PeerPawn; }
	EMiniTask17Phase GetPhase() const { return Phase; }

	UFUNCTION(Server, Reliable)
	void ServerAcknowledge(EMiniTask17Phase AcknowledgedPhase);

private:
	UPROPERTY(Replicated)
	int32 OwnerIndex = 0;

	UPROPERTY(Replicated)
	TObjectPtr<AMiniCharacter> OwnerPawn;

	UPROPERTY(Replicated)
	TObjectPtr<AMiniCharacter> PeerPawn;

	UPROPERTY(Replicated)
	EMiniTask17Phase Phase = EMiniTask17Phase::Initial;

	int32 LastAcknowledgedPhase = INDEX_NONE;
};
