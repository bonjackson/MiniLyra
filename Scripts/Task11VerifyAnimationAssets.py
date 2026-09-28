"""Verify saved animation and socket links from a new editor process."""

import unreal


ANIM_FOLDER = "/Game/Mini/Characters/Mannequins/Anims"
anim_blueprint = unreal.load_asset(f"{ANIM_FOLDER}/ABP_MiniPractice.ABP_MiniPractice")
character = unreal.load_asset("/Game/Mini/Characters/BP_MiniCharacter.BP_MiniCharacter")
rifle = unreal.load_asset("/Game/Mini/Weapons/Rifle/Mesh/SK_Rifle.SK_Rifle")
sequence_paths = (
    f"{ANIM_FOLDER}/Rifle/MF_Rifle_Idle_ADS.MF_Rifle_Idle_ADS",
    f"{ANIM_FOLDER}/Rifle/Jog/MF_Rifle_Jog_Fwd.MF_Rifle_Jog_Fwd",
    f"{ANIM_FOLDER}/Unarmed/Jump/MM_Jump.MM_Jump",
    f"{ANIM_FOLDER}/Unarmed/Jump/MM_Fall_Loop.MM_Fall_Loop",
    f"{ANIM_FOLDER}/Unarmed/Jump/MM_Land.MM_Land",
)
sequences = tuple(unreal.load_asset(path) for path in sequence_paths)
if anim_blueprint is None or character is None or rifle is None or any(
    sequence is None for sequence in sequences
):
    raise RuntimeError("Saved Task 11 animation or character assets are missing")
if not unreal.MiniTask11AnimAssetLibrary.verify_practice_anim(
    anim_blueprint, *sequences
):
    raise RuntimeError("Saved Task 11 AnimGraph is invalid")

character_default = unreal.get_default_object(character.generated_class())
mesh = character_default.get_component_by_class(unreal.SkeletalMeshComponent)
rifle_component = character_default.get_editor_property("practice_rifle_mesh")
if mesh is None or rifle_component is None:
    raise RuntimeError("Saved character lost the body or practice rifle component")
if mesh.get_editor_property("anim_class") != anim_blueprint.generated_class():
    raise RuntimeError("Saved character lost its AnimClass")
rotation = mesh.get_editor_property("relative_rotation")
if abs(rotation.pitch) > 0.01 or abs(rotation.yaw + 90.0) > 0.01 or abs(rotation.roll) > 0.01:
    raise RuntimeError(f"Saved character mesh has the wrong orientation: {rotation}")
if rifle_component.get_editor_property("skeletal_mesh_asset") != rifle:
    raise RuntimeError("Saved character lost its practice rifle")
if str(rifle_component.get_attach_socket_name()) != "HandGrip_R":
    raise RuntimeError("Practice rifle is no longer attached to HandGrip_R")
if mesh.get_editor_property("skeletal_mesh_asset").get_editor_property("skeleton") != anim_blueprint.get_editor_property("target_skeleton"):
    raise RuntimeError("Body and animation Blueprint use different skeletons")

unreal.log("MINI_TASK11_ANIMATION_ASSETS_VERIFIED")
