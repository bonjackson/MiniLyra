"""Independently read saved Task 21 assembly; never repair or save an asset.

Native VerifyAssembly inspects the fixed phase classes/durations, Blueprint
parent/CDO and non-Python-exposed FGameFeatureComponentEntry. This script checks
the descriptor, empty GFD, shared combat/pawn links and isolated timing fixture.
It does not execute Task21CreateAssets.py or call any Ensure/Configure API.
"""

import configparser
import json
from pathlib import Path
import unreal


SYSTEM = "/Game/Mini/System"
DIAGNOSTICS = "/Game/Mini/Diagnostics"
PROJECT = Path(unreal.Paths.project_dir())


def require(path, class_path):
    asset = unreal.load_asset(path)
    if asset is None or asset.get_class().get_path_name() != class_path:
        raise RuntimeError(f"Saved arena asset missing or wrong class: {path}")
    return asset


def read_ini(path):
    parser = configparser.ConfigParser(strict=False, interpolation=None)
    parser.read(path, encoding="utf-8-sig")
    return parser


descriptor = json.loads((PROJECT / "Plugins/GameFeatures/MiniArena/MiniArena.uplugin").read_text(encoding="utf-8-sig"))
if (descriptor.get("CanContainContent") is not True or descriptor.get("ExplicitlyLoaded") is not True or
        descriptor.get("EnabledByDefault") is not False or descriptor.get("BuiltInInitialFeatureState") != "Registered" or
        descriptor.get("Modules") or descriptor.get("Plugins") != [
            {"Name": "GameFeatures", "Enabled": True}, {"Name": "ModularGameplay", "Enabled": True}]):
    raise RuntimeError("MiniArena must be content-only, explicitly loaded, Registered, with only framework dependencies")
project_descriptor = json.loads((PROJECT / "FPS.uproject").read_text(encoding="utf-8-sig"))
arena_plugins = [entry for entry in project_descriptor["Plugins"] if entry.get("Name") == "MiniArena"]
if len(arena_plugins) != 1 or arena_plugins[0].get("Enabled") is not True:
    raise RuntimeError("FPS.uproject must enable exactly one MiniArena descriptor for plugin discovery")
game_config = read_ini(PROJECT / "Config/DefaultGame.ini")
engine_config = read_ini(PROJECT / "Config/DefaultEngine.ini")
if game_config["/Script/FPS.MiniAssetManager"].get("DefaultExperienceId") != "MiniExperienceDefinition:DA_MiniPracticeExperience":
    raise RuntimeError("Project default Experience no longer selects the training assembly")
for field in ("GameDefaultMap", "EditorStartupMap"):
    if engine_config["/Script/EngineSettings.GameMapsSettings"].get(field) != "/Game/Mini/Maps/L_MiniPractice":
        raise RuntimeError(f"Training map default changed: {field}")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/MiniArena", SYSTEM, DIAGNOSTICS], force_rescan=True)
feature = require("/MiniArena/MiniArena.MiniArena", "/Script/GameFeatures.GameFeatureData")
core_feature = require("/MiniShooterCore/GameFeatureData.GameFeatureData", "/Script/GameFeatures.GameFeatureData")
if feature.get_name() == core_feature.get_name():
    raise RuntimeError("GameFeatureData names must be unique across both plugins")
if unreal.EditorAssetLibrary.does_asset_exist("/MiniArena/GameFeatureData.GameFeatureData"):
    raise RuntimeError("The ambiguous original MiniArena GameFeatureData asset or redirector remains")
if list(feature.get_editor_property("actions")):
    raise RuntimeError("MiniArena GFD has a global Action that can leak into another World")
combat = require(f"{SYSTEM}/ActionSets/DA_MiniCombatActionSet.DA_MiniCombatActionSet", "/Script/FPS.MiniExperienceActionSet")
pawn = require(f"{SYSTEM}/PawnData/DA_MiniPracticePawnData.DA_MiniPracticePawnData", "/Script/FPS.MiniPawnData")
practice = require(f"{SYSTEM}/Experiences/DA_MiniPracticeExperience.DA_MiniPracticeExperience", "/Script/FPS.MiniExperienceDefinition")
practice_set = require(f"{SYSTEM}/ActionSets/DA_MiniPracticeActionSet.DA_MiniPracticeActionSet", "/Script/FPS.MiniExperienceActionSet")
if (practice.get_editor_property("default_pawn_data") != pawn or
        list(practice.get_editor_property("action_sets")) != [combat, practice_set] or
        list(practice.get_editor_property("game_features_to_enable")) != ["MiniShooterCore"] or
        list(combat.get_editor_property("game_features_to_enable")) != ["MiniShooterCore"] or
        list(practice_set.get_editor_property("game_features_to_enable")) != ["MiniShooterCore"]):
    raise RuntimeError("Training/Combat assembly gained an Arena dependency or lost its existing links")

assemblies = []
for diagnostic in (False, True):
    if not diagnostic and unreal.EditorAssetLibrary.does_asset_exist("/MiniArena/Config/DA_MiniFFAMatchRules.DA_MiniFFAMatchRules"):
        if not unreal.MiniTask22AssetSetupLibrary.verify_saved_assembly(False):
            raise RuntimeError("Task22 production FFA assembly failed independent verification")
        unreal.log("MINI_TASK21_PRODUCTION_MIGRATED_TO_FFA ReadOnly=1")
        continue
    if diagnostic:
        config_path = f"{DIAGNOSTICS}/Arena/DA_MiniArenaDiagnosticsPhaseConfig.DA_MiniArenaDiagnosticsPhaseConfig"
        blueprint_path = f"{DIAGNOSTICS}/Arena/B_MiniArenaDiagnosticsRulesComponent.B_MiniArenaDiagnosticsRulesComponent"
        set_path = f"{DIAGNOSTICS}/ActionSets/DA_MiniArenaDiagnosticsActionSet.DA_MiniArenaDiagnosticsActionSet"
        experience_path = f"{DIAGNOSTICS}/Experiences/DA_MiniArenaDiagnosticsExperience.DA_MiniArenaDiagnosticsExperience"
    else:
        config_path = "/MiniArena/Config/DA_MiniArenaPhaseConfig.DA_MiniArenaPhaseConfig"
        blueprint_path = "/MiniArena/Components/B_MiniArenaRulesComponent.B_MiniArenaRulesComponent"
        set_path = f"{SYSTEM}/ActionSets/DA_MiniArenaActionSet.DA_MiniArenaActionSet"
        experience_path = f"{SYSTEM}/Experiences/DA_MiniArenaExperience.DA_MiniArenaExperience"
    config = require(config_path, "/Script/FPS.MiniArenaPhaseConfig")
    blueprint = require(blueprint_path, "/Script/Engine.Blueprint")
    action_set = require(set_path, "/Script/FPS.MiniExperienceActionSet")
    experience = require(experience_path, "/Script/FPS.MiniExperienceDefinition")
    if (list(experience.get_editor_property("action_sets")) != [combat, action_set] or
            experience.get_editor_property("default_pawn_data") != pawn or
            list(experience.get_editor_property("actions")) or
            list(experience.get_editor_property("game_features_to_enable")) or
            list(action_set.get_editor_property("game_features_to_enable")) != ["MiniArena"]):
        raise RuntimeError(f"Arena Experience must share Combat/PawnData and declare Arena through its own ActionSet: {experience_path}")
    actions = list(action_set.get_editor_property("actions"))
    if (len(actions) != 1 or actions[0] is None or actions[0].get_name() != "MiniArena_AddRules" or
            actions[0].get_class().get_path_name() != "/Script/GameFeatures.GameFeatureAction_AddComponents" or
            actions[0].get_outer() != action_set):
        raise RuntimeError(f"Arena ActionSet lost its sole stock AddComponents Action: {set_path}")
    if not unreal.MiniTask21AssetSetupLibrary.verify_assembly(
            config, blueprint, action_set, experience, combat, pawn, feature, diagnostic):
        raise RuntimeError(f"Fixed classes/durations, Rules CDO or server-only component entry failed native read-only verification: {experience_path}")
    assemblies.append((config, blueprint, action_set, experience))

if len(assemblies) == 2 and any(production == diagnostic for production, diagnostic in zip(*assemblies)):
    raise RuntimeError("Timing diagnostics must own four distinct assets")
# Both roots are scanned for primary IDs. Check that each new selectable
# Experience/ActionSet name occurs only once across the production/diagnostic roots.
for kind, index in (("Experiences", 3), ("ActionSets", 2)):
    assets = []
    for root in (SYSTEM, DIAGNOSTICS):
        assets.extend(registry.get_assets_by_path(f"{root}/{kind}", recursive=True))
    for assembly in assemblies:
        name = assembly[index].get_name()
        matching = [asset for asset in assets if str(asset.asset_name) == name]
        if len(matching) != 1:
            raise RuntimeError(f"Primary asset name missing or duplicated: {name} count={len(matching)}")
unreal.log("MINI_TASK21_ASSETS_VERIFIED LegacyProduction=10/60/5OrTask22FFA Diagnostics=8/20/3 FixedNativeClasses=1 ServerOnly=1 TrainingDefault=1")
