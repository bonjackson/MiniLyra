#include "MiniTask22AssetSetupLibrary.h"

#if WITH_EDITOR
#include "Arena/MiniArenaPhaseConfig.h"
#include "Arena/MiniArenaRulesComponent.h"
#include "Arena/MiniMatchRulesComponent.h"
#include "Arena/MiniMatchRulesConfig.h"
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
struct FTask22Paths
{
	const TCHAR* PhaseConfig;
	const TCHAR* MatchConfig;
	const TCHAR* PhaseBlueprint;
	const TCHAR* MatchBlueprint;
	const TCHAR* ActionSet;
	const TCHAR* Experience;
};
const FTask22Paths Task22Production = {
	TEXT("/MiniArena/Config/DA_MiniFFAPhaseConfig.DA_MiniFFAPhaseConfig"),
	TEXT("/MiniArena/Config/DA_MiniFFAMatchRules.DA_MiniFFAMatchRules"),
	TEXT("/MiniArena/Components/B_MiniFFAArenaRulesComponent.B_MiniFFAArenaRulesComponent"),
	TEXT("/MiniArena/Components/B_MiniFFAMatchRulesComponent.B_MiniFFAMatchRulesComponent"),
	TEXT("/Game/Mini/System/ActionSets/DA_MiniArenaActionSet.DA_MiniArenaActionSet"),
	TEXT("/Game/Mini/System/Experiences/DA_MiniArenaExperience.DA_MiniArenaExperience")};
const FTask22Paths Task22Diagnostics = {
	TEXT("/Game/Mini/Diagnostics/Arena/DA_MiniFFADiagnosticsPhaseConfig.DA_MiniFFADiagnosticsPhaseConfig"),
	TEXT("/Game/Mini/Diagnostics/Arena/DA_MiniFFADiagnosticsMatchRules.DA_MiniFFADiagnosticsMatchRules"),
	TEXT("/Game/Mini/Diagnostics/Arena/B_MiniFFADiagnosticsArenaRulesComponent.B_MiniFFADiagnosticsArenaRulesComponent"),
	TEXT("/Game/Mini/Diagnostics/Arena/B_MiniFFADiagnosticsMatchRulesComponent.B_MiniFFADiagnosticsMatchRulesComponent"),
	TEXT("/Game/Mini/Diagnostics/ActionSets/DA_MiniFFADiagnosticsActionSet.DA_MiniFFADiagnosticsActionSet"),
	TEXT("/Game/Mini/Diagnostics/Experiences/DA_MiniFFADiagnosticsExperience.DA_MiniFFADiagnosticsExperience")};
const FName Task22ActionName(TEXT("MiniArena_AddRules"));
const FTask22Paths& Task22Paths(bool bDiagnostics) { return bDiagnostics ? Task22Diagnostics : Task22Production; }
bool Task22Path(const UObject* Object, const TCHAR* Path) { return Object && Object->GetPathName() == Path; }
bool Task22PhaseConfig(const UMiniArenaPhaseConfig* Config, bool bDiagnostics)
{
	if (!Task22Path(Config, Task22Paths(bDiagnostics).PhaseConfig) || Config->Phases.Num() != 3) { return false; }
	const TSubclassOf<UMiniGamePhaseAbility> Classes[] = {UMiniGamePhaseAbility_Warmup::StaticClass(),
		UMiniGamePhaseAbility_Playing::StaticClass(), UMiniGamePhaseAbility_PostMatch::StaticClass()};
	const float Durations[] = {0.0f, bDiagnostics ? 20.0f : 300.0f, bDiagnostics ? 3.0f : 5.0f};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		if (Config->Phases[Index].AbilityClass != Classes[Index] || Config->Phases[Index].DurationSeconds != Durations[Index]) { return false; }
	}
	FString Error;
	return Config->ValidateConfig(Error);
}
bool Task22MatchConfig(const UMiniMatchRulesConfig* Config, bool bDiagnostics)
{
	FString Error;
	return Task22Path(Config, Task22Paths(bDiagnostics).MatchConfig) && Config->MinPlayers == 2 &&
		Config->ScoreLimit == 10 && Config->SpawnProtectionSeconds == 2.0f && Config->RespawnDelaySeconds == 3.0f && Config->ValidateConfig(Error);
}
bool Task22BlueprintBase(const UBlueprint* BP, const TCHAR* Path, const UClass* Parent)
{
	return Task22Path(BP, Path) && BP->ParentClass == Parent && BP->Status == BS_UpToDate && BP->GeneratedClass &&
		BP->GeneratedClass->GetSuperClass() == Parent && !BP->GeneratedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists);
}
bool Task22PhaseBlueprint(const UBlueprint* BP, const UMiniArenaPhaseConfig* Config, bool bDiagnostics)
{
	if (!Task22BlueprintBase(BP, Task22Paths(bDiagnostics).PhaseBlueprint, UMiniArenaRulesComponent::StaticClass())) { return false; }
	const UMiniArenaRulesComponent* CDO = Cast<UMiniArenaRulesComponent>(BP->GeneratedClass->GetDefaultObject());
	return CDO && CDO->GetIsReplicated() && CDO->PhaseConfig == Config && Task22PhaseConfig(Config, bDiagnostics);
}
bool Task22MatchBlueprint(const UBlueprint* BP, const UMiniMatchRulesConfig* Config, bool bDiagnostics)
{
	if (!Task22BlueprintBase(BP, Task22Paths(bDiagnostics).MatchBlueprint, UMiniMatchRulesComponent::StaticClass())) { return false; }
	const UMiniMatchRulesComponent* CDO = Cast<UMiniMatchRulesComponent>(BP->GeneratedClass->GetDefaultObject());
	return CDO && CDO->GetIsReplicated() && CDO->MatchRules == Config && Task22MatchConfig(Config, bDiagnostics);
}
bool Task22ActionSet(const UMiniExperienceActionSet* Set, const UBlueprint* PhaseBP, const UBlueprint* MatchBP, bool bDiagnostics)
{
	if (!Task22Path(Set, Task22Paths(bDiagnostics).ActionSet) || !PhaseBP || !PhaseBP->GeneratedClass || !MatchBP || !MatchBP->GeneratedClass ||
		Set->Actions.Num() != 1 || Set->GameFeaturesToEnable != TArray<FString>{TEXT("MiniArena")}) { return false; }
	const UGameFeatureAction_AddComponents* Action = Cast<UGameFeatureAction_AddComponents>(Set->Actions[0]);
	if (!Action || Action->GetOuter() != Set || Action->GetFName() != Task22ActionName || Action->ComponentList.Num() != 2) { return false; }
	const UClass* Classes[] = {PhaseBP->GeneratedClass, MatchBP->GeneratedClass};
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FGameFeatureComponentEntry& Entry = Action->ComponentList[Index];
		if (Entry.ActorClass.ToSoftObjectPath() != FSoftObjectPath(AMiniGameState::StaticClass()) ||
			Entry.ComponentClass.ToSoftObjectPath() != FSoftObjectPath(Classes[Index]) || !Entry.bServerComponent || Entry.bClientComponent || Entry.AdditionFlags != 0) { return false; }
	}
	return true;
}
}
#endif

bool UMiniTask22AssetSetupLibrary::ConfigurePhaseConfig(UMiniArenaPhaseConfig* Config, bool bDiagnostics)
{
#if WITH_EDITOR
	if (!Task22Path(Config, Task22Paths(bDiagnostics).PhaseConfig)) { return false; }
	if (Task22PhaseConfig(Config, bDiagnostics)) { return true; }
	Config->Modify();
	Config->Phases.Reset();
	const TSubclassOf<UMiniGamePhaseAbility> Classes[] = {UMiniGamePhaseAbility_Warmup::StaticClass(), UMiniGamePhaseAbility_Playing::StaticClass(), UMiniGamePhaseAbility_PostMatch::StaticClass()};
	const float Durations[] = {0.0f, bDiagnostics ? 20.0f : 300.0f, bDiagnostics ? 3.0f : 5.0f};
	for (int32 Index = 0; Index < 3; ++Index) { FMiniArenaPhaseEntry& Entry = Config->Phases.AddDefaulted_GetRef(); Entry.AbilityClass = Classes[Index]; Entry.DurationSeconds = Durations[Index]; }
	Config->MarkPackageDirty();
	return Task22PhaseConfig(Config, bDiagnostics);
#else
	return false;
#endif
}
bool UMiniTask22AssetSetupLibrary::ConfigureMatchConfig(UMiniMatchRulesConfig* Config, bool bDiagnostics)
{
#if WITH_EDITOR
	if (!Task22Path(Config, Task22Paths(bDiagnostics).MatchConfig)) { return false; }
	if (Task22MatchConfig(Config, bDiagnostics)) { return true; }
	Config->Modify(); Config->MinPlayers = 2; Config->ScoreLimit = 10; Config->SpawnProtectionSeconds = 2.0f; Config->RespawnDelaySeconds = 3.0f;
	Config->MarkPackageDirty();
	return Task22MatchConfig(Config, bDiagnostics);
#else
	return false;
#endif
}
bool UMiniTask22AssetSetupLibrary::EnsurePhaseRulesBlueprint(UBlueprint* BP, UMiniArenaPhaseConfig* Config, bool bDiagnostics)
{
#if WITH_EDITOR
	if (!Task22Path(BP, Task22Paths(bDiagnostics).PhaseBlueprint) || BP->ParentClass != UMiniArenaRulesComponent::StaticClass() || !Task22PhaseConfig(Config, bDiagnostics)) { return false; }
	if (Task22PhaseBlueprint(BP, Config, bDiagnostics)) { return true; }
	FKismetEditorUtilities::CompileBlueprint(BP);
	UMiniArenaRulesComponent* CDO = BP->GeneratedClass ? Cast<UMiniArenaRulesComponent>(BP->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!CDO) { return false; }
	BP->Modify(); CDO->Modify(); CDO->PhaseConfig = Config;
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP); FKismetEditorUtilities::CompileBlueprint(BP); BP->MarkPackageDirty();
	return Task22PhaseBlueprint(BP, Config, bDiagnostics);
#else
	return false;
#endif
}
bool UMiniTask22AssetSetupLibrary::EnsureMatchRulesBlueprint(UBlueprint* BP, UMiniMatchRulesConfig* Config, bool bDiagnostics)
{
#if WITH_EDITOR
	if (!Task22Path(BP, Task22Paths(bDiagnostics).MatchBlueprint) || BP->ParentClass != UMiniMatchRulesComponent::StaticClass() || !Task22MatchConfig(Config, bDiagnostics)) { return false; }
	if (Task22MatchBlueprint(BP, Config, bDiagnostics)) { return true; }
	FKismetEditorUtilities::CompileBlueprint(BP);
	UMiniMatchRulesComponent* CDO = BP->GeneratedClass ? Cast<UMiniMatchRulesComponent>(BP->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!CDO) { return false; }
	BP->Modify(); CDO->Modify(); CDO->MatchRules = Config;
	FBlueprintEditorUtils::MarkBlueprintAsModified(BP); FKismetEditorUtilities::CompileBlueprint(BP); BP->MarkPackageDirty();
	return Task22MatchBlueprint(BP, Config, bDiagnostics);
#else
	return false;
#endif
}
bool UMiniTask22AssetSetupLibrary::EnsureArenaActionSet(UMiniExperienceActionSet* Set, UBlueprint* PhaseBP, UBlueprint* MatchBP, bool bDiagnostics)
{
#if WITH_EDITOR
	const UMiniArenaRulesComponent* PhaseCDO = PhaseBP && PhaseBP->GeneratedClass ? Cast<UMiniArenaRulesComponent>(PhaseBP->GeneratedClass->GetDefaultObject()) : nullptr;
	const UMiniMatchRulesComponent* MatchCDO = MatchBP && MatchBP->GeneratedClass ? Cast<UMiniMatchRulesComponent>(MatchBP->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!Task22Path(Set, Task22Paths(bDiagnostics).ActionSet) || !PhaseCDO || !MatchCDO ||
		!Task22PhaseBlueprint(PhaseBP, PhaseCDO->PhaseConfig, bDiagnostics) || !Task22MatchBlueprint(MatchBP, MatchCDO->MatchRules, bDiagnostics)) { return false; }
	if (Task22ActionSet(Set, PhaseBP, MatchBP, bDiagnostics)) { return true; }
	// Refuse to replace unrelated Actions while upgrading the one Task21 Action.
	if (Set->Actions.Num() > 1) { return false; }
	UGameFeatureAction_AddComponents* Action = nullptr;
	if (!Set->Actions.IsEmpty())
	{
		Action = Cast<UGameFeatureAction_AddComponents>(Set->Actions[0]);
		if (!Action || Action->GetFName() != Task22ActionName || Action->GetOuter() != Set) { return false; }
	}
	else
	{
		UObject* Existing = FindObject<UObject>(Set, *Task22ActionName.ToString());
		if (Existing && !Existing->IsA<UGameFeatureAction_AddComponents>()) { return false; }
		Action = Cast<UGameFeatureAction_AddComponents>(Existing);
	}
	Set->Modify();
	if (!Action) { Action = NewObject<UGameFeatureAction_AddComponents>(Set, Task22ActionName, RF_Transactional); }
	Action->Modify(); Action->ComponentList.Reset();
	for (UClass* Class : {PhaseBP->GeneratedClass, MatchBP->GeneratedClass})
	{
		FGameFeatureComponentEntry Entry; Entry.ActorClass = TSoftClassPtr<AActor>(AMiniGameState::StaticClass()); Entry.ComponentClass = TSoftClassPtr<UActorComponent>(Class);
		Entry.bServerComponent = true; Entry.bClientComponent = false; Entry.AdditionFlags = 0; Action->ComponentList.Add(Entry);
	}
	Set->Actions = {Action}; Set->GameFeaturesToEnable = {TEXT("MiniArena")}; Set->MarkPackageDirty();
	return Task22ActionSet(Set, PhaseBP, MatchBP, bDiagnostics);
#else
	return false;
#endif
}
bool UMiniTask22AssetSetupLibrary::VerifySavedAssembly(bool bDiagnostics)
{
#if WITH_EDITOR
	const FTask22Paths& Paths = Task22Paths(bDiagnostics);
	const UMiniArenaPhaseConfig* Phase = LoadObject<UMiniArenaPhaseConfig>(nullptr, Paths.PhaseConfig);
	const UMiniMatchRulesConfig* Match = LoadObject<UMiniMatchRulesConfig>(nullptr, Paths.MatchConfig);
	const UBlueprint* PhaseBP = LoadObject<UBlueprint>(nullptr, Paths.PhaseBlueprint);
	const UBlueprint* MatchBP = LoadObject<UBlueprint>(nullptr, Paths.MatchBlueprint);
	const UMiniExperienceActionSet* Set = LoadObject<UMiniExperienceActionSet>(nullptr, Paths.ActionSet);
	const UMiniExperienceDefinition* Experience = LoadObject<UMiniExperienceDefinition>(nullptr, Paths.Experience);
	const UMiniExperienceActionSet* Combat = LoadObject<UMiniExperienceActionSet>(nullptr, TEXT("/Game/Mini/System/ActionSets/DA_MiniCombatActionSet.DA_MiniCombatActionSet"));
	const UMiniPawnData* Pawn = LoadObject<UMiniPawnData>(nullptr, TEXT("/Game/Mini/System/PawnData/DA_MiniPracticePawnData.DA_MiniPracticePawnData"));
	const UGameFeatureData* Feature = LoadObject<UGameFeatureData>(nullptr, TEXT("/MiniArena/MiniArena.MiniArena"));
	if (!Task22PhaseConfig(Phase, bDiagnostics) || !Task22MatchConfig(Match, bDiagnostics) ||
		!Task22PhaseBlueprint(PhaseBP, Phase, bDiagnostics) || !Task22MatchBlueprint(MatchBP, Match, bDiagnostics) ||
		!Task22ActionSet(Set, PhaseBP, MatchBP, bDiagnostics) || !Experience || !Combat || !Pawn || !Feature ||
		!Feature->GetActions().IsEmpty() || !Experience->Actions.IsEmpty() || !Experience->GameFeaturesToEnable.IsEmpty() ||
		Experience->DefaultPawnData != Pawn || Experience->ActionSets.Num() != 2 || Experience->ActionSets[0] != Combat ||
		Experience->ActionSets[1] != Set || Combat->GameFeaturesToEnable != TArray<FString>{TEXT("MiniShooterCore")}) { return false; }
	FString Error;
	return Set->ValidateActionSet(Error) && Experience->ValidateDefinition(Error);
#else
	return false;
#endif
}
