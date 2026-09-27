"""Create the three Task 04 data assets and configure the practice map.

Run through Task04CreateAssets.ps1 after the Task 04 native classes are built.
The script only creates assets at the paths below and updates L_MiniPractice.
Re-running it validates existing assets and repairs their Task 04 links.
"""

import unreal


EXPERIENCE_FOLDER = "/Game/Mini/System/Experiences"
ACTION_SET_FOLDER = "/Game/Mini/System/ActionSets"
PAWN_DATA_FOLDER = "/Game/Mini/System/PawnData"
EXPERIENCE_NAME = "DA_MiniPracticeExperience"
ACTION_SET_NAME = "DA_MiniPracticeActionSet"
PAWN_DATA_NAME = "DA_MiniPracticePawnData"
MAP_PATH = "/Game/Mini/Maps/L_MiniPractice"


def object_path(folder, name):
    return f"{folder}/{name}.{name}"


def native_class(name):
    path = f"/Script/FPS.{name}"
    asset_class = unreal.load_class(None, path)
    if asset_class is None:
        raise RuntimeError(f"Task 04 native class is unavailable: {path}; build FPSEditor first")
    return asset_class


def ensure_asset(folder, name, asset_class):
    path = object_path(folder, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        asset = unreal.load_asset(path)
        if asset is None:
            raise RuntimeError(f"Existing Task 04 asset failed to load: {path}")
        if asset.get_class().get_path_name() != asset_class.get_path_name():
            raise RuntimeError(
                f"Refusing to replace {path}: expected {asset_class.get_path_name()}, "
                f"found {asset.get_class().get_path_name()}"
            )
        return asset

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, asset_class, factory
    )
    if asset is None:
        raise RuntimeError(f"Could not create Task 04 asset: {path}")
    return asset


experience_class = native_class("MiniExperienceDefinition")
action_set_class = native_class("MiniExperienceActionSet")
pawn_data_class = native_class("MiniPawnData")
world_settings_class = native_class("MiniWorldSettings")

# Load the existing map before writing assets. UWorld::RepairWorldSettings
# upgrades a saved Engine.WorldSettings when WorldSettingsClassName points to
# MiniWorldSettings. This preserves the existing practice geometry and actors.
level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level_editor.load_level(MAP_PATH):
    raise RuntimeError(f"Could not load the existing practice map: {MAP_PATH}")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
settings = world.get_world_settings()
if settings.get_class().get_path_name() != world_settings_class.get_path_name():
    raise RuntimeError(
        "Practice map WorldSettings did not upgrade to MiniWorldSettings. "
        "Check DefaultEngine.ini WorldSettingsClassName before running this script: "
        f"{settings.get_class().get_path_name()}"
    )

action_set = ensure_asset(ACTION_SET_FOLDER, ACTION_SET_NAME, action_set_class)
pawn_data = ensure_asset(PAWN_DATA_FOLDER, PAWN_DATA_NAME, pawn_data_class)
experience = ensure_asset(EXPERIENCE_FOLDER, EXPERIENCE_NAME, experience_class)

# This is a temporary native Pawn placeholder. Task 07 replaces it with the
# playable MiniCharacter class; the PawnData itself remains the stable link.
pawn_data.set_editor_property("pawn_class", unreal.Pawn.static_class())
experience.set_editor_property("default_pawn_data", pawn_data)
experience.set_editor_property("action_sets", [action_set])
settings.set_editor_property("default_gameplay_experience", experience)

for asset in (action_set, pawn_data, experience):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")
if not level_editor.save_current_level():
    raise RuntimeError(f"Could not save {MAP_PATH}")

if len(action_set.get_editor_property("actions")) != 0:
    raise RuntimeError("The Task 04 ActionSet must remain empty")
if experience.get_editor_property("default_pawn_data") != pawn_data:
    raise RuntimeError("Experience does not point to the Task 04 PawnData")
linked_sets = experience.get_editor_property("action_sets")
if len(linked_sets) != 1 or linked_sets[0] != action_set:
    raise RuntimeError("Experience does not point to exactly one Task 04 ActionSet")
if EXPERIENCE_NAME not in str(settings.get_editor_property("default_gameplay_experience")):
    raise RuntimeError("Practice map does not point to the Task 04 Experience")

unreal.log(f"MINI_TASK04_EXPERIENCE={experience.get_path_name()}")
unreal.log(f"MINI_TASK04_ACTION_SET={action_set.get_path_name()}")
unreal.log(f"MINI_TASK04_PAWN_DATA={pawn_data.get_path_name()}")
unreal.log(f"MINI_TASK04_WORLD_SETTINGS={settings.get_class().get_path_name()}")
unreal.log("MINI_TASK04_ASSETS_CREATED")
