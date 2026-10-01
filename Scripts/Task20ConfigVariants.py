"""Author diagnostic runtime fixtures after Assembly and Loadout, preserving production."""

import unreal


SYSTEM = "/Game/Mini/System"
DIAGNOSTICS = "/Game/Mini/Diagnostics"
COPY_FIELDS = (
    "pawn_class", "ability_sets", "tag_relationship_mapping", "input_config",
    "default_camera_mode", "aim_camera_mode",
)
VARIANTS = (
    ("RifleOnly", "DA_MiniRifleOnlyLoadout"),
    ("Unarmed", "DA_MiniUnarmedLoadout"),
)


def require(path, class_path):
    asset = unreal.load_asset(path)
    if asset is None or asset.get_class().get_path_name() != class_path:
        raise RuntimeError(f"Required configuration fixture asset is missing or invalid: {path}")
    return asset


def ensure(folder, name, class_path):
    path = f"{folder}/{name}.{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return require(path, class_path)
    cls = unreal.load_class(None, class_path)
    if cls is None:
        raise RuntimeError(f"Missing configuration fixture class: {class_path}")
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, cls, factory)
    if asset is None:
        raise RuntimeError(f"Could not create configuration fixture: {path}")
    return asset


def snapshot(asset, fields):
    return {field: asset.get_editor_property(field) for field in fields}


pawn = require(f"{SYSTEM}/PawnData/DA_MiniPracticePawnData.DA_MiniPracticePawnData", "/Script/FPS.MiniPawnData")
practice = require(f"{SYSTEM}/Experiences/DA_MiniPracticeExperience.DA_MiniPracticeExperience", "/Script/FPS.MiniExperienceDefinition")
combat = require(f"{SYSTEM}/ActionSets/DA_MiniCombatActionSet.DA_MiniCombatActionSet", "/Script/FPS.MiniExperienceActionSet")
if practice.get_editor_property("default_pawn_data") != pawn or combat not in practice.get_editor_property("action_sets"):
    raise RuntimeError("Production assembly is incomplete; create variants after Assembly and Loadout")
previous_pawn = snapshot(pawn, COPY_FIELDS + ("default_loadout",))
previous_practice = snapshot(practice, ("default_pawn_data", "action_sets", "actions", "game_features_to_enable"))

for variant, loadout_name in VARIANTS:
    loadout = require(f"{SYSTEM}/Loadouts/{loadout_name}.{loadout_name}", "/Script/FPS.MiniLoadoutDefinition")
    variant_pawn = ensure(f"{DIAGNOSTICS}/PawnData", f"DA_Mini{variant}PawnData", "/Script/FPS.MiniPawnData")
    variant_experience = ensure(f"{DIAGNOSTICS}/Experiences", f"DA_Mini{variant}Experience", "/Script/FPS.MiniExperienceDefinition")
    for field in COPY_FIELDS:
        variant_pawn.set_editor_property(field, pawn.get_editor_property(field))
    variant_pawn.set_editor_property("default_loadout", loadout)
    variant_experience.set_editor_property("default_pawn_data", variant_pawn)
    variant_experience.set_editor_property("action_sets", [combat])
    variant_experience.set_editor_property("actions", [])
    variant_experience.set_editor_property("game_features_to_enable", ["MiniShooterCore"])
    for asset in (variant_pawn, variant_experience):
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError(f"Could not save configuration fixture: {asset.get_path_name()}")

if snapshot(pawn, COPY_FIELDS + ("default_loadout",)) != previous_pawn or snapshot(
        practice, ("default_pawn_data", "action_sets", "actions", "game_features_to_enable")) != previous_practice:
    raise RuntimeError("Diagnostic configuration authoring changed production")
unreal.log("MINI_TASK20_CONFIG_VARIANTS_CREATED RifleOnly=1 Unarmed=1 PracticeActors=0 ProductionPreserved=1")
