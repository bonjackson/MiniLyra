#include "MiniLoadoutDefinition.h"

#include "Equipment/MiniEquipmentDefinition.h"
#include "Inventory/MiniInventoryItemDefinition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool UMiniLoadoutDefinition::ValidateLoadout(FString& OutError) const
{
	OutError.Reset();
	if (Items.IsEmpty())
	{
		if (InitiallySelectedSlot != INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("Empty Loadout '%s' must use InitiallySelectedSlot=-1"), *GetPathName());
			return false;
		}
		return true;
	}

	TSet<int32> OccupiedSlots;
	for (const FMiniLoadoutEntry& Entry : Items)
	{
		if (Entry.SlotIndex < 0 || Entry.SlotIndex >= NumQuickBarSlots || OccupiedSlots.Contains(Entry.SlotIndex))
		{
			OutError = FString::Printf(TEXT("Loadout '%s' has an invalid or duplicate slot %d"),
				*GetPathName(), Entry.SlotIndex);
			return false;
		}
		OccupiedSlots.Add(Entry.SlotIndex);
		UClass* ItemClass = Entry.ItemDefinition.Get();
		if (!ItemClass || !ItemClass->IsChildOf(UMiniInventoryItemDefinition::StaticClass()) ||
			ItemClass->HasAnyClassFlags(CLASS_Abstract))
		{
			OutError = FString::Printf(TEXT("Loadout '%s' slot %d requires a concrete item definition"),
				*GetPathName(), Entry.SlotIndex);
			return false;
		}
		const UMiniInventoryItemDefinition* ItemCDO = GetDefault<UMiniInventoryItemDefinition>(ItemClass);
		const UMiniInventoryFragment_Equippable* Equippable = ItemCDO
			? ItemCDO->FindFragment<UMiniInventoryFragment_Equippable>() : nullptr;
		UClass* EquipmentClass = Equippable ? Equippable->EquipmentDefinition.LoadSynchronous() : nullptr;
		if (!EquipmentClass || !EquipmentClass->IsChildOf(UMiniEquipmentDefinition::StaticClass()) ||
			EquipmentClass->HasAnyClassFlags(CLASS_Abstract))
		{
			OutError = FString::Printf(TEXT("Loadout '%s' slot %d requires a concrete equipment definition"),
				*GetPathName(), Entry.SlotIndex);
			return false;
		}
		const UMiniEquipmentDefinition* EquipmentCDO = GetDefault<UMiniEquipmentDefinition>(EquipmentClass);
		if (!EquipmentCDO || !EquipmentCDO->GetAbilitySet())
		{
			OutError = FString::Printf(TEXT("Loadout '%s' slot %d equipment has no AbilitySet"),
				*GetPathName(), Entry.SlotIndex);
			return false;
		}

		// Reject malformed initial stats before they are applied to an item. The
		// normal inventory fragment remains the only author of initial ammo values.
		TSet<FGameplayTag> SeenTags;
		int32 MagazineAmmo = INDEX_NONE;
		int32 ReserveAmmo = INDEX_NONE;
		for (const UMiniInventoryItemFragment* Fragment : ItemCDO->Fragments)
		{
			const UMiniInventoryFragment_InitialStats* Stats = Cast<UMiniInventoryFragment_InitialStats>(Fragment);
			if (!Stats) { continue; }
			for (const FMiniInventoryStat& Stat : Stats->InitialStats)
			{
				if (!Stat.Tag.IsValid() || Stat.Count < 0 || SeenTags.Contains(Stat.Tag))
				{
					OutError = FString::Printf(TEXT("Loadout '%s' slot %d item has invalid or duplicate initial stats"),
						*GetPathName(), Entry.SlotIndex);
					return false;
				}
				SeenTags.Add(Stat.Tag);
				if (Stat.Tag == MiniInventoryTags::AmmoInMagazine) { MagazineAmmo = Stat.Count; }
				if (Stat.Tag == MiniInventoryTags::ReserveAmmo) { ReserveAmmo = Stat.Count; }
			}
		}
		if (const UMiniRangedWeaponEquipmentDefinition* Weapon = Cast<UMiniRangedWeaponEquipmentDefinition>(EquipmentCDO))
		{
			if (MagazineAmmo < 0 || ReserveAmmo < 0 || MagazineAmmo > Weapon->GetMagazineCapacity())
			{
				OutError = FString::Printf(TEXT("Loadout '%s' slot %d weapon requires valid initial magazine/reserve ammo"),
					*GetPathName(), Entry.SlotIndex);
				return false;
			}
		}
	}
	if (!OccupiedSlots.Contains(InitiallySelectedSlot))
	{
		OutError = FString::Printf(TEXT("Loadout '%s' initial slot %d is not occupied"),
			*GetPathName(), InitiallySelectedSlot);
		return false;
	}
	return true;
}

#if WITH_EDITOR
EDataValidationResult UMiniLoadoutDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		FString Error;
		if (!ValidateLoadout(Error))
		{
			Context.AddError(FText::FromString(Error));
			return EDataValidationResult::Invalid;
		}
	}
	return Result == EDataValidationResult::Invalid ? Result : EDataValidationResult::Valid;
}
#endif
