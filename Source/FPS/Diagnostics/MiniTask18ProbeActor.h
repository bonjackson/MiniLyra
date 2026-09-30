#pragma once

#include "GameFramework/Actor.h"
#include "MiniTask18ProbeActor.generated.h"

class AMiniCharacter;

/** Owner-only checkpoints keep two clients synchronized with server observations. */
UENUM()
enum class EMiniTask18Phase : uint8
{
	Initial,
	Fire,
	ReloadStart,
	ReloadCancel,
	RapidReloadCancel,
	DeathReloadStart,
	Death,
	Complete
};

UCLASS(NotBlueprintable, Transient)
class FPS_API AMiniTask18ProbeActor : public AActor
{
	GENERATED_BODY()

public:
	AMiniTask18ProbeActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitializeServer(int32 InOwnerIndex, AMiniCharacter* InOwnerPawn, AMiniCharacter* InPeerPawn);
	void SetServerPhase(EMiniTask18Phase InPhase);
	bool HasAcknowledged(EMiniTask18Phase InPhase) const;

	int32 GetOwnerIndex() const { return OwnerIndex; }
	AMiniCharacter* GetOwnerPawn() const { return OwnerPawn; }
	AMiniCharacter* GetPeerPawn() const { return PeerPawn; }
	EMiniTask18Phase GetPhase() const { return Phase; }

	UFUNCTION(Server, Reliable)
	void ServerAcknowledge(EMiniTask18Phase AcknowledgedPhase);

private:
	UPROPERTY(Replicated)
	int32 OwnerIndex = 0;

	UPROPERTY(Replicated)
	TObjectPtr<AMiniCharacter> OwnerPawn;

	UPROPERTY(Replicated)
	TObjectPtr<AMiniCharacter> PeerPawn;

	UPROPERTY(Replicated)
	EMiniTask18Phase Phase = EMiniTask18Phase::Initial;

	int32 LastAcknowledgedPhase = INDEX_NONE;
};
