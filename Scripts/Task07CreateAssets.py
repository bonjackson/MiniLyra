"""Create the Task 07 character Blueprint and update its PawnData and feature Action.

Run through Task07CreateAssets.ps1 after building FPSEditor. Re-running preserves
the earlier Experience and GameFeature Actions while repairing Task 07 links.
"""

import unreal


CHARACTER_FOLDER = "/Game/Mini/Characters"
CHARACTER_NAME = "BP_MiniCharacter"
CHARACTER_NATIVE_CLASS_PATH = "/Script/FPS.MiniCharacter"
CHARACTER_MESH_PATH = (
    "/Game/Mini/Characters/Mannequins/Meshes/"
    "SKM_Manny_Simple.SKM_Manny_Simple"
)
PAWN_DATA_PATH = (
    "/Game/Mini/Diagnostics/PawnData/"
    "DA_MiniDiagnosticsPawnData.DA_MiniDiagnosticsPawnData"
)
EXPERIENCE_PATH = (
    "/Game/Mini/Diagnostics/Experiences/"
    "DA_MiniDiagnosticsExperience.DA_MiniDiagnosticsExperience"
)
GAME_FEATURE_DATA_PATH = "/Game/Mini/Diagnostics/Experiences/DA_MiniDiagnosticsExperience.DA_MiniDiagnosticsExperience"
MARKER_CLASS_PATH = "/Script/FPS.MiniCharacterFeatureMarkerComponent"
ADD_COMPONENTS_CLASS_PATH = "/Script/GameFeatures.GameFeatureAction_AddComponents"
CHARACTER_FEATURE_ACTION_NAME = "MiniTask07_CharacterAddComponents"
production_assembled = unreal.EditorAssetLibrary.does_asset_exist(
    "/Game/Mini/System/ActionSets/DA_MiniCombatActionSet.DA_MiniCombatActionSet"
)


def require_class(path):
    loaded = unreal.load_class(None, path)
    if loaded is None:
        raise RuntimeError(f"Required class is unavailable: {path}; build FPSEditor first")
    return loaded


def require_asset(path, expected_class):
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError(f"Required asset is unavailable: {path}")
    if asset.get_class().get_path_name() != expected_class.get_path_name():
        raise RuntimeError(
            f"Refusing to edit {path}: expected {expected_class.get_path_name()}, "
            f"found {asset.get_class().get_path_name()}"
        )
    return asset


def ensure_character_blueprint(native_character_class):
    asset_path = f"{CHARACTER_FOLDER}/{CHARACTER_NAME}.{CHARACTER_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        blueprint = require_asset(asset_path, unreal.Blueprint.static_class())
    else:
        if production_assembled:
            raise RuntimeError("The shared production character is missing; repair it through production authoring")
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", native_character_class)
        blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            CHARACTER_NAME, CHARACTER_FOLDER, unreal.Blueprint.static_class(), factory
        )
        if blueprint is None:
            raise RuntimeError(f"Could not create {asset_path}")

    if blueprint.get_blueprint_parent_class() != native_character_class:
        raise RuntimeError(
            f"Refusing to reparent {asset_path}: expected {native_character_class.get_path_name()}, "
            f"found {blueprint.get_blueprint_parent_class().get_path_name()}"
        )
    if not production_assembled and not unreal.BlueprintEditorLibrary.compile_blueprint(blueprint):
        raise RuntimeError(f"Could not compile {asset_path}")
    if production_assembled and "UP_TO_DATE" not in str(blueprint.get_editor_property("status")):
        raise RuntimeError("The shared production character must be compiled by its current author")
    generated_class = blueprint.generated_class()
    if generated_class is None:
        raise RuntimeError(f"No generated class for {asset_path}")
    return blueprint, generated_class


native_character_class = require_class(CHARACTER_NATIVE_CLASS_PATH)
marker_class = require_class(MARKER_CLASS_PATH)
add_components_class = require_class(ADD_COMPONENTS_CLASS_PATH)
pawn_data_class = require_class("/Script/FPS.MiniPawnData")
experience_class = require_class("/Script/FPS.MiniExperienceDefinition")
feature_data_class = require_class("/Script/FPS.MiniExperienceDefinition")

# Validate the existing links before making any asset changes.
pawn_data = require_asset(PAWN_DATA_PATH, pawn_data_class)
experience = require_asset(EXPERIENCE_PATH, experience_class)
feature_data = require_asset(GAME_FEATURE_DATA_PATH, feature_data_class)
if experience.get_editor_property("default_pawn_data") != pawn_data:
    raise RuntimeError("Practice Experience no longer references its PawnData")

mesh_asset = unreal.load_asset(CHARACTER_MESH_PATH)
if mesh_asset is None or mesh_asset.get_class().get_path_name() != "/Script/Engine.SkeletalMesh":
    raise RuntimeError(f"The copied Manny Simple mesh is missing or invalid: {CHARACTER_MESH_PATH}")

blueprint, blueprint_class = ensure_character_blueprint(native_character_class)
character_default = unreal.get_default_object(blueprint_class)
mesh_component = character_default.get_component_by_class(unreal.SkeletalMeshComponent)
if mesh_component is None:
    raise RuntimeError(f"{blueprint.get_path_name()} has no inherited skeletal mesh component")

# The character Blueprint is shared with production. After Task 20 assembly,
# a legacy rerun may repair diagnostic links but must preserve its appearance.
if not production_assembled:
    blueprint.modify()
    mesh_component.modify()
    mesh_component.set_editor_property("skeletal_mesh_asset", mesh_asset)
    mesh_component.set_editor_property("relative_location", unreal.Vector(0.0, 0.0, -90.0))
    mesh_component.set_editor_property(
        "relative_rotation", unreal.Rotator(pitch=0.0, yaw=-90.0, roll=0.0)
    )

pawn_data.set_editor_property("pawn_class", blueprint_class)

existing_actions = list(feature_data.get_editor_property("actions"))
matching_actions = [
    action for action in existing_actions
    if action.get_name() == CHARACTER_FEATURE_ACTION_NAME
]
if len(matching_actions) > 1:
    raise RuntimeError(f"Duplicate {CHARACTER_FEATURE_ACTION_NAME} Actions")
if matching_actions and matching_actions[0].get_class() != add_components_class:
    raise RuntimeError(
        f"Refusing to replace an unrelated Action named {CHARACTER_FEATURE_ACTION_NAME}"
    )

# UE 5.8 Python cannot write FGameFeatureComponentEntry directly. The existing
# editor-only bridge updates exactly this named Action without touching Task 06.
if not unreal.MiniTask06AssetSetupLibrary.ensure_add_components_action(
    feature_data, CHARACTER_FEATURE_ACTION_NAME, native_character_class, marker_class
):
    raise RuntimeError(f"Could not configure {CHARACTER_FEATURE_ACTION_NAME}")

assets_to_save = (pawn_data, feature_data) if production_assembled else (blueprint, pawn_data, feature_data)
for asset in assets_to_save:
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")

saved_actions = list(feature_data.get_editor_property("actions"))
if sum(action.get_name() == CHARACTER_FEATURE_ACTION_NAME for action in saved_actions) != 1:
    raise RuntimeError("Task 07 character component Action is missing or duplicated")
if pawn_data.get_editor_property("pawn_class") != blueprint_class:
    raise RuntimeError("PawnData did not retain BP_MiniCharacter")
if mesh_component.get_editor_property("skeletal_mesh_asset") != mesh_asset:
    raise RuntimeError("BP_MiniCharacter did not retain the mannequin mesh")

unreal.log(f"MINI_TASK07_CHARACTER={blueprint.get_path_name()}")
unreal.log(f"MINI_TASK07_PAWN_CLASS={blueprint_class.get_path_name()}")
unreal.log(f"MINI_TASK07_PAWN_DATA={pawn_data.get_path_name()}")
unreal.log(f"MINI_TASK07_FEATURE_ACTION={CHARACTER_FEATURE_ACTION_NAME}")
unreal.log("MINI_TASK07_ASSETS_CREATED")
