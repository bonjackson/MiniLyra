"""Idempotently create the Task 10 Enhanced Input assets and practice links."""

import unreal


INPUT_FOLDER = "/Game/Mini/Diagnostics/Input"
PAWN_DATA_PATH = "/Game/Mini/Diagnostics/PawnData/DA_MiniDiagnosticsPawnData.DA_MiniDiagnosticsPawnData"
PAWN_SET_PATH = "/Game/Mini/Diagnostics/AbilitySets/DA_MiniPawnAbilitySet.DA_MiniPawnAbilitySet"
EXPERIENCE_PATH = "/Game/Mini/Diagnostics/Experiences/DA_MiniDiagnosticsExperience.DA_MiniDiagnosticsExperience"
ACTION_NAMES = (
    "Move",
    "Look",
    "Jump",
    "Fire",
    "Reload",
    "SwitchWeapon",
    "Aim",
)


def require_class(path):
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError(f"Required Task 10 class is unavailable: {path}")
    return cls


def require_asset(path, cls):
    asset = unreal.load_asset(path)
    if asset is None or asset.get_class() != cls:
        raise RuntimeError(f"Required asset missing or wrong class: {path}")
    return asset


def ensure_data_asset(name, cls):
    path = f"{INPUT_FOLDER}/{name}.{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return require_asset(path, cls)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, INPUT_FOLDER, cls, factory
    )
    if asset is None:
        raise RuntimeError(f"Could not create {path}")
    return asset


input_config_class = require_class("/Script/FPS.MiniInputConfig")
pawn_set_class = require_class("/Script/FPS.MiniAbilitySet")
pawn_data_class = require_class("/Script/FPS.MiniPawnData")
experience_class = require_class("/Script/FPS.MiniExperienceDefinition")
input_action_class = require_class("/Script/EnhancedInput.InputAction")
mapping_context_class = require_class("/Script/EnhancedInput.InputMappingContext")
require_class("/Script/FPS.MiniGameFeatureAction_AddInput")

pawn_data = require_asset(PAWN_DATA_PATH, pawn_data_class)
pawn_set = require_asset(PAWN_SET_PATH, pawn_set_class)
experience = require_asset(EXPERIENCE_PATH, experience_class)
if experience.get_editor_property("default_pawn_data") != pawn_data:
    raise RuntimeError("Practice Experience lost its PawnData link")

input_config = ensure_data_asset("DA_MiniInputConfig", input_config_class)
mapping_context = ensure_data_asset("IMC_MiniDefault", mapping_context_class)
actions = tuple(
    ensure_data_asset(f"IA_Mini{name}", input_action_class) for name in ACTION_NAMES
)

args = (input_config, mapping_context, pawn_set, *actions, pawn_data, experience)
if not unreal.MiniTask10AssetSetupLibrary.configure_practice_input(*args):
    raise RuntimeError("Task 10 asset bridge rejected existing assets")

for asset in (input_config, mapping_context, pawn_set, *actions, pawn_data, experience):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")

if not unreal.MiniTask10AssetSetupLibrary.verify_practice_input(*args):
    raise RuntimeError("Task 10 asset links or mappings failed native verification")

unreal.log(f"MINI_TASK10_INPUT_CONFIG={input_config.get_path_name()}")
unreal.log(f"MINI_TASK10_MAPPING_CONTEXT={mapping_context.get_path_name()}")
for name, action in zip(ACTION_NAMES, actions):
    unreal.log(f"MINI_TASK10_INPUT_ACTION_{name.upper()}={action.get_path_name()}")
unreal.log("MINI_TASK10_ASSETS_CREATED")
