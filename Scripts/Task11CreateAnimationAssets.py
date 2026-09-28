"""Build the practice Manny AnimGraph and attach a visual-only rifle."""

import unreal


ANIM_FOLDER = "/Game/Mini/Characters/Mannequins/Anims"
ANIM_NAME = "ABP_MiniPractice"
ANIM_PATH = f"{ANIM_FOLDER}/{ANIM_NAME}.{ANIM_NAME}"
CHARACTER_PATH = "/Game/Mini/Characters/BP_MiniCharacter.BP_MiniCharacter"
SKELETON_PATH = "/Game/Mini/Characters/Mannequins/Meshes/SK_Mannequin.SK_Mannequin"
RIFLE_PATH = "/Game/Mini/Weapons/Rifle/Mesh/SK_Rifle.SK_Rifle"
SEQUENCE_PATHS = (
    f"{ANIM_FOLDER}/Rifle/MF_Rifle_Idle_ADS.MF_Rifle_Idle_ADS",
    f"{ANIM_FOLDER}/Rifle/Jog/MF_Rifle_Jog_Fwd.MF_Rifle_Jog_Fwd",
    f"{ANIM_FOLDER}/Unarmed/Jump/MM_Jump.MM_Jump",
    f"{ANIM_FOLDER}/Unarmed/Jump/MM_Fall_Loop.MM_Fall_Loop",
    f"{ANIM_FOLDER}/Unarmed/Jump/MM_Land.MM_Land",
)


def require_asset(path):
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError(f"Required Task 11 asset is unavailable: {path}")
    return asset


parent = unreal.load_class(None, "/Script/FPS.MiniAnimInstance")
skeleton = require_asset(SKELETON_PATH)
sequences = tuple(require_asset(path) for path in SEQUENCE_PATHS)
rifle = require_asset(RIFLE_PATH)
character = require_asset(CHARACTER_PATH)
if parent is None:
    raise RuntimeError("Build FPSEditor before creating the animation Blueprint")

if unreal.EditorAssetLibrary.does_asset_exist(ANIM_PATH):
    anim_blueprint = require_asset(ANIM_PATH)
    if not isinstance(anim_blueprint, unreal.AnimBlueprint):
        raise RuntimeError(f"Unexpected asset at {ANIM_PATH}")
else:
    factory = unreal.AnimBlueprintFactory()
    factory.set_editor_property("parent_class", parent)
    factory.set_editor_property("target_skeleton", skeleton)
    anim_blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        ANIM_NAME, ANIM_FOLDER, unreal.AnimBlueprint.static_class(), factory
    )
    if anim_blueprint is None:
        raise RuntimeError(f"Could not create {ANIM_PATH}")

if not unreal.MiniTask11AnimAssetLibrary.configure_practice_anim(
    anim_blueprint, *sequences
):
    raise RuntimeError("Could not build the Task 11 five-pose AnimGraph")
if not unreal.MiniTask11AnimAssetLibrary.verify_practice_anim(
    anim_blueprint, *sequences
):
    raise RuntimeError("Task 11 AnimGraph did not validate")

generated_class = anim_blueprint.generated_class()
character_default = unreal.get_default_object(character.generated_class())
mesh = character_default.get_component_by_class(unreal.SkeletalMeshComponent)
rifle_component = character_default.get_editor_property("practice_rifle_mesh")
if mesh is None or rifle_component is None:
    raise RuntimeError("BP_MiniCharacter has no body or practice rifle mesh")

character.modify()
mesh.modify()
rifle_component.modify()
mesh.set_editor_property("relative_rotation", unreal.Rotator(pitch=0.0, yaw=-90.0, roll=0.0))
mesh.set_editor_property("animation_mode", unreal.AnimationMode.ANIMATION_BLUEPRINT)
mesh.set_editor_property("anim_class", generated_class)
rifle_component.set_editor_property("skeletal_mesh_asset", rifle)
for asset in (anim_blueprint, character):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")

if mesh.get_editor_property("anim_class") != generated_class:
    raise RuntimeError("Character did not retain the Task 11 AnimClass")
if rifle_component.get_editor_property("skeletal_mesh_asset") != rifle:
    raise RuntimeError("Character did not retain the practice rifle mesh")

unreal.log("MINI_TASK11_ANIMATION_ASSETS_CREATED")
