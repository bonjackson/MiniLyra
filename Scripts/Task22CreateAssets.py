"""Author fixed production FFA assets and a separate timing fixture.

The Task21 phase-only assets remain unchanged. Only the production ArenaSet is
upgraded to inject both replicated rules components through its stock Action.
"""
import hashlib
from pathlib import Path
import unreal

PROJECT = Path(unreal.Paths.project_dir())
SYSTEM = "/Game/Mini/System"
DIAG = "/Game/Mini/Diagnostics"
PROTECTED = (
    "Config/DefaultEngine.ini", "Config/DefaultGame.ini",
    "Content/Mini/Maps/L_MiniPractice.umap",
    "Content/Mini/System/Experiences/DA_MiniPracticeExperience.uasset",
    "Content/Mini/System/PawnData/DA_MiniPracticePawnData.uasset",
    "Content/Mini/System/ActionSets/DA_MiniCombatActionSet.uasset",
    "Content/Mini/System/ActionSets/DA_MiniPracticeActionSet.uasset",
    "Content/Mini/System/AbilitySets/DA_MiniPawnAbilitySet.uasset",
    "Plugins/GameFeatures/MiniShooterCore/Content/GameFeatureData.uasset",
    "Plugins/GameFeatures/MiniArena/Content/MiniArena.uasset",
    "Plugins/GameFeatures/MiniArena/Content/Config/DA_MiniArenaPhaseConfig.uasset",
    "Plugins/GameFeatures/MiniArena/Content/Components/B_MiniArenaRulesComponent.uasset",
    "Content/Mini/Diagnostics/Arena/DA_MiniArenaDiagnosticsPhaseConfig.uasset",
    "Content/Mini/Diagnostics/Arena/B_MiniArenaDiagnosticsRulesComponent.uasset",
    "Content/Mini/Diagnostics/ActionSets/DA_MiniArenaDiagnosticsActionSet.uasset",
    "Content/Mini/Diagnostics/Experiences/DA_MiniArenaDiagnosticsExperience.uasset",
)


def hashes():
    return {name: hashlib.sha256((PROJECT / name).read_bytes()).hexdigest() for name in PROTECTED}


def require(path, cls):
    asset = unreal.load_asset(path)
    if asset is None or asset.get_class().get_path_name() != cls:
        raise RuntimeError(f"Required FFA asset missing or wrong class: {path}")
    return asset


def ensure(folder, name, class_path, parent=None):
    path = f"{folder}/{name}.{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return require(path, class_path)
    cls = unreal.load_class(None, class_path)
    if cls is None:
        raise RuntimeError(f"Build FPSEditor first: {class_path}")
    if parent:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", parent)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", cls)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, cls, factory)
    if asset is None:
        raise RuntimeError(f"Could not create {path}")
    return asset


before = hashes()
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/MiniArena", SYSTEM, DIAG], force_rescan=True)
phase_parent = unreal.load_class(None, "/Script/FPS.MiniArenaRulesComponent")
match_parent = unreal.load_class(None, "/Script/FPS.MiniMatchRulesComponent")
if phase_parent is None or match_parent is None:
    raise RuntimeError("Build the Task22 FPSEditor classes first")
library = unreal.MiniTask22AssetSetupLibrary
combat = require(f"{SYSTEM}/ActionSets/DA_MiniCombatActionSet.DA_MiniCombatActionSet", "/Script/FPS.MiniExperienceActionSet")
pawn = require(f"{SYSTEM}/PawnData/DA_MiniPracticePawnData.DA_MiniPracticePawnData", "/Script/FPS.MiniPawnData")
for diagnostic in (False, True):
    config_folder = f"{DIAG}/Arena" if diagnostic else "/MiniArena/Config"
    bp_folder = f"{DIAG}/Arena" if diagnostic else "/MiniArena/Components"
    prefix = "MiniFFADiagnostics" if diagnostic else "MiniFFA"
    phase = ensure(config_folder, f"DA_{prefix}PhaseConfig", "/Script/FPS.MiniArenaPhaseConfig")
    match = ensure(config_folder, f"DA_{prefix}MatchRules", "/Script/FPS.MiniMatchRulesConfig")
    phase_bp = ensure(bp_folder, f"B_{prefix}ArenaRulesComponent", "/Script/Engine.Blueprint", phase_parent)
    match_bp = ensure(bp_folder, f"B_{prefix}MatchRulesComponent", "/Script/Engine.Blueprint", match_parent)
    root = DIAG if diagnostic else SYSTEM
    set_name = "DA_MiniFFADiagnosticsActionSet" if diagnostic else "DA_MiniArenaActionSet"
    experience_name = "DA_MiniFFADiagnosticsExperience" if diagnostic else "DA_MiniArenaExperience"
    action_set = ensure(f"{root}/ActionSets", set_name, "/Script/FPS.MiniExperienceActionSet")
    experience = ensure(f"{root}/Experiences", experience_name, "/Script/FPS.MiniExperienceDefinition")
    for passed, step in (
        (library.configure_phase_config(phase, diagnostic), "phase defaults"),
        (library.configure_match_config(match, diagnostic), "match defaults"),
        (library.ensure_phase_rules_blueprint(phase_bp, phase, diagnostic), "phase CDO"),
        (library.ensure_match_rules_blueprint(match_bp, match, diagnostic), "match CDO"),
        (library.ensure_arena_action_set(action_set, phase_bp, match_bp, diagnostic), "stock AddComponents"),
    ):
        if not passed:
            raise RuntimeError(f"Fixed-path Task22 bridge rejected {step}, diagnostic={diagnostic}")
    experience.set_editor_property("default_pawn_data", pawn)
    experience.set_editor_property("action_sets", [combat, action_set])
    experience.set_editor_property("actions", [])
    experience.set_editor_property("game_features_to_enable", [])
    for asset in (phase, match, phase_bp, match_bp, action_set, experience):
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError(f"Could not save {asset.get_path_name()}")
    if not library.verify_saved_assembly(diagnostic):
        raise RuntimeError(f"Saved FFA assembly failed verification: diagnostic={diagnostic}")
if hashes() != before:
    raise RuntimeError("Task22 author changed a protected training or phase-only fixture")
unreal.log("MINI_TASK22_ASSETS_CREATED Production=0/300/5 Diagnostics=0/20/3 Match=2/10/2/3 TrainingAndTask21Preserved=1")
