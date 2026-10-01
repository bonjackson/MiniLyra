#include "MiniTask12AssetSetupLibrary.h"

#if WITH_EDITOR
#include <initializer_list>

#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/MiniAbilitySet.h"
#include "AbilitySystem/MiniAbilityTagRelationshipMapping.h"
#include "AbilitySystem/MiniProbeAbility.h"
#include "Character/MiniPawnData.h"
#include "MiniLegacyAssetAuthoringGuard.h"
#include "System/MiniGameplayTags.h"
#include "UObject/UObjectGlobals.h"

namespace
{
FGameplayTag FindTag(const TCHAR* Name)
{
	return FGameplayTag::RequestGameplayTag(FName(Name), false);
}

struct FPracticeTags
{
	FGameplayTag Fire = FindTag(TEXT("Ability.Fire"));
	FGameplayTag Jump = FindTag(TEXT("Ability.Jump"));
	FGameplayTag Aim = FindTag(TEXT("Ability.Aim"));
	FGameplayTag Dead = FindTag(TEXT("State.Dead"));
	FGameplayTag Reloading = FindTag(TEXT("State.Reloading"));
	FGameplayTag InputBlocked = FindTag(TEXT("Gameplay.AbilityInputBlocked"));

	bool IsValid() const
	{
		return Fire.IsValid() && Jump.IsValid() && Aim.IsValid() && Dead.IsValid() &&
			Reloading.IsValid() && InputBlocked.IsValid();
	}
};

FGameplayTagContainer MakeTags(std::initializer_list<FGameplayTag> Tags)
{
	FGameplayTagContainer Result;
	for (const FGameplayTag Tag : Tags)
	{
		Result.AddTag(Tag);
	}
	return Result;
}

void AddRelationship(UMiniAbilityTagRelationshipMapping* Mapping, FGameplayTag Tag,
	const FGameplayTagContainer& Blocked, const FGameplayTagContainer& Cancel)
{
	FMiniAbilityTagRelationship& Row = Mapping->Relationships.AddDefaulted_GetRef();
	Row.Tag = Tag;
	Row.ActivationBlockedTags = Blocked;
	Row.CancelAbilitiesWithTags = Cancel;
}

bool HasExactTags(const FGameplayTagContainer& Actual, const FGameplayTagContainer& Expected)
{
	return Actual.Num() == Expected.Num() && Actual.HasAllExact(Expected);
}

bool IsRelationship(const FMiniAbilityTagRelationship& Row, FGameplayTag Tag,
	const FGameplayTagContainer& Blocked, const FGameplayTagContainer& Cancel)
{
	return Row.Tag == Tag && Row.ActivationRequiredTags.IsEmpty() &&
		HasExactTags(Row.ActivationBlockedTags, Blocked) &&
		HasExactTags(Row.CancelAbilitiesWithTags, Cancel);
}

UClass* LoadAbilityClass(const TCHAR* Path)
{
	return LoadClass<UGameplayAbility>(nullptr, Path);
}
}
#endif

bool UMiniTask12AssetSetupLibrary::ConfigurePracticeAbilities(UMiniAbilitySet* PawnSet,
	UMiniAbilityTagRelationshipMapping* Relationships, UMiniPawnData* PawnData)
{
#if WITH_EDITOR
	const FPracticeTags Tags;
	UClass* JumpClass = LoadAbilityClass(TEXT("/Script/FPS.MiniGameplayAbility_Jump"));
	UClass* AimClass = LoadAbilityClass(TEXT("/Script/FPS.MiniGameplayAbility_Aim"));
	if (!MiniIsDiagnosticsAuthoringAsset(PawnSet) || !MiniIsDiagnosticsAuthoringAsset(Relationships) ||
		!MiniIsDiagnosticsAuthoringAsset(PawnData) || !Tags.IsValid() || !JumpClass || !AimClass ||
		PawnData->AbilitySets.Num() != 1 || PawnData->AbilitySets[0] != PawnSet ||
		PawnSet->Abilities.IsEmpty() ||
		PawnSet->Abilities[0].Ability != UMiniPawnProbeAbility::StaticClass() ||
		PawnSet->Abilities[0].InputTag != MiniGameplayTags::InputTag_Fire)
	{
		return false;
	}

	PawnSet->Modify();
	Relationships->Modify();
	PawnData->Modify();

	PawnSet->Abilities.SetNum(3);
	PawnSet->Abilities[0].Ability = UMiniPawnProbeAbility::StaticClass();
	PawnSet->Abilities[0].Level = 1;
	PawnSet->Abilities[0].InputTag = MiniGameplayTags::InputTag_Fire;
	PawnSet->Abilities[1].Ability = JumpClass;
	PawnSet->Abilities[1].Level = 1;
	PawnSet->Abilities[1].InputTag = MiniGameplayTags::InputTag_Jump;
	PawnSet->Abilities[2].Ability = AimClass;
	PawnSet->Abilities[2].Level = 1;
	PawnSet->Abilities[2].InputTag = MiniGameplayTags::InputTag_Aim;

	Relationships->Relationships.Reset();
	AddRelationship(Relationships, Tags.Fire,
		MakeTags({Tags.Dead, Tags.Reloading, Tags.InputBlocked}), {});
	AddRelationship(Relationships, Tags.Jump,
		MakeTags({Tags.Dead, Tags.InputBlocked}), {});
	AddRelationship(Relationships, Tags.Aim,
		MakeTags({Tags.Dead, Tags.Reloading, Tags.InputBlocked}), {});
	AddRelationship(Relationships, Tags.Dead, {},
		MakeTags({Tags.Fire, Tags.Jump, Tags.Aim}));
	AddRelationship(Relationships, Tags.Reloading, {},
		MakeTags({Tags.Fire, Tags.Aim}));
	AddRelationship(Relationships, Tags.InputBlocked, {},
		MakeTags({Tags.Fire, Tags.Jump, Tags.Aim}));

	PawnData->TagRelationshipMapping = Relationships;
	PawnSet->MarkPackageDirty();
	Relationships->MarkPackageDirty();
	PawnData->MarkPackageDirty();
	return VerifyPracticeAbilities(PawnSet, Relationships, PawnData);
#else
	return false;
#endif
}

bool UMiniTask12AssetSetupLibrary::VerifyPracticeAbilities(const UMiniAbilitySet* PawnSet,
	const UMiniAbilityTagRelationshipMapping* Relationships, const UMiniPawnData* PawnData)
{
#if WITH_EDITOR
	const FPracticeTags Tags;
	UClass* JumpClass = LoadAbilityClass(TEXT("/Script/FPS.MiniGameplayAbility_Jump"));
	UClass* AimClass = LoadAbilityClass(TEXT("/Script/FPS.MiniGameplayAbility_Aim"));
	if (!PawnSet || !Relationships || !PawnData || !Tags.IsValid() || !JumpClass || !AimClass ||
		PawnData->AbilitySets.Num() != 1 || PawnData->AbilitySets[0] != PawnSet ||
		PawnData->TagRelationshipMapping != Relationships ||
		PawnSet->Abilities.Num() != 3 || !PawnSet->Effects.IsEmpty() || !PawnSet->Attributes.IsEmpty() ||
		PawnSet->Abilities[0].Ability != UMiniPawnProbeAbility::StaticClass() ||
		PawnSet->Abilities[0].Level != 1 || PawnSet->Abilities[0].InputTag != MiniGameplayTags::InputTag_Fire ||
		PawnSet->Abilities[1].Ability != JumpClass || PawnSet->Abilities[1].Level != 1 ||
		PawnSet->Abilities[1].InputTag != MiniGameplayTags::InputTag_Jump ||
		PawnSet->Abilities[2].Ability != AimClass || PawnSet->Abilities[2].Level != 1 ||
		PawnSet->Abilities[2].InputTag != MiniGameplayTags::InputTag_Aim ||
		Relationships->Relationships.Num() != 6)
	{
		return false;
	}

	const auto& Rows = Relationships->Relationships;
	return IsRelationship(Rows[0], Tags.Fire,
		MakeTags({Tags.Dead, Tags.Reloading, Tags.InputBlocked}), {}) &&
		IsRelationship(Rows[1], Tags.Jump,
			MakeTags({Tags.Dead, Tags.InputBlocked}), {}) &&
		IsRelationship(Rows[2], Tags.Aim,
			MakeTags({Tags.Dead, Tags.Reloading, Tags.InputBlocked}), {}) &&
		IsRelationship(Rows[3], Tags.Dead, {},
			MakeTags({Tags.Fire, Tags.Jump, Tags.Aim})) &&
		IsRelationship(Rows[4], Tags.Reloading, {},
			MakeTags({Tags.Fire, Tags.Aim})) &&
		IsRelationship(Rows[5], Tags.InputBlocked, {},
			MakeTags({Tags.Fire, Tags.Jump, Tags.Aim}));
#else
	return false;
#endif
}
