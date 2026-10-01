#pragma once

#include "GameFramework/Actor.h"
#include "MiniTask20ProbeActor.generated.h"

class AMiniCharacter;
class AMiniPracticeTarget;
class AMiniPracticeSupply;

UENUM()
enum class EMiniTask20Phase : uint8
{
	Initial, RifleOne, RifleTwo, RifleThree, RifleFour, DisabledShot, Reset,
	PistolSwitch, PistolFire, ReloadStart, ReloadComplete, Rejections,
	SupplyEmpty, SupplyEnter, SupplyLeave, SupplyOutside, CoreDeactivate, Complete
};

/** Owner-only checkpoints observe gameplay; they cannot author damage or target state. */
UCLASS(NotBlueprintable, Transient)
class FPS_API AMiniTask20ProbeActor : public AActor
{
	GENERATED_BODY()
public:
	AMiniTask20ProbeActor();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void InitializeServer(int32 Index, AMiniCharacter* Pawn, AMiniCharacter* Peer,
		AMiniPracticeTarget* Target, AMiniPracticeSupply* Supply);
	void SetServerPhase(EMiniTask20Phase Value);
	bool HasAcknowledged(EMiniTask20Phase Value) const;
	int32 GetOwnerIndex() const { return OwnerIndex; }
	AMiniCharacter* GetOwnerPawn() const { return OwnerPawn; }
	AMiniCharacter* GetPeerPawn() const { return PeerPawn; }
	AMiniPracticeTarget* GetTarget() const { return Target; }
	AMiniPracticeSupply* GetSupply() const { return Supply; }
	EMiniTask20Phase GetPhase() const { return Phase; }
	UFUNCTION(Server, Reliable) void ServerAcknowledge(EMiniTask20Phase Value);
private:
	UPROPERTY(Replicated) int32 OwnerIndex = 0;
	UPROPERTY(Replicated) TObjectPtr<AMiniCharacter> OwnerPawn;
	UPROPERTY(Replicated) TObjectPtr<AMiniCharacter> PeerPawn;
	UPROPERTY(Replicated) TObjectPtr<AMiniPracticeTarget> Target;
	UPROPERTY(Replicated) TObjectPtr<AMiniPracticeSupply> Supply;
	UPROPERTY(Replicated) EMiniTask20Phase Phase = EMiniTask20Phase::Initial;
	int32 LastAcknowledged = INDEX_NONE;
};
