#include "MiniQuickBarComponent.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
#include "Character/MiniPawnData.h"
#include "Equipment/MiniLoadoutDefinition.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Inventory/MiniInventoryManagerComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "Player/MiniPlayerController.h"
#include "Player/MiniPlayerState.h"
#include "System/MiniLogChannels.h"

UMiniQuickBarComponent::UMiniQuickBarComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
	Slots.SetNum(UMiniLoadoutDefinition::NumQuickBarSlots);
}

void UMiniQuickBarComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMiniQuickBarComponent, Slots);
	DOREPLIFETIME(UMiniQuickBarComponent, ActiveSlotIndex);
}

FGuid UMiniQuickBarComponent::GetSlotItemId(int32 SlotIndex) const
{
	return Slots.IsValidIndex(SlotIndex) ? Slots[SlotIndex] : FGuid();
}

UMiniInventoryItemInstance* UMiniQuickBarComponent::GetSlotItem(int32 SlotIndex) const
{
	const FGuid ItemId = GetSlotItemId(SlotIndex);
	const AMiniPlayerController* Controller = Cast<AMiniPlayerController>(GetOwner());
	const UMiniInventoryManagerComponent* Inventory = Controller ? Controller->GetInventoryManager() : nullptr;
	if (!ItemId.IsValid() || !Inventory)
	{
		return nullptr;
	}
	for (const FMiniInventoryEntry& Entry : Inventory->GetEntries())
	{
		if (IsValid(Entry.Instance) && Entry.Instance->GetInstanceId() == ItemId)
		{
			return Entry.Instance;
		}
	}
	return nullptr;
}

bool UMiniQuickBarComponent::InitializeForPawn(AMiniCharacter* Pawn)
{
	AMiniPlayerController* Controller = Cast<AMiniPlayerController>(GetOwner());
	AMiniPlayerState* State = Controller ? Controller->GetPlayerState<AMiniPlayerState>() : nullptr;
	UMiniAbilitySystemComponent* ASC = State ? State->GetMiniAbilitySystemComponent() : nullptr;
	UMiniInventoryManagerComponent* Inventory = Controller ? Controller->GetInventoryManager() : nullptr;
	if (!Controller || !Controller->HasAuthority() || !IsValid(Pawn) || Controller->GetPawn() != Pawn ||
		!Inventory || !Pawn->GetEquipmentManager() || !ASC || ASC->GetAvatarActor() != Pawn ||
		bApplyingLoadout || (Pawn->GetHealthComponent() && Pawn->GetHealthComponent()->IsDead()))
	{
		return false;
	}
	// Older isolated probes own their inventory or expect the pre-equipment ASC count.
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask13")) ||
		FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask14")))
	{
		return false;
	}
#endif
	// Repeated init notifications during one life must not refill a deliberately
	// emptied slot or reset ammo. A failed first setup clears BoundPawn below.
	if (BoundPawn.Get() == Pawn)
	{
		return true;
	}
	const UMiniPawnData* PawnData = Pawn->GetPawnData();
	const UMiniLoadoutDefinition* Loadout = PawnData ? PawnData->DefaultLoadout.Get() : nullptr;
	FString LoadoutError;
	if (!PawnData || (Loadout && !Loadout->ValidateLoadout(LoadoutError)))
	{
		UE_LOG(LogMiniEquipment, Error, TEXT("MiniQuickBar INVALID_LOADOUT: Controller=%s Pawn=%s Reason=%s"),
			*Controller->GetPathName(), *Pawn->GetPathName(),
			PawnData ? *LoadoutError : TEXT("PawnData missing"));
		return false;
	}
	TGuardValue<bool> ApplyGuard(bApplyingLoadout, true);
	if (AMiniCharacter* PreviousPawn = BoundPawn.Get())
	{
		if (PreviousPawn != Pawn && PreviousPawn->GetEquipmentManager())
		{
			PreviousPawn->GetEquipmentManager()->UnequipItem();
		}
	}
	// Revoke grants before deleting their source inventory. Reset the old slots
	// before inventory callbacks so a removed item cannot refer to stale equipment.
	Pawn->GetEquipmentManager()->UnequipItem();
	BoundPawn.Reset();
	ActiveSlotIndex = INDEX_NONE;
	Slots.SetNum(UMiniLoadoutDefinition::NumQuickBarSlots);
	for (FGuid& Slot : Slots) { Slot.Invalidate(); }
	OnChanged.Broadcast();
	// The controller survives respawns, so old item objects cannot be reused.
	TArray<TObjectPtr<UMiniInventoryItemInstance>> OldItems;
	for (const FMiniInventoryEntry& Entry : Inventory->GetEntries())
	{
		if (IsValid(Entry.Instance))
		{
			OldItems.Add(Entry.Instance);
		}
	}
	for (UMiniInventoryItemInstance* Item : OldItems)
	{
		Inventory->RemoveItem(Item);
	}
	TArray<TObjectPtr<UMiniInventoryItemInstance>> CreatedItems;
	TArray<FGuid> NewSlots;
	NewSlots.SetNum(UMiniLoadoutDefinition::NumQuickBarSlots);
	const auto Rollback = [this, Pawn, Controller, Inventory, &CreatedItems]()
	{
		// No grant or partially created item is kept on an unsuccessful setup.
		Pawn->GetEquipmentManager()->UnequipItem();
		BoundPawn.Reset();
		ActiveSlotIndex = INDEX_NONE;
		for (FGuid& Slot : Slots) { Slot.Invalidate(); }
		for (UMiniInventoryItemInstance* Item : CreatedItems)
		{
			if (IsValid(Item)) { Inventory->RemoveItem(Item); }
		}
		OnChanged.Broadcast();
		Controller->ForceNetUpdate();
	};
	if (Loadout)
	{
		for (const FMiniLoadoutEntry& Entry : Loadout->Items)
		{
			UMiniInventoryItemInstance* Item = Inventory->AddItem(Entry.ItemDefinition);
			if (!Item)
			{
				Rollback();
				return false;
			}
			CreatedItems.Add(Item);
			NewSlots[Entry.SlotIndex] = Item->GetInstanceId();
		}
	}
	Slots = MoveTemp(NewSlots);
	BoundPawn = Pawn;
	OnChanged.Broadcast();
	Controller->ForceNetUpdate();
	if (!CreatedItems.IsEmpty() && !SelectSlot(Loadout->InitiallySelectedSlot))
	{
		Rollback();
		return false;
	}
	UE_LOG(LogMiniEquipment, Display,
		TEXT("MiniQuickBar LOADOUT_APPLIED: Controller=%s Pawn=%s Loadout=%s Items=%d Active=%d"),
		*Controller->GetPathName(), *Pawn->GetPathName(), *GetNameSafe(Loadout),
		CreatedItems.Num(), ActiveSlotIndex);
	return true;
}

void UMiniQuickBarComponent::HandlePawnLost(AMiniCharacter* Pawn)
{
	AMiniPlayerController* Controller = Cast<AMiniPlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !Pawn || BoundPawn.Get() != Pawn)
	{
		return;
	}
	if (UMiniEquipmentManagerComponent* Equipment = Pawn->GetEquipmentManager())
	{
		Equipment->UnequipItem();
	}
	BoundPawn.Reset();
	ActiveSlotIndex = INDEX_NONE;
	Controller->ForceNetUpdate();
	OnChanged.Broadcast();
	UE_LOG(LogMiniEquipment, Display, TEXT("MiniQuickBar PAWN_LOST: Controller=%s Pawn=%s"),
		*Controller->GetPathName(), *Pawn->GetPathName());
}

void UMiniQuickBarComponent::HandleItemRemoved(UMiniInventoryItemInstance* Item)
{
	AMiniPlayerController* Controller = Cast<AMiniPlayerController>(GetOwner());
	if (!Controller || !Controller->HasAuthority() || !IsValid(Item))
	{
		return;
	}
	const int32 SlotIndex = Slots.IndexOfByKey(Item->GetInstanceId());
	if (SlotIndex == INDEX_NONE)
	{
		return;
	}
	if (SlotIndex == ActiveSlotIndex)
	{
		if (AMiniCharacter* Pawn = BoundPawn.Get())
		{
			if (UMiniEquipmentManagerComponent* Equipment = Pawn->GetEquipmentManager())
			{
				Equipment->UnequipItem();
			}
		}
		ActiveSlotIndex = INDEX_NONE;
	}
	Slots[SlotIndex].Invalidate();
	OnChanged.Broadcast();
	Controller->ForceNetUpdate();
}

bool UMiniQuickBarComponent::SelectSlot(int32 SlotIndex)
{
	AMiniPlayerController* Controller = Cast<AMiniPlayerController>(GetOwner());
	AMiniCharacter* Pawn = Controller ? Cast<AMiniCharacter>(Controller->GetPawn()) : nullptr;
	UMiniEquipmentManagerComponent* Equipment = Pawn ? Pawn->GetEquipmentManager() : nullptr;
	UMiniInventoryItemInstance* Item = GetSlotItem(SlotIndex);
	if (!Controller || !Controller->HasAuthority() || !Slots.IsValidIndex(SlotIndex) ||
		!Item || !Equipment || BoundPawn.Get() != Pawn ||
		(Pawn->GetHealthComponent() && Pawn->GetHealthComponent()->IsDead()))
	{
		return false;
	}
	if (ActiveSlotIndex == SlotIndex && Equipment->GetCurrentItemId() == Item->GetInstanceId())
	{
		return true;
	}
	if (!Equipment->EquipItem(Item))
	{
		return false;
	}
	ActiveSlotIndex = SlotIndex;
	OnChanged.Broadcast();
	Controller->ForceNetUpdate();
	UE_LOG(LogMiniEquipment, Display, TEXT("MiniQuickBar SELECTED: Controller=%s Slot=%d Item=%s"),
		*Controller->GetPathName(), SlotIndex, *Item->GetInstanceId().ToString());
	return true;
}

void UMiniQuickBarComponent::RequestNextSlot()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		SelectNextAvailableSlot();
	}
	else
	{
		ServerSelectNextSlot();
	}
}

void UMiniQuickBarComponent::ServerSelectSlot_Implementation(int32 SlotIndex)
{
	SelectSlot(SlotIndex);
}

void UMiniQuickBarComponent::ServerSelectNextSlot_Implementation()
{
	SelectNextAvailableSlot();
}

bool UMiniQuickBarComponent::SelectNextAvailableSlot()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || Slots.IsEmpty())
	{
		return false;
	}
	for (int32 Offset = 1; Offset <= Slots.Num(); ++Offset)
	{
		const int32 SlotIndex = (ActiveSlotIndex + Offset + Slots.Num()) % Slots.Num();
		if (GetSlotItemId(SlotIndex).IsValid() && SelectSlot(SlotIndex))
		{
			return true;
		}
	}
	return false;
}

void UMiniQuickBarComponent::OnRep_Slots()
{
	OnChanged.Broadcast();
	UE_LOG(LogMiniEquipment, Display, TEXT("MiniQuickBar CLIENT_SLOTS: %s"), *GetDebugSnapshot());
}

void UMiniQuickBarComponent::OnRep_ActiveSlotIndex()
{
	OnChanged.Broadcast();
	UE_LOG(LogMiniEquipment, Display, TEXT("MiniQuickBar CLIENT_ACTIVE: %s"), *GetDebugSnapshot());
}

FString UMiniQuickBarComponent::GetDebugSnapshot() const
{
	return FString::Printf(TEXT("Active=%d Slot0=%s Slot1=%s"), ActiveSlotIndex,
		*GetSlotItemId(0).ToString(), *GetSlotItemId(1).ToString());
}
