#include "MiniTask09AssetSetupLibrary.h"

#if WITH_EDITOR
#include "AbilitySystem/MiniAbilitySet.h"
#include "AbilitySystem/MiniProbeAbility.h"
#include "AbilitySystem/MiniProbeAttributeSet.h"
#include "AbilitySystem/MiniProbeEffect.h"
#include "Character/MiniPawnData.h"
#include "GameFeatures/MiniGameFeatureAction_AddAbilities.h"
#include "GameModes/MiniExperienceDefinition.h"
#endif

bool UMiniTask09AssetSetupLibrary::ConfigurePracticeAbilities(
	UMiniAbilitySet* PawnSet, UMiniAbilitySet* FeatureSet,
	UMiniPawnData* PawnData, UMiniExperienceDefinition* Experience)
{
#if WITH_EDITOR
	if (!PawnSet || !FeatureSet || PawnSet == FeatureSet || !PawnData || !Experience ||
		Experience->DefaultPawnData.Get() != PawnData ||
		(!PawnSet->Abilities.IsEmpty() &&
			PawnSet->Abilities[0].Ability != UMiniPawnProbeAbility::StaticClass()))
	{
		return false;
	}
	static const FName ActionName(TEXT("MiniTask09_AddAbilities"));
	UMiniGameFeatureAction_AddAbilities* Action = nullptr;
	for (UGameFeatureAction* Candidate : Experience->Actions)
	{
		if (Candidate && Candidate->GetFName() == ActionName)
		{
			if (Action || !Candidate->IsA<UMiniGameFeatureAction_AddAbilities>())
			{
				return false;
			}
			Action = CastChecked<UMiniGameFeatureAction_AddAbilities>(Candidate);
		}
	}
	PawnSet->Modify();
	FeatureSet->Modify();
	PawnData->Modify();
	Experience->Modify();
	if (!Action)
	{
		Action = NewObject<UMiniGameFeatureAction_AddAbilities>(Experience, ActionName, RF_Transactional);
		Experience->Actions.Add(Action);
	}
	Action->Modify();

	// Later tasks add input and more pawn abilities; keep those entries on re-runs.
	if (PawnSet->Abilities.IsEmpty())
	{
		PawnSet->Abilities.AddDefaulted();
	}
	PawnSet->Effects.Reset();
	PawnSet->Attributes.Reset();
	FMiniAbilitySetAbility& PawnAbility = PawnSet->Abilities[0];
	PawnAbility.Ability = UMiniPawnProbeAbility::StaticClass();
	PawnAbility.Level = 1;

	FeatureSet->Abilities.Reset();
	FeatureSet->Effects.Reset();
	FeatureSet->Attributes.Reset();
	FMiniAbilitySetAbility& FeatureAbility = FeatureSet->Abilities.AddDefaulted_GetRef();
	FeatureAbility.Ability = UMiniFeatureProbeAbility::StaticClass();
	FeatureAbility.Level = 1;
	FMiniAbilitySetEffect& FeatureEffect = FeatureSet->Effects.AddDefaulted_GetRef();
	FeatureEffect.Effect = UMiniProbeEffect::StaticClass();
	FeatureEffect.Level = 1.0f;
	FMiniAbilitySetAttribute& FeatureAttribute = FeatureSet->Attributes.AddDefaulted_GetRef();
	FeatureAttribute.AttributeSet = UMiniProbeAttributeSet::StaticClass();

	PawnData->AbilitySets.Reset();
	PawnData->AbilitySets.Add(PawnSet);
	Action->AbilitySet = FeatureSet;
	PawnSet->MarkPackageDirty();
	FeatureSet->MarkPackageDirty();
	PawnData->MarkPackageDirty();
	Experience->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}

bool UMiniTask09AssetSetupLibrary::VerifyPracticeAbilities(
	const UMiniAbilitySet* PawnSet, const UMiniAbilitySet* FeatureSet,
	const UMiniPawnData* PawnData, const UMiniExperienceDefinition* Experience)
{
#if WITH_EDITOR
	if (!PawnSet || !FeatureSet || !PawnData || !Experience ||
		PawnData->AbilitySets.Num() != 1 || PawnData->AbilitySets[0] != PawnSet ||
		PawnSet->Abilities.IsEmpty() || PawnSet->Abilities[0].Ability != UMiniPawnProbeAbility::StaticClass() ||
		!PawnSet->Effects.IsEmpty() || !PawnSet->Attributes.IsEmpty() ||
		FeatureSet->Abilities.Num() != 1 || FeatureSet->Abilities[0].Ability != UMiniFeatureProbeAbility::StaticClass() ||
		FeatureSet->Effects.Num() != 1 || FeatureSet->Effects[0].Effect != UMiniProbeEffect::StaticClass() ||
		FeatureSet->Attributes.Num() != 1 || FeatureSet->Attributes[0].AttributeSet != UMiniProbeAttributeSet::StaticClass())
	{
		return false;
	}
	int32 Matches = 0;
	for (const UGameFeatureAction* Candidate : Experience->Actions)
	{
		if (Candidate && Candidate->GetFName() == TEXT("MiniTask09_AddAbilities"))
		{
			const UMiniGameFeatureAction_AddAbilities* Action = Cast<UMiniGameFeatureAction_AddAbilities>(Candidate);
			if (!Action || Action->AbilitySet != FeatureSet)
			{
				return false;
			}
			++Matches;
		}
	}
	return Matches == 1;
#else
	return false;
#endif
}
