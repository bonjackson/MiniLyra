#include "MiniTask21AssetSetupLibrary.h"

#if WITH_EDITOR
#include "Arena/MiniArenaPhaseConfig.h"
#include "Arena/MiniArenaRulesComponent.h"
#include "Character/MiniPawnData.h"
#include "Engine/Blueprint.h"
#include "GameFeatureAction_AddComponents.h"
#include "GameFeatureData.h"
#include "GameModes/MiniExperienceActionSet.h"
#include "GameModes/MiniExperienceDefinition.h"
#include "GameModes/MiniGamePhaseAbility.h"
#include "GameModes/MiniGameState.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
const FName Task21RulesActionName(TEXT("MiniArena_AddRules"));
const TCHAR* const Task21CombatPath = TEXT("/Game/Mini/System/ActionSets/DA_MiniCombatActionSet.DA_MiniCombatActionSet");
const TCHAR* const Task21PawnPath = TEXT("/Game/Mini/System/PawnData/DA_MiniPracticePawnData.DA_MiniPracticePawnData");
const TCHAR* const Task21FeaturePath = TEXT("/MiniArena/MiniArena.MiniArena");

struct FTask21AssetPaths
{
	const TCHAR* Config;
	const TCHAR* Blueprint;
	const TCHAR* ActionSet;
	const TCHAR* Experience;
};

const FTask21AssetPaths Task21ProductionPaths =
{
	TEXT("/MiniArena/Config/DA_MiniArenaPhaseConfig.DA_MiniArenaPhaseConfig"),
	TEXT("/MiniArena/Components/B_MiniArenaRulesComponent.B_MiniArenaRulesComponent"),
	TEXT("/Game/Mini/System/ActionSets/DA_MiniArenaActionSet.DA_MiniArenaActionSet"),
	TEXT("/Game/Mini/System/Experiences/DA_MiniArenaExperience.DA_MiniArenaExperience")
};
const FTask21AssetPaths Task21DiagnosticsPaths =
{
	TEXT("/Game/Mini/Diagnostics/Arena/DA_MiniArenaDiagnosticsPhaseConfig.DA_MiniArenaDiagnosticsPhaseConfig"),
	TEXT("/Game/Mini/Diagnostics/Arena/B_MiniArenaDiagnosticsRulesComponent.B_MiniArenaDiagnosticsRulesComponent"),
	TEXT("/Game/Mini/Diagnostics/ActionSets/DA_MiniArenaDiagnosticsActionSet.DA_MiniArenaDiagnosticsActionSet"),
	TEXT("/Game/Mini/Diagnostics/Experiences/DA_MiniArenaDiagnosticsExperience.DA_MiniArenaDiagnosticsExperience")
};

bool Task21IsPath(const UObject* Asset, const TCHAR* Path)
{
	return Asset && Asset->GetPathName() == Path;
}

const FTask21AssetPaths* Task21ResolveBlueprintPaths(const UBlueprint* Blueprint)
{
	if (Task21IsPath(Blueprint, Task21ProductionPaths.Blueprint)) { return &Task21ProductionPaths; }
	if (Task21IsPath(Blueprint, Task21DiagnosticsPaths.Blueprint)) { return &Task21DiagnosticsPaths; }
	return nullptr;
}

TArray<TSubclassOf<UMiniGamePhaseAbility>> Task21FixedClasses()
{
	return {UMiniGamePhaseAbility_Warmup::StaticClass(), UMiniGamePhaseAbility_Playing::StaticClass(),
		UMiniGamePhaseAbility_PostMatch::StaticClass()};
}

bool Task21VerifyConfig(const UMiniArenaPhaseConfig* Config, bool bDiagnostics)
{
	const FTask21AssetPaths& Paths = bDiagnostics ? Task21DiagnosticsPaths : Task21ProductionPaths;
	if (!Task21IsPath(Config, Paths.Config) || Config->Phases.Num() != 3) { return false; }
	const TArray<TSubclassOf<UMiniGamePhaseAbility>> Classes = Task21FixedClasses();
	const float Durations[] = {bDiagnostics ? 8.0f : 10.0f, bDiagnostics ? 20.0f : 60.0f, bDiagnostics ? 3.0f : 5.0f};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		// Production and timing fixtures must use the three fixed native classes;
		// ability subclasses used by runtime negative probes never enter these assets.
		if (Config->Phases[Index].AbilityClass != Classes[Index] ||
			Config->Phases[Index].DurationSeconds != Durations[Index]) { return false; }
	}
	FString Error;
	return Config->ValidateConfig(Error);
}

bool Task21VerifyBlueprint(const UBlueprint* Blueprint, const UMiniArenaPhaseConfig* Config)
{
	const FTask21AssetPaths* Paths = Task21ResolveBlueprintPaths(Blueprint);
	if (!Paths || !Task21IsPath(Config, Paths->Config) || Blueprint->ParentClass != UMiniArenaRulesComponent::StaticClass() ||
		Blueprint->Status != BS_UpToDate || !Blueprint->GeneratedClass ||
		Blueprint->GeneratedClass->GetSuperClass() != UMiniArenaRulesComponent::StaticClass() ||
		Blueprint->GeneratedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return false;
	}
	const UMiniArenaRulesComponent* CDO = Cast<UMiniArenaRulesComponent>(Blueprint->GeneratedClass->GetDefaultObject());
	return CDO && CDO->GetIsReplicated() && CDO->PhaseConfig == Config &&
		Task21VerifyConfig(Config, Paths == &Task21DiagnosticsPaths);
}

bool Task21VerifyActionSet(const UMiniExperienceActionSet* ArenaSet, const UBlueprint* Blueprint)
{
	const FTask21AssetPaths* Paths = Task21ResolveBlueprintPaths(Blueprint);
	if (!Paths || !Task21IsPath(ArenaSet, Paths->ActionSet) || !Blueprint->GeneratedClass ||
		ArenaSet->Actions.Num() != 1 || ArenaSet->GameFeaturesToEnable.Num() != 1 ||
		ArenaSet->GameFeaturesToEnable[0] != TEXT("MiniArena")) { return false; }
	const UGameFeatureAction_AddComponents* Action = Cast<UGameFeatureAction_AddComponents>(ArenaSet->Actions[0]);
	if (!Action || Action->GetOuter() != ArenaSet || Action->GetFName() != Task21RulesActionName ||
		Action->ComponentList.Num() != 1) { return false; }
	const FGameFeatureComponentEntry& Entry = Action->ComponentList[0];
	return Entry.ActorClass.ToSoftObjectPath() == FSoftObjectPath(AMiniGameState::StaticClass()) &&
		Entry.ComponentClass.ToSoftObjectPath() == FSoftObjectPath(Blueprint->GeneratedClass) &&
		Entry.bServerComponent && !Entry.bClientComponent && Entry.AdditionFlags == 0;
}
}
#endif

bool UMiniTask21AssetSetupLibrary::ConfigurePhaseConfig(UMiniArenaPhaseConfig* Config, bool bDiagnostics)
{
#if WITH_EDITOR
	const FTask21AssetPaths& Paths = bDiagnostics ? Task21DiagnosticsPaths : Task21ProductionPaths;
	if (!Task21IsPath(Config, Paths.Config)) { return false; }
	if (Task21VerifyConfig(Config, bDiagnostics)) { return true; }
	const TArray<TSubclassOf<UMiniGamePhaseAbility>> Classes = Task21FixedClasses();
	const float Durations[] = {bDiagnostics ? 8.0f : 10.0f, bDiagnostics ? 20.0f : 60.0f, bDiagnostics ? 3.0f : 5.0f};
	Config->Modify();
	Config->Phases.Reset();
	for (int32 Index = 0; Index < 3; ++Index)
	{
		FMiniArenaPhaseEntry& Entry = Config->Phases.AddDefaulted_GetRef();
		Entry.AbilityClass = Classes[Index];
		Entry.DurationSeconds = Durations[Index];
	}
	Config->MarkPackageDirty();
	return Task21VerifyConfig(Config, bDiagnostics);
#else
	return false;
#endif
}

bool UMiniTask21AssetSetupLibrary::EnsureRulesBlueprint(UBlueprint* RulesBlueprint, UMiniArenaPhaseConfig* Config)
{
#if WITH_EDITOR
	const FTask21AssetPaths* Paths = Task21ResolveBlueprintPaths(RulesBlueprint);
	if (!Paths || RulesBlueprint->ParentClass != UMiniArenaRulesComponent::StaticClass() ||
		!Task21VerifyConfig(Config, Paths == &Task21DiagnosticsPaths)) { return false; }
	if (Task21VerifyBlueprint(RulesBlueprint, Config)) { return true; }
	FKismetEditorUtilities::CompileBlueprint(RulesBlueprint);
	UMiniArenaRulesComponent* CDO = RulesBlueprint->GeneratedClass
		? Cast<UMiniArenaRulesComponent>(RulesBlueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!CDO) { return false; }
	RulesBlueprint->Modify();
	CDO->Modify();
	CDO->PhaseConfig = Config;
	FBlueprintEditorUtils::MarkBlueprintAsModified(RulesBlueprint);
	FKismetEditorUtilities::CompileBlueprint(RulesBlueprint);
	RulesBlueprint->MarkPackageDirty();
	return Task21VerifyBlueprint(RulesBlueprint, Config);
#else
	return false;
#endif
}

bool UMiniTask21AssetSetupLibrary::EnsureArenaActionSet(UMiniExperienceActionSet* ArenaSet, UBlueprint* RulesBlueprint)
{
#if WITH_EDITOR
	const FTask21AssetPaths* Paths = Task21ResolveBlueprintPaths(RulesBlueprint);
	const UMiniArenaRulesComponent* CDO = RulesBlueprint && RulesBlueprint->GeneratedClass
		? Cast<UMiniArenaRulesComponent>(RulesBlueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!Paths || !Task21IsPath(ArenaSet, Paths->ActionSet) || !CDO ||
		!Task21VerifyBlueprint(RulesBlueprint, CDO->PhaseConfig)) { return false; }
	if (Task21VerifyActionSet(ArenaSet, RulesBlueprint)) { return true; }
	// Production Task22 adds a Match component. Historical authors cannot
	// replace that complete assembly with the phase-only Task21 configuration.
	if (Paths == &Task21ProductionPaths)
	{
		for (UGameFeatureAction* ExistingAction : ArenaSet->Actions)
		{
			const UGameFeatureAction_AddComponents* Components = Cast<UGameFeatureAction_AddComponents>(ExistingAction);
			if (Components && Components->ComponentList.Num() > 1) { return false; }
		}
	}
	UGameFeatureAction_AddComponents* Action = nullptr;
	for (UGameFeatureAction* Candidate : ArenaSet->Actions)
	{
		if (Candidate && Candidate->GetFName() == Task21RulesActionName)
		{
			if (Action || Candidate->GetOuter() != ArenaSet || !Candidate->IsA<UGameFeatureAction_AddComponents>())
			{
				return false;
			}
			Action = CastChecked<UGameFeatureAction_AddComponents>(Candidate);
		}
	}
	if (!Action)
	{
		UObject* Existing = FindObject<UObject>(ArenaSet, *Task21RulesActionName.ToString());
		if (Existing && !Existing->IsA<UGameFeatureAction_AddComponents>()) { return false; }
		Action = Cast<UGameFeatureAction_AddComponents>(Existing);
	}
	ArenaSet->Modify();
	if (!Action)
	{
		Action = NewObject<UGameFeatureAction_AddComponents>(ArenaSet, Task21RulesActionName, RF_Transactional);
	}
	Action->Modify();
	FGameFeatureComponentEntry Entry;
	Entry.ActorClass = TSoftClassPtr<AActor>(AMiniGameState::StaticClass());
	Entry.ComponentClass = TSoftClassPtr<UActorComponent>(RulesBlueprint->GeneratedClass);
	Entry.bServerComponent = true;
	Entry.bClientComponent = false;
	Entry.AdditionFlags = 0;
	Action->ComponentList = {Entry};
	ArenaSet->Actions = {Action};
	ArenaSet->GameFeaturesToEnable = {TEXT("MiniArena")};
	ArenaSet->MarkPackageDirty();
	return Task21VerifyActionSet(ArenaSet, RulesBlueprint);
#else
	return false;
#endif
}

bool UMiniTask21AssetSetupLibrary::VerifyAssembly(const UMiniArenaPhaseConfig* Config, const UBlueprint* RulesBlueprint,
	const UMiniExperienceActionSet* ArenaSet, const UMiniExperienceDefinition* Experience,
	const UMiniExperienceActionSet* CombatSet, const UMiniPawnData* PawnData,
	const UGameFeatureData* ArenaFeature, bool bDiagnostics)
{
#if WITH_EDITOR
	const FTask21AssetPaths& Paths = bDiagnostics ? Task21DiagnosticsPaths : Task21ProductionPaths;
	if (!Task21VerifyConfig(Config, bDiagnostics) || !Task21IsPath(RulesBlueprint, Paths.Blueprint) ||
		!Task21VerifyBlueprint(RulesBlueprint, Config) || !Task21VerifyActionSet(ArenaSet, RulesBlueprint) ||
		!Task21IsPath(Experience, Paths.Experience) || !Task21IsPath(CombatSet, Task21CombatPath) ||
		!Task21IsPath(PawnData, Task21PawnPath) || !Task21IsPath(ArenaFeature, Task21FeaturePath) ||
		!ArenaFeature->GetActions().IsEmpty() || !Experience->GameFeaturesToEnable.IsEmpty() ||
		!Experience->Actions.IsEmpty() || Experience->DefaultPawnData != PawnData ||
		Experience->ActionSets.Num() != 2 || Experience->ActionSets[0] != CombatSet ||
		Experience->ActionSets[1] != ArenaSet || CombatSet->GameFeaturesToEnable.Num() != 1 ||
		CombatSet->GameFeaturesToEnable[0] != TEXT("MiniShooterCore"))
	{
		return false;
	}
	FString Error;
	return ArenaSet->ValidateActionSet(Error) && Experience->ValidateDefinition(Error);
#else
	return false;
#endif
}
