#pragma once

#include "Components/ActorComponent.h"
#include "MiniQuickBarComponent.generated.h"

class AMiniCharacter;
class UMiniInventoryItemInstance;

/** Two private inventory slots on the controller; only the server equips a slot. */
UCLASS(ClassGroup=(Mini), meta=(BlueprintSpawnableComponent))
class FPS_API UMiniQuickBarComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	FSimpleMulticastDelegate OnChanged;
	UMiniQuickBarComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Idempotent for one Pawn; a replacement receives fresh item instances and ammo. */
	bool InitializeForPawn(AMiniCharacter* Pawn);
	/** Clears the active equipment before this Pawn loses its ASC Avatar. */
	void HandlePawnLost(AMiniCharacter* Pawn);
	/** Called before a server-owned inventory item is removed. */
	void HandleItemRemoved(UMiniInventoryItemInstance* Item);
	bool SelectSlot(int32 SlotIndex);
	void RequestNextSlot();
	int32 GetActiveSlotIndex() const { return ActiveSlotIndex; }
	FGuid GetSlotItemId(int32 SlotIndex) const;
	UMiniInventoryItemInstance* GetSlotItem(int32 SlotIndex) const;
	FString GetDebugSnapshot() const;

private:
	bool SelectNextAvailableSlot();
	UFUNCTION(Server, Reliable)
	void ServerSelectSlot(int32 SlotIndex);
	UFUNCTION(Server, Reliable)
	void ServerSelectNextSlot();
	UFUNCTION()
	void OnRep_Slots();
	UFUNCTION()
	void OnRep_ActiveSlotIndex();

	UPROPERTY(ReplicatedUsing=OnRep_Slots)
	TArray<FGuid> Slots;
	UPROPERTY(ReplicatedUsing=OnRep_ActiveSlotIndex)
	int32 ActiveSlotIndex = INDEX_NONE;
	TWeakObjectPtr<AMiniCharacter> BoundPawn;
	bool bApplyingLoadout = false;
};
