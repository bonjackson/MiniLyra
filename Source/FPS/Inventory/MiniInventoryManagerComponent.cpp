#include "MiniInventoryManagerComponent.h"

#include "Engine/ActorChannel.h"
#include "Equipment/MiniQuickBarComponent.h"
#include "Equipment/MiniEquipmentManagerComponent.h"
#include "Character/MiniCharacter.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "Inventory/MiniInventoryItemInstance.h"
#include "Net/UnrealNetwork.h"
#include "System/MiniLogChannels.h"
#include "Player/MiniPlayerController.h"

void FMiniInventoryList::PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize)
{
	for (const int32 Index : RemovedIndices)
	{
		const UMiniInventoryItemInstance* Instance = Entries.IsValidIndex(Index) ? Entries[Index].Instance : nullptr;
		UE_LOG(LogMiniInit, Display, TEXT("MiniInventory CLIENT_REMOVE: InstanceId=%s"),
			IsValid(Instance) ? *Instance->GetInstanceId().ToString() : TEXT("Unresolved"));
	}
}

void FMiniInventoryList::PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize)
{
	UE_LOG(LogMiniInit, Display, TEXT("MiniInventory CLIENT_ADD: Count=%d Snapshot=%s"),
		Entries.Num(), OwnerComponent ? *OwnerComponent->GetDebugSnapshot() : TEXT("NoOwner"));
}

void FMiniInventoryList::PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize)
{
	UE_LOG(LogMiniInit, Display, TEXT("MiniInventory CLIENT_CHANGE: Count=%d Snapshot=%s"),
		Entries.Num(), OwnerComponent ? *OwnerComponent->GetDebugSnapshot() : TEXT("NoOwner"));
}

UMiniInventoryManagerComponent::UMiniInventoryManagerComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
	InventoryList.OwnerComponent = this;
}

void FMiniInventoryList::PostReplicatedReceive(const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters)
{
	// Runs after add/change/remove and again when delayed UObject references map.
	if (OwnerComponent)
	{
		OwnerComponent->NotifyDataChanged();
	}
}

void UMiniInventoryManagerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMiniInventoryManagerComponent, InventoryList);
}

bool UMiniInventoryManagerComponent::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch,
	FReplicationFlags* RepFlags)
{
	bool bWrote = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);
	for (const FMiniInventoryEntry& Entry : InventoryList.Entries)
	{
		if (IsValid(Entry.Instance))
		{
			bWrote |= Channel->ReplicateSubobject(Entry.Instance, *Bunch, *RepFlags);
		}
	}
	return bWrote;
}

UMiniInventoryItemInstance* UMiniInventoryManagerComponent::AddItem(
	TSubclassOf<UMiniInventoryItemDefinition> Definition, int32 StackCount)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !Definition ||
		Definition->HasAnyClassFlags(CLASS_Abstract) || StackCount <= 0)
	{
		return nullptr;
	}
	UMiniInventoryItemInstance* Instance = NewObject<UMiniInventoryItemInstance>(Owner);
	if (!Instance || !Instance->InitializeItem(Definition))
	{
		return nullptr;
	}
	FMiniInventoryEntry& Entry = InventoryList.Entries.AddDefaulted_GetRef();
	Entry.Instance = Instance;
	Entry.StackCount = StackCount;
	InventoryList.MarkItemDirty(Entry);
	NotifyDataChanged();
	Owner->ForceNetUpdate();
	UE_LOG(LogMiniInit, Display, TEXT("MiniInventory SERVER_ADD: Owner=%s Definition=%s InstanceId=%s Count=%d"),
		*Owner->GetPathName(), *GetNameSafe(Definition.Get()), *Instance->GetInstanceId().ToString(), StackCount);
	return Instance;
}

bool UMiniInventoryManagerComponent::RemoveItem(UMiniInventoryItemInstance* Instance)
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || !IsValid(Instance))
	{
		return false;
	}
	const int32 Index = InventoryList.Entries.IndexOfByPredicate(
		[Instance](const FMiniInventoryEntry& Entry) { return Entry.Instance == Instance; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	if (AMiniPlayerController* Controller = Cast<AMiniPlayerController>(Owner))
	{
		if (UMiniQuickBarComponent* QuickBar = Controller->GetQuickBar())
		{
			QuickBar->HandleItemRemoved(Instance);
		}
		if (AMiniCharacter* Pawn = Cast<AMiniCharacter>(Controller->GetPawn()))
		{
			if (UMiniEquipmentManagerComponent* Equipment = Pawn->GetEquipmentManager())
			{
				if (Equipment->GetCurrentItemId() == Instance->GetInstanceId())
				{
					Equipment->UnequipItem();
				}
			}
		}
	}
	const FGuid RemovedId = Instance->GetInstanceId();
	InventoryList.Entries.RemoveAt(Index);
	InventoryList.MarkArrayDirty();
	NotifyDataChanged();
	// The legacy ReplicateSubobjects path no longer sends this item. Explicitly
	// close its client replica rather than waiting for the actor channel to end.
	DestroyReplicatedSubObjectOnRemotePeers(Instance);
	Owner->ForceNetUpdate();
	UE_LOG(LogMiniInit, Display, TEXT("MiniInventory SERVER_REMOVE: Owner=%s InstanceId=%s Remaining=%d"),
		*Owner->GetPathName(), *RemovedId.ToString(), InventoryList.Entries.Num());
	return true;
}

FString UMiniInventoryManagerComponent::GetDebugSnapshot() const
{
	FString Result = FString::Printf(TEXT("Items=%d"), InventoryList.Entries.Num());
	for (const FMiniInventoryEntry& Entry : InventoryList.Entries)
	{
		const UMiniInventoryItemInstance* Instance = Entry.Instance;
		Result += FString::Printf(TEXT(" [%s x%d Id=%s]"),
			IsValid(Instance) ? *GetNameSafe(Instance->GetItemDefinition().Get()) : TEXT("Unresolved"),
			Entry.StackCount,
			IsValid(Instance) ? *Instance->GetInstanceId().ToString() : TEXT("Unresolved"));
		if (IsValid(Instance))
		{
			for (const FMiniInventoryStat& Stat : Instance->GetStats())
			{
				Result += FString::Printf(TEXT(" {%s=%d}"), *Stat.Tag.ToString(), Stat.Count);
			}
		}
	}
	return Result;
}
