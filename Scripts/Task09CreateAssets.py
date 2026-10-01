"""Idempotently author the two Task 09 AbilitySets and scoped Action."""

import unreal


SET_FOLDER = "/Game/Mini/Diagnostics/AbilitySets"
PAWN_SET_NAME = "DA_MiniPawnAbilitySet"
FEATURE_SET_NAME = "DA_MiniFeatureAbilitySet"
PAWN_DATA_PATH = "/Game/Mini/Diagnostics/PawnData/DA_MiniDiagnosticsPawnData.DA_MiniDiagnosticsPawnData"
EXPERIENCE_PATH = "/Game/Mini/Diagnostics/Experiences/DA_MiniDiagnosticsExperience.DA_MiniDiagnosticsExperience"
ACTION_NAME = "MiniTask09_AddAbilities"


def require_class(path):
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError(f"Required Task 09 class is unavailable: {path}")
    return cls


def require_asset(path, cls):
    asset = unreal.load_asset(path)
    if asset is None or asset.get_class() != cls:
        raise RuntimeError(f"Required asset missing or wrong class: {path}")
    return asset


def ensure_set(name, cls):
    path = f"{SET_FOLDER}/{name}.{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return require_asset(path, cls)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, SET_FOLDER, cls, factory)
    if asset is None:
        raise RuntimeError(f"Could not create {path}")
    return asset


ability_set_class = require_class("/Script/FPS.MiniAbilitySet")
pawn_data_class = require_class("/Script/FPS.MiniPawnData")
experience_class = require_class("/Script/FPS.MiniExperienceDefinition")
action_class = require_class("/Script/FPS.MiniGameFeatureAction_AddAbilities")

pawn_data = require_asset(PAWN_DATA_PATH, pawn_data_class)
experience = require_asset(EXPERIENCE_PATH, experience_class)
if experience.get_editor_property("default_pawn_data") != pawn_data:
    raise RuntimeError("Practice Experience lost its PawnData link")

pawn_set = ensure_set(PAWN_SET_NAME, ability_set_class)
feature_set = ensure_set(FEATURE_SET_NAME, ability_set_class)
if not unreal.MiniTask09AssetSetupLibrary.configure_practice_abilities(
    pawn_set, feature_set, pawn_data, experience
):
    raise RuntimeError("Task 09 asset bridge rejected existing assets")

for asset in (pawn_set, feature_set, pawn_data, experience):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")

matching = [
    action for action in experience.get_editor_property("actions")
    if action and action.get_name() == ACTION_NAME
]
if len(matching) != 1 or matching[0].get_class() != action_class:
    raise RuntimeError(f"Expected one saved {ACTION_NAME} Action")
if not unreal.MiniTask09AssetSetupLibrary.verify_practice_abilities(
    pawn_set, feature_set, pawn_data, experience
):
    raise RuntimeError("Task 09 asset links or entries failed native verification")

unreal.log(f"MINI_TASK09_PAWN_SET={pawn_set.get_path_name()}")
unreal.log(f"MINI_TASK09_FEATURE_SET={feature_set.get_path_name()}")
unreal.log(f"MINI_TASK09_ACTION={matching[0].get_path_name()}")
unreal.log("MINI_TASK09_ASSETS_CREATED")
