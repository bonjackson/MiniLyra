#include "MiniQuickBarComponent.h"

#include "AbilitySystem/MiniAbilitySystemComponent.h"
#include "Character/MiniCharacter.h"
#include "Character/MiniHealthComponent.h"
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
	Slots.SetNum(2);
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
		(Pawn->GetHealthComponent() && Pawn->GetHealthComponent()->IsDead()))
	{
		return false;
	}
	// Older isolated probes own their inventory or expect the pre-equipment ASC count.
	if (FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask13")) ||
		FParse::Param(FCommandLine::Get(), TEXT("MiniProbeTask14")))
	{
		return false;
	}
	// Repeated init notifications during one life must not refill a deliberately
	// emptied slot or reset ammo. A failed first setup clears BoundPawn below.
	if (BoundPawn.Get() == Pawn)
	{
		return true;
	}
	if (AMiniCharacter* PreviousPawn = BoundPawn.Get())
	{
		if (PreviousPawn != Pawn && PreviousPawn->GetEquipmentManager())
		{
			PreviousPawn->GetEquipmentManager()->UnequipItem();
		}
	}
	BoundPawn = Pawn;
	ActiveSlotIndex = INDEX_NONE;
	Slots.SetNum(2);
	Slots[0].Invalidate();
	Slots[1].Invalidate();
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
	UMiniInventoryItemInstance* Rifle = Inventory->AddItem(UMiniRifleItemDefinition::StaticClass());
	UMiniInventoryItemInstance* Pistol = Inventory->AddItem(UMiniPistolItemDefinition::StaticClass());
	if (!Rifle || !Pistol)
	{
		if (Rifle) { Inventory->RemoveItem(Rifle); }
		if (Pistol) { Inventory->RemoveItem(Pistol); }
		BoundPawn.Reset();
		Controller->ForceNetUpdate();
		return false;
	}
	Slots[0] = Rifle->GetInstanceId();
	Slots[1] = Pistol->GetInstanceId();
	Controller->ForceNetUpdate();
	UE_LOG(LogMiniEquipment, Display,
		TEXT("MiniQuickBar DEFAULTS: Controller=%s Pawn=%s Rifle=%s Pistol=%s"),
		*Controller->GetPathName(), *Pawn->GetPathName(), *Slots[0].ToString(), *Slots[1].ToString());
	if (SelectSlot(0))
	{
		return true;
	}
	Inventory->RemoveItem(Rifle);
	Inventory->RemoveItem(Pistol);
	Slots[0].Invalidate();
	Slots[1].Invalidate();
	BoundPawn.Reset();
	Controller->ForceNetUpdate();
	return false;
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
	Controller->ForceNetUpdate();
	UE_LOG(LogMiniEquipment, Display, TEXT("MiniQuickBar SELECTED: Controller=%s Slot=%d Item=%s"),
		*Controller->GetPathName(), SlotIndex, *Item->GetInstanceId().ToString());
	return true;
}

void UMiniQuickBarComponent::RequestNextSlot()
{
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		SelectSlot(ActiveSlotIndex == 0 ? 1 : 0);
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
	SelectSlot(ActiveSlotIndex == 0 ? 1 : 0);
}

void UMiniQuickBarComponent::OnRep_Slots()
{
	UE_LOG(LogMiniEquipment, Display, TEXT("MiniQuickBar CLIENT_SLOTS: %s"), *GetDebugSnapshot());
}

void UMiniQuickBarComponent::OnRep_ActiveSlotIndex()
{
	UE_LOG(LogMiniEquipment, Display, TEXT("MiniQuickBar CLIENT_ACTIVE: %s"), *GetDebugSnapshot());
}

FString UMiniQuickBarComponent::GetDebugSnapshot() const
{
	return FString::Printf(TEXT("Active=%d Slot0=%s Slot1=%s"), ActiveSlotIndex,
		*GetSlotItemId(0).ToString(), *GetSlotItemId(1).ToString());
}
