#pragma once

#include "GameFramework/Actor.h"
#include "MiniTask19ProbeActor.generated.h"

class AMiniCharacter;

UENUM()
enum class EMiniTask19Phase : uint8
{
	Initial, HostAuthorityState, RifleFire, SwitchWeapon, LateHUDRemoved, LateHUDState, LateHUDRestored,
	MenuOpen, MenuClosed, RootRemoved, RootRestored, Death, Respawn,
	FeatureMenu, FeatureOff, Complete
};

/** Owner-only acknowledgements report local UI observations, never author gameplay state. */
UCLASS(NotBlueprintable, Transient)
class FPS_API AMiniTask19ProbeActor : public AActor
{
	GENERATED_BODY()
public:
	AMiniTask19ProbeActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void InitializeServer(int32 Index, AMiniCharacter* Pawn, AMiniCharacter* Peer);
	void UpdateServerPawns(AMiniCharacter* Pawn, AMiniCharacter* Peer);
	void SetServerPhase(EMiniTask19Phase Value);
	bool HasAcknowledged(EMiniTask19Phase Value) const;
	int32 GetOwnerIndex() const { return OwnerIndex; }
	AMiniCharacter* GetOwnerPawn() const { return OwnerPawn; }
	AMiniCharacter* GetPeerPawn() const { return PeerPawn; }
	EMiniTask19Phase GetPhase() const { return Phase; }
	UFUNCTION(Server, Reliable)
	void ServerAcknowledge(EMiniTask19Phase Value);
private:
	UPROPERTY(Replicated) int32 OwnerIndex = 0;
	UPROPERTY(Replicated) TObjectPtr<AMiniCharacter> OwnerPawn;
	UPROPERTY(Replicated) TObjectPtr<AMiniCharacter> PeerPawn;
	UPROPERTY(Replicated) EMiniTask19Phase Phase = EMiniTask19Phase::Initial;
	int32 LastAcknowledged = INDEX_NONE;
};
