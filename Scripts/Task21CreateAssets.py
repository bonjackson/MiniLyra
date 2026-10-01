"""Idempotently author only Task 21 arena assets after building FPSEditor.

MiniArena GameFeatureData intentionally has no Actions. Each arena Experience
owns [shared CombatSet, its ArenaSet]; only ArenaSet declares MiniArena and
injects the replicated rules BP on the server. No map/default entry is edited.
Production uses the three fixed native phase abilities at 10/60/5 seconds;
the independent Diagnostics config/BP/ActionSet/Experience uses 8/20/3.
"""

import hashlib
from pathlib import Path
import unreal


SYSTEM = "/Game/Mini/System"
DIAGNOSTICS = "/Game/Mini/Diagnostics"
FEATURE_PATH = "/MiniArena/MiniArena.MiniArena"
PROJECT = Path(unreal.Paths.project_dir())
PROTECTED_FILES = (
    "Config/DefaultEngine.ini", "Config/DefaultGame.ini",
    "Content/Mini/Maps/L_MiniPractice.umap",
    "Content/Mini/System/Experiences/DA_MiniPracticeExperience.uasset",
    "Content/Mini/System/PawnData/DA_MiniPracticePawnData.uasset",
    "Content/Mini/System/ActionSets/DA_MiniCombatActionSet.uasset",
    "Content/Mini/System/ActionSets/DA_MiniPracticeActionSet.uasset",
    "Content/Mini/System/AbilitySets/DA_MiniPawnAbilitySet.uasset",
    "Content/Mini/System/Input/IMC_MiniDefault.uasset",
    "Plugins/GameFeatures/MiniShooterCore/Content/GameFeatureData.uasset",
)


def protected_hashes():
    result = {}
    for relative in PROTECTED_FILES:
        path = PROJECT / relative
        if not path.is_file():
            raise RuntimeError(f"Required shared task 20 file is missing: {path}")
        result[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
    return result


def require_class(path):
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError(f"Task 21 class unavailable; build FPSEditor first: {path}")
    return cls


def require(path, class_path):
    asset = unreal.load_asset(path)
    if asset is None or asset.get_class().get_path_name() != class_path:
        raise RuntimeError(f"Required Task 21 asset missing or wrong class: {path}")
    return asset


def ensure(folder, name, class_path, parent=None):
    path = f"{folder}/{name}.{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return require(path, class_path)
    cls = require_class(class_path)
    if parent is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", cls)
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", parent)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, cls, factory)
    if asset is None:
        raise RuntimeError(f"Could not create arena asset: {path}")
    return asset


def save(*assets):
    for asset in assets:
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError(f"Could not save {asset.get_path_name()}")


previous_shared_files = protected_hashes()
for name in (
    "MiniTask21AssetSetupLibrary", "MiniArenaPhaseConfig", "MiniArenaRulesComponent",
    "MiniGamePhaseAbility_Warmup", "MiniGamePhaseAbility_Playing", "MiniGamePhaseAbility_PostMatch",
):
    require_class(f"/Script/FPS.{name}")
parent_class = require_class("/Script/FPS.MiniArenaRulesComponent")
library = unreal.MiniTask21AssetSetupLibrary
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/MiniArena", SYSTEM, DIAGNOSTICS], force_rescan=True)
combat = require(f"{SYSTEM}/ActionSets/DA_MiniCombatActionSet.DA_MiniCombatActionSet", "/Script/FPS.MiniExperienceActionSet")
pawn = require(f"{SYSTEM}/PawnData/DA_MiniPracticePawnData.DA_MiniPracticePawnData", "/Script/FPS.MiniPawnData")
practice = require(f"{SYSTEM}/Experiences/DA_MiniPracticeExperience.DA_MiniPracticeExperience", "/Script/FPS.MiniExperienceDefinition")
practice_set = require(f"{SYSTEM}/ActionSets/DA_MiniPracticeActionSet.DA_MiniPracticeActionSet", "/Script/FPS.MiniExperienceActionSet")
if (practice.get_editor_property("default_pawn_data") != pawn or
        list(practice.get_editor_property("action_sets")) != [combat, practice_set]):
    raise RuntimeError("Task 20 production assembly is incomplete")

feature = ensure("/MiniArena", "MiniArena", "/Script/GameFeatures.GameFeatureData")
feature.set_editor_property("actions", [])
save(feature)

for diagnostic in (False, True):
    if not diagnostic and unreal.EditorAssetLibrary.does_asset_exist("/MiniArena/Config/DA_MiniFFAMatchRules.DA_MiniFFAMatchRules"):
        # Task22 owns the production ArenaSet now. Never downgrade it to a
        # single phase component when replaying a historical asset author.
        if not unreal.MiniTask22AssetSetupLibrary.verify_saved_assembly(False):
            raise RuntimeError("Task22 production FFA assembly is invalid; refusing Task21 downgrade")
        unreal.log("MINI_TASK21_PRODUCTION_MIGRATED_TO_FFA ReadOnly=1")
        continue
    if diagnostic:
        config_folder = blueprint_folder = f"{DIAGNOSTICS}/Arena"
        config_name = "DA_MiniArenaDiagnosticsPhaseConfig"
        blueprint_name = "B_MiniArenaDiagnosticsRulesComponent"
        action_folder, action_name = f"{DIAGNOSTICS}/ActionSets", "DA_MiniArenaDiagnosticsActionSet"
        experience_folder, experience_name = f"{DIAGNOSTICS}/Experiences", "DA_MiniArenaDiagnosticsExperience"
    else:
        config_folder, config_name = "/MiniArena/Config", "DA_MiniArenaPhaseConfig"
        blueprint_folder, blueprint_name = "/MiniArena/Components", "B_MiniArenaRulesComponent"
        action_folder, action_name = f"{SYSTEM}/ActionSets", "DA_MiniArenaActionSet"
        experience_folder, experience_name = f"{SYSTEM}/Experiences", "DA_MiniArenaExperience"

    config = ensure(config_folder, config_name, "/Script/FPS.MiniArenaPhaseConfig")
    blueprint = ensure(blueprint_folder, blueprint_name, "/Script/Engine.Blueprint", parent_class)
    action_set = ensure(action_folder, action_name, "/Script/FPS.MiniExperienceActionSet")
    experience = ensure(experience_folder, experience_name, "/Script/FPS.MiniExperienceDefinition")
    if not library.configure_phase_config(config, diagnostic):
        raise RuntimeError(f"Native bridge rejected fixed phase defaults: {config.get_path_name()}")
    if not library.ensure_rules_blueprint(blueprint, config):
        raise RuntimeError(f"Native bridge rejected rules Blueprint/CDO: {blueprint.get_path_name()}")
    if not library.ensure_arena_action_set(action_set, blueprint):
        raise RuntimeError(f"Native bridge rejected stock server-only AddComponents: {action_set.get_path_name()}")
    experience.set_editor_property("default_pawn_data", pawn)
    experience.set_editor_property("action_sets", [combat, action_set])
    experience.set_editor_property("actions", [])
    experience.set_editor_property("game_features_to_enable", [])
    save(config, blueprint, action_set, experience)
    if not library.verify_assembly(config, blueprint, action_set, experience, combat, pawn, feature, diagnostic):
        raise RuntimeError(f"Arena assets failed native verification: {experience.get_path_name()}")
    unreal.log(f"MINI_TASK21_ARENA_EXPERIENCE={experience.get_path_name()} Diagnostics={int(diagnostic)}")

if protected_hashes() != previous_shared_files:
    raise RuntimeError("Task 21 authoring changed a shared training asset or default entry")
unreal.log("MINI_TASK21_ASSETS_CREATED LegacyProduction=10/60/5OrTask22FFA Diagnostics=8/20/3 AddComponents=ServerOnly TrainingPreserved=1")
