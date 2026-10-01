#include "MiniTask20LoadoutAssetLibrary.h"

#if WITH_EDITOR
#include "Character/MiniPawnData.h"
#include "Equipment/MiniLoadoutDefinition.h"
#include "Inventory/MiniInventoryItemDefinition.h"
#include "System/MiniLogChannels.h"

namespace MiniTask20LoadoutAssets
{
enum class EPreset : uint8 { Shared, RifleOnly, Unarmed };

const TCHAR* GetPresetPath(EPreset Preset)
{
	switch (Preset)
	{
	case EPreset::Shared:
		return TEXT("/Game/Mini/System/Loadouts/DA_MiniSharedCombatLoadout.DA_MiniSharedCombatLoadout");
	case EPreset::RifleOnly:
		return TEXT("/Game/Mini/System/Loadouts/DA_MiniRifleOnlyLoadout.DA_MiniRifleOnlyLoadout");
	case EPreset::Unarmed:
	default:
		return TEXT("/Game/Mini/System/Loadouts/DA_MiniUnarmedLoadout.DA_MiniUnarmedLoadout");
	}
}

FMiniLoadoutEntry MakeEntry(int32 SlotIndex, TSubclassOf<UMiniInventoryItemDefinition> ItemClass)
{
	FMiniLoadoutEntry Entry;
	Entry.SlotIndex = SlotIndex;
	Entry.ItemDefinition = ItemClass;
	return Entry;
}

void SetPresetValues(UMiniLoadoutDefinition* Loadout, EPreset Preset)
{
	Loadout->Items.Reset();
	Loadout->InitiallySelectedSlot = INDEX_NONE;
	if (Preset != EPreset::Unarmed)
	{
		Loadout->Items.Add(MakeEntry(0, UMiniRifleItemDefinition::StaticClass()));
		Loadout->InitiallySelectedSlot = 0;
	}
	if (Preset == EPreset::Shared)
	{
		Loadout->Items.Add(MakeEntry(1, UMiniPistolItemDefinition::StaticClass()));
	}
}

bool VerifyPreset(const UMiniLoadoutDefinition* Loadout, EPreset Preset)
{
	FString Error;
	if (!Loadout || Loadout->GetPathName() != GetPresetPath(Preset) || !Loadout->ValidateLoadout(Error))
	{
		return false;
	}
	const int32 ExpectedCount = Preset == EPreset::Shared ? 2 : (Preset == EPreset::RifleOnly ? 1 : 0);
	if (Loadout->Items.Num() != ExpectedCount ||
		Loadout->InitiallySelectedSlot != (ExpectedCount > 0 ? 0 : INDEX_NONE))
	{
		return false;
	}
	if (ExpectedCount > 0 && (Loadout->Items[0].SlotIndex != 0 ||
		Loadout->Items[0].ItemDefinition != UMiniRifleItemDefinition::StaticClass()))
	{
		return false;
	}
	return ExpectedCount < 2 || (Loadout->Items[1].SlotIndex == 1 &&
		Loadout->Items[1].ItemDefinition == UMiniPistolItemDefinition::StaticClass());
}

bool ConfigurePreset(UMiniLoadoutDefinition* Loadout, EPreset Preset)
{
	if (!Loadout || Loadout->GetPathName() != GetPresetPath(Preset))
	{
		return false;
	}
	if (!VerifyPreset(Loadout, Preset))
	{
		Loadout->Modify();
		SetPresetValues(Loadout, Preset);
		Loadout->MarkPackageDirty();
	}
	return VerifyPreset(Loadout, Preset);
}

bool IsSupportedPawnData(const UMiniPawnData* PawnData)
{
	return PawnData && (PawnData->GetPathName() ==
		TEXT("/Game/Mini/System/PawnData/DA_MiniPracticePawnData.DA_MiniPracticePawnData") ||
		PawnData->GetPathName() ==
		TEXT("/Game/Mini/Diagnostics/PawnData/DA_MiniDiagnosticsPawnData.DA_MiniDiagnosticsPawnData"));
}

bool IsVerifiedOwnedLoadout(const UMiniLoadoutDefinition* Loadout)
{
	return VerifyPreset(Loadout, EPreset::Shared) || VerifyPreset(Loadout, EPreset::RifleOnly) ||
		VerifyPreset(Loadout, EPreset::Unarmed);
}

bool ExpectValidation(UMiniLoadoutDefinition* Loadout, const TCHAR* CaseName, bool bExpectedValid)
{
	FString Error;
	const bool bValid = Loadout->ValidateLoadout(Error);
	if (bValid != bExpectedValid || (!bExpectedValid && Error.IsEmpty()))
	{
		UE_LOG(LogMiniEquipment, Error, TEXT("MiniTask20Loadout CASE_FAIL: Name=%s ExpectedValid=%d Valid=%d Reason=%s"),
			CaseName, bExpectedValid ? 1 : 0, bValid ? 1 : 0, *Error);
		return false;
	}
	UE_LOG(LogMiniEquipment, Display, TEXT("MiniTask20Loadout CASE_PASS: Name=%s ExpectedValid=%d"),
		CaseName, bExpectedValid ? 1 : 0);
	return true;
}
}
#endif

bool UMiniTask20LoadoutAssetLibrary::ConfigureSharedLoadout(UMiniLoadoutDefinition* Loadout)
{
#if WITH_EDITOR
	return MiniTask20LoadoutAssets::ConfigurePreset(Loadout, MiniTask20LoadoutAssets::EPreset::Shared);
#else
	return false;
#endif
}

bool UMiniTask20LoadoutAssetLibrary::ConfigureRifleOnlyLoadout(UMiniLoadoutDefinition* Loadout)
{
#if WITH_EDITOR
	return MiniTask20LoadoutAssets::ConfigurePreset(Loadout, MiniTask20LoadoutAssets::EPreset::RifleOnly);
#else
	return false;
#endif
}

bool UMiniTask20LoadoutAssetLibrary::ConfigureUnarmedLoadout(UMiniLoadoutDefinition* Loadout)
{
#if WITH_EDITOR
	return MiniTask20LoadoutAssets::ConfigurePreset(Loadout, MiniTask20LoadoutAssets::EPreset::Unarmed);
#else
	return false;
#endif
}

bool UMiniTask20LoadoutAssetLibrary::VerifySharedLoadout(const UMiniLoadoutDefinition* Loadout)
{
#if WITH_EDITOR
	return MiniTask20LoadoutAssets::VerifyPreset(Loadout, MiniTask20LoadoutAssets::EPreset::Shared);
#else
	return false;
#endif
}

bool UMiniTask20LoadoutAssetLibrary::VerifyRifleOnlyLoadout(const UMiniLoadoutDefinition* Loadout)
{
#if WITH_EDITOR
	return MiniTask20LoadoutAssets::VerifyPreset(Loadout, MiniTask20LoadoutAssets::EPreset::RifleOnly);
#else
	return false;
#endif
}

bool UMiniTask20LoadoutAssetLibrary::VerifyUnarmedLoadout(const UMiniLoadoutDefinition* Loadout)
{
#if WITH_EDITOR
	return MiniTask20LoadoutAssets::VerifyPreset(Loadout, MiniTask20LoadoutAssets::EPreset::Unarmed);
#else
	return false;
#endif
}

bool UMiniTask20LoadoutAssetLibrary::ConfigurePawnDataLoadout(UMiniPawnData* PawnData,
	UMiniLoadoutDefinition* Loadout)
{
#if WITH_EDITOR
	if (!MiniTask20LoadoutAssets::IsSupportedPawnData(PawnData) ||
		!MiniTask20LoadoutAssets::IsVerifiedOwnedLoadout(Loadout))
	{
		return false;
	}
	if (PawnData->DefaultLoadout != Loadout)
	{
		PawnData->Modify();
		PawnData->DefaultLoadout = Loadout;
		PawnData->MarkPackageDirty();
	}
	return VerifyPawnDataLoadout(PawnData, Loadout);
#else
	return false;
#endif
}

bool UMiniTask20LoadoutAssetLibrary::VerifyPawnDataLoadout(const UMiniPawnData* PawnData,
	const UMiniLoadoutDefinition* Loadout)
{
#if WITH_EDITOR
	FString Error;
	return MiniTask20LoadoutAssets::IsSupportedPawnData(PawnData) &&
		MiniTask20LoadoutAssets::IsVerifiedOwnedLoadout(Loadout) &&
		PawnData->DefaultLoadout == Loadout && PawnData->ValidatePawnData(Error);
#else
	return false;
#endif
}

bool UMiniTask20LoadoutAssetLibrary::VerifyValidationCases()
{
#if WITH_EDITOR
	using namespace MiniTask20LoadoutAssets;
	UMiniLoadoutDefinition* Fixture = NewObject<UMiniLoadoutDefinition>(GetTransientPackage());
	SetPresetValues(Fixture, EPreset::Shared);
	if (!ExpectValidation(Fixture, TEXT("TwoItems"), true)) { return false; }
	SetPresetValues(Fixture, EPreset::RifleOnly);
	if (!ExpectValidation(Fixture, TEXT("OneItem"), true)) { return false; }
	SetPresetValues(Fixture, EPreset::Unarmed);
	if (!ExpectValidation(Fixture, TEXT("ZeroItems"), true)) { return false; }
	Fixture->Items.Add(MakeEntry(1, UMiniPistolItemDefinition::StaticClass()));
	Fixture->InitiallySelectedSlot = 1;
	if (!ExpectValidation(Fixture, TEXT("OnlySlotOne"), true)) { return false; }
	SetPresetValues(Fixture, EPreset::Shared);
	Fixture->Items[1].SlotIndex = 0;
	if (!ExpectValidation(Fixture, TEXT("DuplicateSlot"), false)) { return false; }
	SetPresetValues(Fixture, EPreset::RifleOnly);
	Fixture->Items[0].SlotIndex = UMiniLoadoutDefinition::NumQuickBarSlots;
	if (!ExpectValidation(Fixture, TEXT("OutOfRangeSlot"), false)) { return false; }
	SetPresetValues(Fixture, EPreset::RifleOnly);
	Fixture->Items[0].ItemDefinition = nullptr;
	if (!ExpectValidation(Fixture, TEXT("MissingItemClass"), false)) { return false; }
	SetPresetValues(Fixture, EPreset::RifleOnly);
	Fixture->Items[0].ItemDefinition = UMiniInventoryItemDefinition::StaticClass();
	if (!ExpectValidation(Fixture, TEXT("AbstractItemClass"), false)) { return false; }
	SetPresetValues(Fixture, EPreset::RifleOnly);
	Fixture->InitiallySelectedSlot = 1;
	if (!ExpectValidation(Fixture, TEXT("UnoccupiedInitialSlot"), false)) { return false; }
	SetPresetValues(Fixture, EPreset::Unarmed);
	Fixture->InitiallySelectedSlot = 0;
	if (!ExpectValidation(Fixture, TEXT("EmptyWithSelectedSlot"), false)) { return false; }
	UE_LOG(LogMiniEquipment, Display, TEXT("MINI_TASK20_LOADOUT_VALIDATION_CASES_PASSED Valid=4 Rejected=6"));
	return true;
#else
	return false;
#endif
}
