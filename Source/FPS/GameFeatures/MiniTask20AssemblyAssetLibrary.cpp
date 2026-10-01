#include "MiniTask20AssemblyAssetLibrary.h"

#if WITH_EDITOR
#include "AbilitySystem/MiniAbilitySet.h"
#include "AbilitySystem/MiniGameplayAbility_Aim.h"
#include "AbilitySystem/MiniGameplayAbility_Jump.h"
#include "Character/MiniPawnData.h"
#include "GameFeatureData.h"
#include "GameFeatures/MiniGameFeatureAction_AddActors.h"
#include "GameFeatures/MiniGameFeatureAction_AddInput.h"
#include "GameFeatures/MiniGameFeatureAction_AddGameplayCuePath.h"
#include "GameFeatures/MiniGameFeatureAction_AddWidgets.h"
#include "GameModes/MiniExperienceActionSet.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameFramework/Actor.h"
#include "Input/MiniInputConfig.h"
#include "InputMappingContext.h"
#include "System/MiniGameplayTags.h"

namespace
{
bool Task20IsProductionAsset(const UObject* Asset)
{
	return Asset && Asset->GetPathName().StartsWith(TEXT("/Game/Mini/System/"));
}

bool Task20FindCoreActions(const UGameFeatureData* Core, TArray<TObjectPtr<UGameFeatureAction>>& OutActions)
{
	UGameFeatureAction* Cue = nullptr;
	UGameFeatureAction* HUD = nullptr;
	for (UGameFeatureAction* Action : Core->GetActions())
	{
		if (Action && Action->GetFName() == TEXT("MiniTask18_AddGameplayCuePath"))
		{
			if (Cue || !Action->IsA<UMiniGameFeatureAction_AddGameplayCuePath>()) { return false; }
			Cue = Action;
		}
		if (Action && Action->GetFName() == TEXT("MiniTask19_AddWidgets"))
		{
			if (HUD || !Action->IsA<UMiniGameFeatureAction_AddWidgets>()) { return false; }
			HUD = Action;
		}
	}
	if (!Cue || !HUD) { return false; }
	OutActions = {Cue, HUD};
	return true;
}

bool Task20HasCoreFeature(const TArray<FString>& Names)
{
	return Names.Num() == 1 && Names[0] == TEXT("MiniShooterCore");
}

bool Task20ValidActorConfiguration(TSubclassOf<AActor> TargetClass, const TArray<FTransform>& Transforms,
	TSubclassOf<AActor> SupplyClass, const FTransform& SupplyTransform)
{
	if (!TargetClass || !SupplyClass || Transforms.Num() != 3 ||
		TargetClass->GetPathName() != TEXT("/Game/Mini/Targets/BP_MiniPracticeTarget.BP_MiniPracticeTarget_C") ||
		SupplyClass->GetPathName() != TEXT("/Script/FPS.MiniPracticeSupply")) { return false; }
	for (UClass* Class : {TargetClass.Get(), SupplyClass.Get()})
	{
		const AActor* Default = Cast<AActor>(Class->GetDefaultObject());
		if (!Default || !Default->GetIsReplicated() || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			return false;
		}
	}
	const FVector Positions[] = {FVector(-700.0, 500.0, 120.0), FVector(0.0, 650.0, 120.0), FVector(700.0, 500.0, 120.0)};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (!Transforms[Index].Equals(FTransform(FRotator(0.0, 90.0, 0.0), Positions[Index]), 0.01)) { return false; }
	}
	return SupplyTransform.Equals(FTransform(FVector(0.0, -1150.0, 10.0)), 0.01);
}

bool Task20HasProductionAbilities(const UMiniAbilitySet* PawnSet)
{
	return PawnSet && PawnSet->Abilities.Num() == 2 && PawnSet->Effects.IsEmpty() && PawnSet->Attributes.IsEmpty() &&
		PawnSet->Abilities[0].Ability == UMiniGameplayAbility_Jump::StaticClass() &&
		PawnSet->Abilities[0].Level == 1 && PawnSet->Abilities[0].InputTag == MiniGameplayTags::InputTag_Jump &&
		PawnSet->Abilities[1].Ability == UMiniGameplayAbility_Aim::StaticClass() &&
		PawnSet->Abilities[1].Level == 1 && PawnSet->Abilities[1].InputTag == MiniGameplayTags::InputTag_Aim;
}
}
#endif

bool UMiniTask20AssemblyAssetLibrary::ConfigureCombat(UMiniAbilitySet* PawnSet, UMiniPawnData* PawnData,
	UMiniExperienceActionSet* CombatSet, UInputMappingContext* MappingContext,
	UMiniExperienceDefinition* PracticeExperience, UGameFeatureData* CoreFeature)
{
#if WITH_EDITOR
	if (!Task20IsProductionAsset(PawnSet) || !Task20IsProductionAsset(PawnData) ||
		!Task20IsProductionAsset(CombatSet) || !Task20IsProductionAsset(MappingContext) ||
		!Task20IsProductionAsset(PracticeExperience) || !Task20IsProductionAsset(PawnData->InputConfig) || !CoreFeature ||
		CoreFeature->GetPathName() != TEXT("/MiniShooterCore/GameFeatureData.GameFeatureData"))
	{
		return false;
	}
	TArray<TObjectPtr<UGameFeatureAction>> CoreActions;
	if (!Task20FindCoreActions(CoreFeature, CoreActions)) { return false; }
	UMiniGameFeatureAction_AddInput* InputAction = nullptr;
	for (UGameFeatureAction* Candidate : CombatSet->Actions)
	{
		if (Candidate && Candidate->GetFName() == TEXT("MiniCombat_AddInput"))
		{
			if (InputAction || !Candidate->IsA<UMiniGameFeatureAction_AddInput>())
			{
				return false;
			}
			InputAction = CastChecked<UMiniGameFeatureAction_AddInput>(Candidate);
		}
	}
	PawnSet->Modify();
	PawnData->Modify();
	CombatSet->Modify();
	PracticeExperience->Modify();
	CoreFeature->Modify();
	PawnSet->Abilities.Reset();
	PawnSet->Effects.Reset();
	PawnSet->Attributes.Reset();
	FMiniAbilitySetAbility& Jump = PawnSet->Abilities.AddDefaulted_GetRef();
	Jump.Ability = UMiniGameplayAbility_Jump::StaticClass();
	Jump.Level = 1;
	Jump.InputTag = MiniGameplayTags::InputTag_Jump;
	FMiniAbilitySetAbility& Aim = PawnSet->Abilities.AddDefaulted_GetRef();
	Aim.Ability = UMiniGameplayAbility_Aim::StaticClass();
	Aim.Level = 1;
	Aim.InputTag = MiniGameplayTags::InputTag_Aim;
	PawnData->AbilitySets = {PawnSet};
	if (!InputAction)
	{
		InputAction = NewObject<UMiniGameFeatureAction_AddInput>(CombatSet, TEXT("MiniCombat_AddInput"), RF_Transactional);
	}
	InputAction->Modify();
	InputAction->MappingContext = MappingContext;
	CombatSet->Actions = {InputAction};
	CombatSet->GameFeaturesToEnable = {TEXT("MiniShooterCore")};
	PracticeExperience->Actions.Reset();
	PracticeExperience->GameFeaturesToEnable = {TEXT("MiniShooterCore")};
	CoreFeature->GetMutableActionsInEditor() = MoveTemp(CoreActions);
	const TArray<UObject*> Assets = {PawnSet, PawnData, CombatSet, PracticeExperience, CoreFeature};
	for (UObject* Asset : Assets)
	{
		Asset->MarkPackageDirty();
	}
	return true;
#else
	return false;
#endif
}

bool UMiniTask20AssemblyAssetLibrary::ConfigurePracticeActors(UMiniExperienceActionSet* PracticeSet,
	TSubclassOf<AActor> TargetClass, const TArray<FTransform>& TargetTransforms,
	TSubclassOf<AActor> SupplyClass, const FTransform& SupplyTransform)
{
#if WITH_EDITOR
	if (!Task20IsProductionAsset(PracticeSet) ||
		!Task20ValidActorConfiguration(TargetClass, TargetTransforms, SupplyClass, SupplyTransform))
	{
		return false;
	}
	UMiniGameFeatureAction_AddActors* Action = nullptr;
	for (UGameFeatureAction* Candidate : PracticeSet->Actions)
	{
		if (Candidate && Candidate->GetFName() == TEXT("MiniPractice_AddActors"))
		{
			if (Action || !Candidate->IsA<UMiniGameFeatureAction_AddActors>())
			{
				return false;
			}
			Action = CastChecked<UMiniGameFeatureAction_AddActors>(Candidate);
		}
	}
	PracticeSet->Modify();
	if (!Action)
	{
		Action = NewObject<UMiniGameFeatureAction_AddActors>(PracticeSet, TEXT("MiniPractice_AddActors"), RF_Transactional);
	}
	Action->Modify();
	Action->Actors.Reset();
	for (const FTransform& Transform : TargetTransforms)
	{
		FMiniFeatureActorEntry& Entry = Action->Actors.AddDefaulted_GetRef();
		Entry.ActorClass = TSoftClassPtr<AActor>(TargetClass.Get());
		Entry.Transform = Transform;
	}
	FMiniFeatureActorEntry& Supply = Action->Actors.AddDefaulted_GetRef();
	Supply.ActorClass = TSoftClassPtr<AActor>(SupplyClass.Get());
	Supply.Transform = SupplyTransform;
	PracticeSet->Actions = {Action};
	PracticeSet->GameFeaturesToEnable = {TEXT("MiniShooterCore")};
	PracticeSet->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}

bool UMiniTask20AssemblyAssetLibrary::VerifyAssembly(const UMiniAbilitySet* PawnSet,
	const UMiniPawnData* PawnData, const UMiniExperienceActionSet* CombatSet,
	const UMiniExperienceActionSet* PracticeSet, const UInputMappingContext* MappingContext,
	const UMiniExperienceDefinition* Experience, const UGameFeatureData* CoreFeature)
{
#if WITH_EDITOR
	if (!Task20HasProductionAbilities(PawnSet) || !PawnData || PawnData->AbilitySets.Num() != 1 ||
		PawnData->AbilitySets[0] != PawnSet || !CombatSet || !PracticeSet || !MappingContext || !Experience || !CoreFeature ||
		Experience->DefaultPawnData != PawnData || Experience->ActionSets.Num() != 2 ||
		Experience->ActionSets[0] != CombatSet || Experience->ActionSets[1] != PracticeSet ||
		CombatSet->Actions.Num() != 1 || PracticeSet->Actions.Num() != 1 || !Experience->Actions.IsEmpty() ||
		!Task20HasCoreFeature(Experience->GameFeaturesToEnable) || !Task20HasCoreFeature(CombatSet->GameFeaturesToEnable) ||
		!Task20HasCoreFeature(PracticeSet->GameFeaturesToEnable) || !Task20IsProductionAsset(PawnData->InputConfig))
	{
		return false;
	}
	const UMiniGameFeatureAction_AddInput* Input = Cast<UMiniGameFeatureAction_AddInput>(CombatSet->Actions[0]);
	const UMiniGameFeatureAction_AddActors* Actors = Cast<UMiniGameFeatureAction_AddActors>(PracticeSet->Actions[0]);
	if (!Input || Input->MappingContext != MappingContext || Input->GetFName() != TEXT("MiniCombat_AddInput") ||
		Input->GetOuter() != CombatSet || !Actors || Actors->Actors.Num() != 4 ||
		Actors->GetFName() != TEXT("MiniPractice_AddActors") || Actors->GetOuter() != PracticeSet)
	{
		return false;
	}
	TArray<FTransform> TargetTransforms;
	for (int32 Index = 0; Index < 3; ++Index) { TargetTransforms.Add(Actors->Actors[Index].Transform); }
	TArray<TObjectPtr<UGameFeatureAction>> CoreActions;
	return Task20ValidActorConfiguration(Actors->Actors[0].ActorClass.LoadSynchronous(), TargetTransforms,
		Actors->Actors[3].ActorClass.LoadSynchronous(), Actors->Actors[3].Transform) &&
		Actors->Actors[1].ActorClass == Actors->Actors[0].ActorClass &&
		Actors->Actors[2].ActorClass == Actors->Actors[0].ActorClass &&
		Task20FindCoreActions(CoreFeature, CoreActions) && CoreFeature->GetActions().Num() == 2 &&
		CoreFeature->GetActions()[0] == CoreActions[0] && CoreFeature->GetActions()[1] == CoreActions[1];
#else
	return false;
#endif
}
