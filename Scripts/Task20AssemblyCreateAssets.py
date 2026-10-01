"""Separate legacy diagnostics, then assemble the production combat/practice Actions."""

from pathlib import Path
import unreal


SYSTEM = "/Game/Mini/System"
DIAGNOSTICS = "/Game/Mini/Diagnostics"
SCRIPT_DIR = Path(unreal.Paths.project_dir()) / "Scripts"


def require(path, class_path=None):
    asset = unreal.load_asset(path)
    if asset is None or (class_path and asset.get_class().get_path_name() != class_path):
        raise RuntimeError(f"Required assembly asset missing or wrong class: {path}")
    return asset


def ensure(folder, name, class_path):
    path = f"{folder}/{name}.{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return require(path, class_path)
    cls = unreal.load_class(None, class_path)
    if cls is None:
        raise RuntimeError(f"Required class unavailable: {class_path}")
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, cls, factory)
    if asset is None:
        raise RuntimeError(f"Could not create {path}")
    return asset


def save(*assets):
    for asset in assets:
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError(f"Could not save {asset.get_path_name()}")


def run_legacy(name):
    path = SCRIPT_DIR / name
    exec(compile(path.read_text(encoding="utf-8-sig"), str(path), "exec"), {"__file__": str(path)})


practice = require(f"{SYSTEM}/Experiences/DA_MiniPracticeExperience.DA_MiniPracticeExperience", "/Script/FPS.MiniExperienceDefinition")
pawn = require(f"{SYSTEM}/PawnData/DA_MiniPracticePawnData.DA_MiniPracticePawnData", "/Script/FPS.MiniPawnData")
practice_set = require(f"{SYSTEM}/ActionSets/DA_MiniPracticeActionSet.DA_MiniPracticeActionSet", "/Script/FPS.MiniExperienceActionSet")
core = require("/MiniShooterCore/GameFeatureData.GameFeatureData", "/Script/GameFeatures.GameFeatureData")
mapping = require(f"{SYSTEM}/Input/IMC_MiniDefault.IMC_MiniDefault", "/Script/EnhancedInput.InputMappingContext")
pawn_set = require(f"{SYSTEM}/AbilitySets/DA_MiniPawnAbilitySet.DA_MiniPawnAbilitySet", "/Script/FPS.MiniAbilitySet")
diagnostic_pawn = ensure(f"{DIAGNOSTICS}/PawnData", "DA_MiniDiagnosticsPawnData", "/Script/FPS.MiniPawnData")
diagnostic_experience = ensure(f"{DIAGNOSTICS}/Experiences", "DA_MiniDiagnosticsExperience", "/Script/FPS.MiniExperienceDefinition")
diagnostic_set = ensure(f"{DIAGNOSTICS}/ActionSets", "DA_MiniDiagnosticsActionSet", "/Script/FPS.MiniExperienceActionSet")
diagnostic_pawn_set = ensure(f"{DIAGNOSTICS}/AbilitySets", "DA_MiniPawnAbilitySet", "/Script/FPS.MiniAbilitySet")
# Copy only shared character/camera/equipment settings. Probe authors own the
# diagnostic ability/input links; they never write the production assets.
for field in ("pawn_class", "default_camera_mode", "aim_camera_mode", "default_loadout"):
    diagnostic_pawn.set_editor_property(field, pawn.get_editor_property(field))
diagnostic_experience.set_editor_property("default_pawn_data", diagnostic_pawn)
diagnostic_experience.set_editor_property("action_sets", [diagnostic_set])
save(diagnostic_pawn, diagnostic_experience, diagnostic_set, diagnostic_pawn_set)
for name in ("Task06CreateAssets.py", "Task07CreateAssets.py", "Task09CreateAssets.py",
             "Task10CreateAssets.py", "Task12CreateAssets.py"):
    run_legacy(name)

combat_set = ensure(f"{SYSTEM}/ActionSets", "DA_MiniCombatActionSet", "/Script/FPS.MiniExperienceActionSet")
if not unreal.MiniTask20AssemblyAssetLibrary.configure_combat(pawn_set, pawn, combat_set, mapping, practice, core):
    raise RuntimeError("Production combat bridge rejected assembly")
target_class = unreal.load_class(None, "/Game/Mini/Targets/BP_MiniPracticeTarget.BP_MiniPracticeTarget_C")
supply_class = unreal.load_class(None, "/Script/FPS.MiniPracticeSupply")
transforms = [unreal.Transform(location=unreal.Vector(x, y, 120.0), rotation=unreal.Rotator(yaw=90.0))
              for x, y in ((-700.0, 500.0), (0.0, 650.0), (700.0, 500.0))]
supply_transform = unreal.Transform(location=unreal.Vector(0.0, -1150.0, 10.0))
if not unreal.MiniTask20AssemblyAssetLibrary.configure_practice_actors(
        practice_set, target_class, transforms, supply_class, supply_transform):
    raise RuntimeError("Practice actor bridge rejected assembly")
practice.set_editor_property("action_sets", [combat_set, practice_set])
practice.set_editor_property("game_features_to_enable", ["MiniShooterCore"])
save(pawn_set, pawn, combat_set, practice_set, practice, core)
if not unreal.MiniTask20AssemblyAssetLibrary.verify_assembly(
        pawn_set, pawn, combat_set, practice_set, mapping, practice, core):
    raise RuntimeError("Production assembly failed native verification")
unreal.log("MINI_TASK20_ASSEMBLY_CREATED")
