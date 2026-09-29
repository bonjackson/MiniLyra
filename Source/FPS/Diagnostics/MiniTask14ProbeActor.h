#pragma once

#include "GameFramework/Actor.h"
#include "MiniTask14ProbeActor.generated.h"

class UMiniInventoryItemInstance;

/** Owner-only RPC bridge for the Task 14 inventory replication acceptance probe. */
UCLASS(NotBlueprintable, Transient)
class FPS_API AMiniTask14ProbeActor : public AActor
{
	GENERATED_BODY()

public:
	AMiniTask14ProbeActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void InitializeServer(int32 InOwnerIndex, UMiniInventoryItemInstance* Rifle,
		UMiniInventoryItemInstance* Pistol);
	int32 GetOwnerIndex() const { return OwnerIndex; }

	UFUNCTION(Server, Reliable)
	void ServerChangeRifleStat();

	UFUNCTION(Server, Reliable)
	void ServerRemoveRifle();

	UFUNCTION(Server, Reliable)
	void ServerAddRifle();

private:
	enum class EPhase : uint8
	{
		Initial,
		RifleStatChanged,
		RifleRemoved,
		RifleReadded
	};

	void Fail(const TCHAR* Reason);
	bool HasExpectedInventory(bool bHasRifle, UMiniInventoryItemInstance* ExpectedRifle,
		int32 ExpectedRifleAmmo = 30) const;

	UPROPERTY(Replicated)
	int32 OwnerIndex = 0;

	TWeakObjectPtr<UMiniInventoryItemInstance> InitialRifle;
	TWeakObjectPtr<UMiniInventoryItemInstance> InitialPistol;
	FGuid InitialRifleId;
	FGuid InitialPistolId;
	EPhase Phase = EPhase::Initial;
	bool bFailed = false;
};
