"""Create four weapon montages and connect dedicated combat slots.

The character slot is inserted after Task 11's locomotion output. The two
weapon AnimBPs are purpose-built reference-pose -> slot passthrough graphs.
This script is safe to rerun after its first successful save.
"""

import unreal


SLOT = "DefaultSlot"
BODY_FOLDER = "/Game/Mini/Characters/Mannequins/Anims"
BODY_ABP = f"{BODY_FOLDER}/ABP_MiniPractice"
WEAPONS = (
    ("Rifle", "SK_Rifle_Skeleton", "Weap_Rifle"),
    ("Pistol", "SK_Pistol_Skeleton", "Weap_Pistol"),
)


def require(path, cls):
    asset = unreal.EditorAssetLibrary.load_asset(path)
    if asset is None or not isinstance(asset, cls):
        raise RuntimeError(f"Missing or wrong type: {path}; expected {cls}")
    return asset


def get_or_create(name, folder, cls, factory):
    path = f"{folder}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return require(path, cls)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, cls.static_class(), factory)
    if asset is None or not isinstance(asset, cls):
        raise RuntimeError(f"Could not create {path}")
    return asset


body = require(BODY_ABP, unreal.AnimBlueprint)
if not unreal.MiniTask18AnimAssetLibrary.configure_character_slot(body, SLOT):
    raise RuntimeError("Could not add the combat slot after the existing body movement graph")
if not unreal.MiniTask18AnimAssetLibrary.verify_character_slot(body, SLOT):
    raise RuntimeError("Body movement graph/slot is invalid")
assets_to_save = [body, body.get_editor_property("target_skeleton")]

for weapon, skeleton_name, sequence_prefix in WEAPONS:
    folder = f"/Game/Mini/Weapons/{weapon}/Animations"
    skeleton = require(
        f"/Game/Mini/Weapons/{weapon}/Mesh/{skeleton_name}", unreal.Skeleton
    )
    anim_factory = unreal.AnimBlueprintFactory()
    anim_factory.set_editor_property(
        "parent_class", unreal.load_class(None, "/Script/Engine.AnimInstance")
    )
    anim_factory.set_editor_property("target_skeleton", skeleton)
    anim_bp = get_or_create(f"ABP_Mini{weapon}Weapon", folder, unreal.AnimBlueprint, anim_factory)
    if not unreal.MiniTask18AnimAssetLibrary.configure_weapon_anim(anim_bp, SLOT):
        raise RuntimeError(f"Could not build {weapon} weapon slot AnimGraph")
    if not unreal.MiniTask18AnimAssetLibrary.verify_weapon_anim(anim_bp, SLOT):
        raise RuntimeError(f"Invalid {weapon} weapon slot AnimGraph")
    assets_to_save.extend((anim_bp, skeleton))

    for action in ("Fire", "Reload"):
        sequence = require(f"{folder}/{sequence_prefix}_{action}", unreal.AnimSequence)
        if sequence.get_editor_property("skeleton") != skeleton:
            raise RuntimeError(f"{weapon} {action} sequence uses another skeleton")
        montage_factory = unreal.AnimMontageFactory()
        montage_factory.set_editor_property("target_skeleton", skeleton)
        montage_factory.set_editor_property("source_animation", sequence)
        montage = get_or_create(
            f"AM_Mini{weapon}_{action}", folder, unreal.AnimMontage, montage_factory
        )
        if not unreal.MiniTask18AnimAssetLibrary.configure_montage(montage, sequence, SLOT):
            raise RuntimeError(f"Could not assign {weapon} {action} montage slot")
        if not unreal.MiniTask18AnimAssetLibrary.verify_montage(montage, sequence, SLOT):
            raise RuntimeError(f"Invalid {weapon} {action} montage")
        assets_to_save.append(montage)

for asset in assets_to_save:
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")

unreal.log("MINI_TASK18_ANIMATION_ASSETS_CREATED Slot=DefaultSlot Montages=4")
