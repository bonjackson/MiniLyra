#include "MiniInventoryItemDefinition.h"

#include "Inventory/MiniInventoryItemInstance.h"
namespace MiniInventoryTags
{
	UE_DEFINE_GAMEPLAY_TAG(AmmoInMagazine, "Inventory.Stat.AmmoInMagazine");
	UE_DEFINE_GAMEPLAY_TAG(ReserveAmmo, "Inventory.Stat.ReserveAmmo");
}

const UMiniInventoryItemFragment* UMiniInventoryItemDefinition::FindFragmentByClass(
	TSubclassOf<UMiniInventoryItemFragment> FragmentClass) const
{
	if (FragmentClass)
	{
		for (const UMiniInventoryItemFragment* Fragment : Fragments)
		{
			if (Fragment && Fragment->IsA(FragmentClass))
			{
				return Fragment;
			}
		}
	}
	return nullptr;
}

void UMiniInventoryFragment_InitialStats::OnInstanceCreated(UMiniInventoryItemInstance* Instance) const
{
	if (!Instance)
	{
		return;
	}
	for (const FMiniInventoryStat& Stat : InitialStats)
	{
		Instance->SetStat(Stat.Tag, Stat.Count);
	}
}

UMiniRifleItemDefinition::UMiniRifleItemDefinition()
{
	DisplayName = NSLOCTEXT("MiniInventory", "Rifle", "Rifle");
	UMiniInventoryFragment_InitialStats* Stats = CreateDefaultSubobject<UMiniInventoryFragment_InitialStats>(TEXT("InitialStats"));
	Stats->InitialStats.Add(FMiniInventoryStat(MiniInventoryTags::AmmoInMagazine, 30));
	Stats->InitialStats.Add(FMiniInventoryStat(MiniInventoryTags::ReserveAmmo, 90));
	Fragments.Add(Stats);
	Fragments.Add(CreateDefaultSubobject<UMiniInventoryFragment_Equippable>(TEXT("Equippable")));
	Fragments.Add(CreateDefaultSubobject<UMiniInventoryFragment_Icon>(TEXT("Icon")));
}

UMiniPistolItemDefinition::UMiniPistolItemDefinition()
{
	DisplayName = NSLOCTEXT("MiniInventory", "Pistol", "Pistol");
	UMiniInventoryFragment_InitialStats* Stats = CreateDefaultSubobject<UMiniInventoryFragment_InitialStats>(TEXT("InitialStats"));
	Stats->InitialStats.Add(FMiniInventoryStat(MiniInventoryTags::AmmoInMagazine, 12));
	Stats->InitialStats.Add(FMiniInventoryStat(MiniInventoryTags::ReserveAmmo, 36));
	Fragments.Add(Stats);
	Fragments.Add(CreateDefaultSubobject<UMiniInventoryFragment_Equippable>(TEXT("Equippable")));
	Fragments.Add(CreateDefaultSubobject<UMiniInventoryFragment_Icon>(TEXT("Icon")));
}
