"""Create the Task 06 GameFeatureData and two positive/negative Experiences.

Run through Task06CreateAssets.ps1 after the Task 06 marker components are built.
Re-running this script updates only the Task 06 component Actions and feature names.
"""

import unreal


PLUGIN_NAME = "MiniShooterCore"
PLUGIN_FOLDER = "/MiniShooterCore"
FEATURE_DATA_NAME = "GameFeatureData"
EXPERIENCE_FOLDER = "/Game/Mini/System/Experiences"
ACTION_SET_FOLDER = "/Game/Mini/System/ActionSets"
PAWN_DATA_FOLDER = "/Game/Mini/System/PawnData"
EXPERIENCE_NAME = "DA_MiniPracticeExperience"
ACTION_SET_NAME = "DA_MiniPracticeActionSet"
PAWN_DATA_NAME = "DA_MiniPracticePawnData"
MISSING_EXPERIENCE_NAME = "DA_MiniMissingFeatureExperience"
MISSING_PLUGIN_NAME = "MiniDefinitelyMissing"
GAME_STATE_CLASS_PATH = "/Script/FPS.MiniGameState"
FEATURE_MARKER_CLASS_PATH = "/Script/FPS.MiniFeatureMarkerComponent"
ACTION_MARKER_CLASS_PATH = "/Script/FPS.MiniExperienceActionMarkerComponent"
ADD_COMPONENTS_CLASS_PATH = "/Script/GameFeatures.GameFeatureAction_AddComponents"
FEATURE_ACTION_NAME = "MiniTask06_FeatureAddComponents"
EXPERIENCE_ACTION_NAME = "MiniTask06_ExperienceAddComponents"


def object_path(folder, name):
    return f"{folder}/{name}.{name}"


def require_class(path):
    loaded = unreal.load_class(None, path)
    if loaded is None:
        raise RuntimeError(f"Task 06 native class is unavailable: {path}; build FPSEditor first")
    return loaded


def require_asset(folder, name, expected_class):
    path = object_path(folder, name)
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError(f"Required Task 04 asset is unavailable: {path}")
    if asset.get_class().get_path_name() != expected_class.get_path_name():
        raise RuntimeError(
            f"Refusing to edit {path}: expected {expected_class.get_path_name()}, "
            f"found {asset.get_class().get_path_name()}"
        )
    return asset


def ensure_asset(folder, name, asset_class, factory):
    path = object_path(folder, name)
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        asset = unreal.load_asset(path)
        if asset is None:
            raise RuntimeError(f"Existing Task 06 asset failed to load: {path}")
        if asset.get_class().get_path_name() != asset_class.get_path_name():
            raise RuntimeError(
                f"Refusing to replace {path}: expected {asset_class.get_path_name()}, "
                f"found {asset.get_class().get_path_name()}"
            )
        return asset

    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, asset_class, factory
    )
    if asset is None:
        raise RuntimeError(f"Could not create Task 06 asset: {path}")
    return asset


def data_asset_factory(asset_class):
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    return factory


def ensure_add_components_action(owner, action_name, action_class, actor_class, component_class):
    actions = list(owner.get_editor_property("actions"))
    matching = [action for action in actions if action.get_name() == action_name]
    if len(matching) > 1:
        raise RuntimeError(f"Duplicate {action_name} Actions on {owner.get_path_name()}")
    if matching and matching[0].get_class().get_path_name() != action_class.get_path_name():
        action = matching[0]
        raise RuntimeError(
            f"Refusing to replace {action.get_path_name()}: expected "
            f"{action_class.get_path_name()}, found {action.get_class().get_path_name()}"
        )

    # FGameFeatureComponentEntry is deliberately not exposed to UE Python in
    # 5.8. This narrow editor-only C++ bridge authors that reflected struct.
    if not unreal.MiniTask06AssetSetupLibrary.ensure_add_components_action(
        owner, action_name, actor_class, component_class
    ):
        raise RuntimeError(f"Could not configure {action_name} on {owner.get_path_name()}")

    matching = [
        action for action in owner.get_editor_property("actions")
        if action.get_name() == action_name
    ]
    if len(matching) != 1:
        raise RuntimeError(f"Expected one {action_name} Action on {owner.get_path_name()}")
    action = matching[0]
    if action.get_class().get_path_name() != action_class.get_path_name():
        raise RuntimeError(
            f"Refusing to replace {action.get_path_name()}: expected "
            f"{action_class.get_path_name()}, found {action.get_class().get_path_name()}"
        )
    return action


def add_feature_name(asset, name):
    existing = list(asset.get_editor_property("game_features_to_enable"))
    # Keep unrelated future entries, but collapse any duplicates of this task's feature.
    updated = [feature for feature in existing if feature != name]
    updated.append(name)
    asset.set_editor_property("game_features_to_enable", updated)


game_feature_data_class = require_class("/Script/GameFeatures.GameFeatureData")
experience_class = require_class("/Script/FPS.MiniExperienceDefinition")
action_set_class = require_class("/Script/FPS.MiniExperienceActionSet")
pawn_data_class = require_class("/Script/FPS.MiniPawnData")
actor_class = require_class(GAME_STATE_CLASS_PATH)
feature_marker_class = require_class(FEATURE_MARKER_CLASS_PATH)
action_marker_class = require_class(ACTION_MARKER_CLASS_PATH)
add_components_class = require_class(ADD_COMPONENTS_CLASS_PATH)

# Check all pre-existing training assets before writing any of them.
experience = require_asset(EXPERIENCE_FOLDER, EXPERIENCE_NAME, experience_class)
action_set = require_asset(ACTION_SET_FOLDER, ACTION_SET_NAME, action_set_class)
pawn_data = require_asset(PAWN_DATA_FOLDER, PAWN_DATA_NAME, pawn_data_class)
if action_set not in experience.get_editor_property("action_sets"):
    raise RuntimeError("Practice Experience no longer references its Task 04 ActionSet")
if experience.get_editor_property("default_pawn_data") != pawn_data:
    raise RuntimeError("Practice Experience no longer references its Task 04 PawnData")
if len(action_set.get_editor_property("actions")) != 0:
    raise RuntimeError("Task 06 expects the practice ActionSet Actions to remain empty")

# UE 5.8 discovers this preferred path when the built-in plugin reaches Registered.
feature_data = ensure_asset(
    PLUGIN_FOLDER, FEATURE_DATA_NAME, game_feature_data_class, None
)
feature_action = ensure_add_components_action(
    feature_data, FEATURE_ACTION_NAME, add_components_class,
    actor_class, feature_marker_class
)

experience_action = ensure_add_components_action(
    experience, EXPERIENCE_ACTION_NAME, add_components_class,
    actor_class, action_marker_class
)
add_feature_name(experience, PLUGIN_NAME)
add_feature_name(action_set, PLUGIN_NAME)

# Dedicated negative case: a well-formed Experience with an undiscoverable
# required plugin. It must fail before entering Loaded on both network roles.
missing_experience = ensure_asset(
    EXPERIENCE_FOLDER, MISSING_EXPERIENCE_NAME, experience_class,
    data_asset_factory(experience_class)
)
missing_experience.set_editor_property("default_pawn_data", pawn_data)
missing_experience.set_editor_property("action_sets", [])
missing_experience.set_editor_property("actions", [])
missing_experience.set_editor_property(
    "game_features_to_enable", [MISSING_PLUGIN_NAME]
)

for asset in (feature_data, action_set, experience, missing_experience):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")

if list(feature_data.get_editor_property("actions")).count(feature_action) != 1:
    raise RuntimeError("GameFeatureData does not own exactly one Task 06 Action")
if list(experience.get_editor_property("actions")).count(experience_action) != 1:
    raise RuntimeError("Practice Experience does not own exactly one Task 06 Action")
if list(experience.get_editor_property("game_features_to_enable")).count(PLUGIN_NAME) != 1:
    raise RuntimeError("Practice Experience feature name is missing or duplicated")
if list(action_set.get_editor_property("game_features_to_enable")).count(PLUGIN_NAME) != 1:
    raise RuntimeError("Practice ActionSet feature name is missing or duplicated")
if list(missing_experience.get_editor_property("game_features_to_enable")) != [MISSING_PLUGIN_NAME]:
    raise RuntimeError("Missing-feature Experience is not configured for the negative case")

unreal.log(f"MINI_TASK06_FEATURE_DATA={feature_data.get_path_name()}")
unreal.log(f"MINI_TASK06_FEATURE_ACTION={feature_action.get_path_name()}")
unreal.log(f"MINI_TASK06_EXPERIENCE_ACTION={experience_action.get_path_name()}")
unreal.log(f"MINI_TASK06_MISSING_EXPERIENCE={missing_experience.get_path_name()}")
unreal.log("MINI_TASK06_ASSETS_CREATED")
