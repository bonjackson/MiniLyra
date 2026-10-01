"""Read saved production assembly and isolated diagnostic Actions without editing."""

from pathlib import Path
import unreal


def require(path):
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError(f"Saved assembly asset missing: {path}")
    return asset


system = "/Game/Mini/System"
pawn_set = require(f"{system}/AbilitySets/DA_MiniPawnAbilitySet.DA_MiniPawnAbilitySet")
pawn = require(f"{system}/PawnData/DA_MiniPracticePawnData.DA_MiniPracticePawnData")
combat = require(f"{system}/ActionSets/DA_MiniCombatActionSet.DA_MiniCombatActionSet")
practice_set = require(f"{system}/ActionSets/DA_MiniPracticeActionSet.DA_MiniPracticeActionSet")
mapping = require(f"{system}/Input/IMC_MiniDefault.IMC_MiniDefault")
experience = require(f"{system}/Experiences/DA_MiniPracticeExperience.DA_MiniPracticeExperience")
core = require("/MiniShooterCore/GameFeatureData.GameFeatureData")
if not unreal.MiniTask20AssemblyAssetLibrary.verify_assembly(
        pawn_set, pawn, combat, practice_set, mapping, experience, core):
    raise RuntimeError("Saved production assembly differs from configuration")
actors = list(practice_set.get_editor_property("actions"))[0].get_editor_property("actors")
for index, expected in enumerate(((-700.0, 500.0, 120.0), (0.0, 650.0, 120.0),
                                  (700.0, 500.0, 120.0), (0.0, -1150.0, 10.0))):
    entry = actors[index]
    transform = entry.get_editor_property("transform")
    location = transform.translation
    if max(abs(location.x - expected[0]), abs(location.y - expected[1]), abs(location.z - expected[2])) > 0.01:
        raise RuntimeError(f"Actor {index} has wrong placement: {location}")
    expected_class = "/Game/Mini/Targets/BP_MiniPracticeTarget.BP_MiniPracticeTarget_C" if index < 3 else "/Script/FPS.MiniPracticeSupply"
    if expected_class not in str(entry.get_editor_property("actor_class")):
        raise RuntimeError(f"Actor {index} has wrong class")
    if index < 3 and abs(transform.rotation.rotator().yaw - 90.0) > 0.01:
        raise RuntimeError(f"Target {index} does not face the shooting lane")
diagnostics = require("/Game/Mini/Diagnostics/Experiences/DA_MiniDiagnosticsExperience.DA_MiniDiagnosticsExperience")
expected_names = {"MiniTask06_FeatureAddComponents", "MiniTask06_ExperienceAddComponents",
                  "MiniTask07_CharacterAddComponents", "MiniTask09_AddAbilities", "MiniTask10_AddInput"}
names = [action.get_name() for action in diagnostics.get_editor_property("actions") if action]
if set(names) != expected_names or len(names) != len(expected_names):
    raise RuntimeError(f"Diagnostics lost Actions: {names}")
if diagnostics.get_editor_property("default_pawn_data") == pawn:
    raise RuntimeError("Diagnostics still shares production PawnData")
diagnostic_pawn = require("/Game/Mini/Diagnostics/PawnData/DA_MiniDiagnosticsPawnData.DA_MiniDiagnosticsPawnData")
diagnostic_set = require("/Game/Mini/Diagnostics/ActionSets/DA_MiniDiagnosticsActionSet.DA_MiniDiagnosticsActionSet")
if (diagnostics.get_editor_property("default_pawn_data") != diagnostic_pawn or
        list(diagnostics.get_editor_property("action_sets")) != [diagnostic_set] or
        list(diagnostic_set.get_editor_property("actions")) or
        list(diagnostics.get_editor_property("game_features_to_enable")) != ["MiniShooterCore"] or
        list(diagnostic_set.get_editor_property("game_features_to_enable")) != ["MiniShooterCore"]):
    raise RuntimeError("Diagnostics lost its isolated links or real plugin activation coverage")
for field in ("input_config", "tag_relationship_mapping"):
    asset = diagnostic_pawn.get_editor_property(field)
    if asset is None or not asset.get_path_name().startswith("/Game/Mini/Diagnostics/"):
        raise RuntimeError(f"Diagnostics still shares production {field}")
for asset in diagnostic_pawn.get_editor_property("ability_sets"):
    if asset is None or not asset.get_path_name().startswith("/Game/Mini/Diagnostics/AbilitySets/"):
        raise RuntimeError("Diagnostic probe abilities escaped their directory")
missing = require("/Game/Mini/Diagnostics/Experiences/DA_MiniMissingFeatureExperience.DA_MiniMissingFeatureExperience")
if list(missing.get_editor_property("game_features_to_enable")) != ["MiniDefinitelyMissing"]:
    raise RuntimeError("Missing-plugin fixture lost its intentional failure")

# Reuse the detailed, read-only legacy checks on the relocated probe assets.
script_directory = Path(unreal.Paths.project_dir()) / "Scripts"
for name in ("Task07VerifyAssets.py", "Task09VerifyAssets.py", "Task10VerifyAssets.py", "Task12VerifyAssets.py"):
    path = script_directory / name
    exec(compile(path.read_text(encoding="utf-8-sig"), str(path), "exec"), {"__file__": str(path)})
unreal.log("MINI_TASK20_ASSEMBLY_VERIFIED")
