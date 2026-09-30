#include "MiniInventoryItemInstance.h"

#include "GameFramework/Actor.h"
#include "Inventory/MiniInventoryManagerComponent.h"
#include "Net/UnrealNetwork.h"

void UMiniInventoryItemInstance::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ThisClass, ItemDefinition);
	DOREPLIFETIME(ThisClass, InstanceId);
	DOREPLIFETIME(ThisClass, Stats);
}

bool UMiniInventoryItemInstance::InitializeItem(TSubclassOf<UMiniInventoryItemDefinition> InDefinition)
{
	const AActor* OwnerActor = Cast<AActor>(GetOuter());
	if (!OwnerActor || !OwnerActor->HasAuthority() || !InDefinition || InDefinition->HasAnyClassFlags(CLASS_Abstract) ||
		ItemDefinition || InstanceId.IsValid())
	{
		return false;
	}

	ItemDefinition = InDefinition;
	InstanceId = FGuid::NewGuid();
	const UMiniInventoryItemDefinition* DefinitionCDO = GetDefault<UMiniInventoryItemDefinition>(InDefinition);
	for (const UMiniInventoryItemFragment* Fragment : DefinitionCDO->Fragments)
	{
		if (Fragment)
		{
			Fragment->OnInstanceCreated(this);
		}
	}
	NotifyChanged();
	return true;
}

int32 UMiniInventoryItemInstance::GetStat(FGameplayTag Tag) const
{
	for (const FMiniInventoryStat& Stat : Stats)
	{
		if (Stat.Tag == Tag)
		{
			return Stat.Count;
		}
	}
	return 0;
}

bool UMiniInventoryItemInstance::SetStat(FGameplayTag Tag, int32 Count)
{
	AActor* OwnerActor = Cast<AActor>(GetOuter());
	if (!OwnerActor || !OwnerActor->HasAuthority() || !InstanceId.IsValid() || !Tag.IsValid() || Count < 0)
	{
		return false;
	}
	for (FMiniInventoryStat& Stat : Stats)
	{
		if (Stat.Tag == Tag)
		{
			if (Stat.Count != Count)
			{
				Stat.Count = Count;
				OwnerActor->ForceNetUpdate();
				NotifyChanged();
			}
			return true;
		}
	}
	Stats.Add(FMiniInventoryStat(Tag, Count));
	OwnerActor->ForceNetUpdate();
	NotifyChanged();
	return true;
}

void UMiniInventoryItemInstance::OnRep_ItemData()
{
	NotifyChanged();
}

void UMiniInventoryItemInstance::NotifyChanged()
{
	OnChanged.Broadcast();
	if (const AActor* OwnerActor = Cast<AActor>(GetOuter()))
	{
		if (UMiniInventoryManagerComponent* Inventory = OwnerActor->FindComponentByClass<UMiniInventoryManagerComponent>())
		{
			// The item GUID/definition can map after Slots and the FastArray entry.
			Inventory->NotifyDataChanged();
		}
	}
}

const UMiniInventoryItemFragment* UMiniInventoryItemInstance::FindFragmentByClass(
	TSubclassOf<UMiniInventoryItemFragment> FragmentClass) const
{
	return ItemDefinition && FragmentClass
		? GetDefault<UMiniInventoryItemDefinition>(ItemDefinition)->FindFragmentByClass(FragmentClass)
		: nullptr;
}
