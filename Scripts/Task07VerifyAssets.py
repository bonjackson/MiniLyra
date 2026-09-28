"""Reload and inspect Task 07's saved assets in a fresh UE Editor process.

This script deliberately does not compile, alter, or save any asset. A passing
result therefore proves that the links survived the asset-authoring process.
"""

import unreal


BLUEPRINT_PATH = "/Game/Mini/Characters/BP_MiniCharacter.BP_MiniCharacter"
NATIVE_CHARACTER_PATH = "/Script/FPS.MiniCharacter"
MESH_PATH = (
    "/Game/Mini/Characters/Mannequins/Meshes/"
    "SKM_Manny_Simple.SKM_Manny_Simple"
)
PAWN_DATA_PATH = (
    "/Game/Mini/System/PawnData/"
    "DA_MiniPracticePawnData.DA_MiniPracticePawnData"
)
FEATURE_DATA_PATH = "/MiniShooterCore/GameFeatureData.GameFeatureData"
ACTION_CLASS_PATH = "/Script/GameFeatures.GameFeatureAction_AddComponents"
EXPECTED_ACTIONS = (
    "MiniTask06_FeatureAddComponents",
    "MiniTask07_CharacterAddComponents",
)


def require_asset(path, expected_class):
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError(f"Saved asset does not reload: {path}")
    actual_class = asset.get_class().get_path_name()
    if actual_class != expected_class:
        raise RuntimeError(f"{path}: expected {expected_class}, got {actual_class}")
    return asset


native_character = unreal.load_class(None, NATIVE_CHARACTER_PATH)
if native_character is None:
    raise RuntimeError(f"Native character class is unavailable: {NATIVE_CHARACTER_PATH}")

blueprint = require_asset(BLUEPRINT_PATH, "/Script/Engine.Blueprint")
parent = blueprint.get_blueprint_parent_class()
if parent is None or parent.get_path_name() != NATIVE_CHARACTER_PATH:
    raise RuntimeError(f"Saved Blueprint has wrong parent: {parent}")

status = str(blueprint.get_editor_property("status"))
if "UP_TO_DATE" not in status:
    raise RuntimeError(f"Saved Blueprint is not compiled and up to date: {status}")
generated_class = blueprint.generated_class()
if generated_class is None:
    raise RuntimeError("Saved Blueprint has no generated class")
if generated_class.get_path_name() != BLUEPRINT_PATH.replace(
    ".BP_MiniCharacter", ".BP_MiniCharacter_C"
):
    raise RuntimeError(f"Saved Blueprint has unexpected generated class: {generated_class}")

character_default = unreal.get_default_object(generated_class)
mesh_component = character_default.get_component_by_class(unreal.SkeletalMeshComponent)
if mesh_component is None:
    raise RuntimeError("Saved Blueprint has no skeletal mesh component")
mesh = mesh_component.get_editor_property("skeletal_mesh_asset")
if mesh is None or mesh.get_path_name() != MESH_PATH:
    raise RuntimeError(f"Saved Blueprint lost Manny Simple mesh: {mesh}")
mesh_rotation = mesh_component.get_editor_property("relative_rotation")
if (abs(mesh_rotation.pitch) > 0.01 or
        abs(mesh_rotation.yaw + 90.0) > 0.01 or
        abs(mesh_rotation.roll) > 0.01):
    raise RuntimeError(f"Saved Blueprint mesh is not upright: {mesh_rotation}")

pawn_data = require_asset(PAWN_DATA_PATH, "/Script/FPS.MiniPawnData")
selected_pawn_class = pawn_data.get_editor_property("pawn_class")
if selected_pawn_class != generated_class:
    raise RuntimeError(f"Saved PawnData points to wrong PawnClass: {selected_pawn_class}")

feature_data = require_asset(FEATURE_DATA_PATH, "/Script/GameFeatures.GameFeatureData")
actions = list(feature_data.get_editor_property("actions"))
for name in EXPECTED_ACTIONS:
    matching = [action for action in actions if action and action.get_name() == name]
    if len(matching) != 1:
        raise RuntimeError(f"Expected one saved {name} Action, got {len(matching)}")
    action = matching[0]
    if action.get_class().get_path_name() != ACTION_CLASS_PATH:
        raise RuntimeError(f"Saved {name} has wrong Action class: {action.get_class()}")

# UE 5.8's Python wrapper does not expose AddComponents.ComponentList. The
# runtime spawn probe verifies the resulting component injection on both roles.

unreal.log(f"MINI_TASK07_ASSET_BLUEPRINT={blueprint.get_path_name()} Status={status}")
unreal.log(f"MINI_TASK07_ASSET_MESH={mesh.get_path_name()}")
unreal.log(f"MINI_TASK07_ASSET_PAWN_CLASS={selected_pawn_class.get_path_name()}")
unreal.log(f"MINI_TASK07_ASSET_ACTIONS={','.join(EXPECTED_ACTIONS)}")
unreal.log("MINI_TASK07_ASSETS_VERIFIED")
