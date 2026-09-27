"""Check saved Task 04 assets and map in a fresh Unreal Editor process."""

import unreal


EXPERIENCE_PACKAGE = "/Game/Mini/System/Experiences/DA_MiniPracticeExperience"
ACTION_SET_PACKAGE = "/Game/Mini/System/ActionSets/DA_MiniPracticeActionSet"
PAWN_DATA_PACKAGE = "/Game/Mini/System/PawnData/DA_MiniPracticePawnData"
MAP_PACKAGE = "/Game/Mini/Maps/L_MiniPractice"


def check_asset(package, expected_class):
    name = package.rsplit("/", 1)[1]
    object_path = f"{package}.{name}"
    asset = unreal.load_asset(object_path)
    if asset is None:
        raise RuntimeError(f"Saved Task 04 asset does not load: {object_path}")
    class_path = asset.get_class().get_path_name()
    if class_path != expected_class:
        raise RuntimeError(f"Expected a native {expected_class} instance at {object_path}, got {class_path}")
    if asset.get_path_name() != object_path:
        raise RuntimeError(f"Unexpected object path for {object_path}: {asset.get_path_name()}")
    return asset


experience = check_asset(EXPERIENCE_PACKAGE, "/Script/FPS.MiniExperienceDefinition")
action_set = check_asset(ACTION_SET_PACKAGE, "/Script/FPS.MiniExperienceActionSet")
pawn_data = check_asset(PAWN_DATA_PACKAGE, "/Script/FPS.MiniPawnData")

pawn_class = pawn_data.get_editor_property("pawn_class")
valid_pawn_classes = {
    "/Script/FPS.MiniCharacter",  # Fresh assets before Task 07 Blueprint setup.
    "/Game/Mini/Characters/BP_MiniCharacter.BP_MiniCharacter_C",
}
if pawn_class is None or pawn_class.get_path_name() not in valid_pawn_classes:
    raise RuntimeError(f"Practice PawnData has an unexpected PawnClass: {pawn_class}")
if experience.get_editor_property("default_pawn_data") != pawn_data:
    raise RuntimeError("Saved Experience does not reference the expected PawnData")
action_sets = experience.get_editor_property("action_sets")
if len(action_sets) != 1 or action_sets[0] != action_set:
    raise RuntimeError("Saved Experience must reference exactly the Task 04 ActionSet")
# Later tasks populate Actions and GameFeatures. Preserve Task 04's stable
# asset, class and map-link checks without rejecting the later configuration.

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Mini/System"], True)
found = registry.get_assets_by_path("/Game/Mini/System", True)
matching = [data for data in found if str(data.asset_name) == "DA_MiniPracticeExperience"]
if len(matching) != 1 or str(matching[0].package_name) != EXPERIENCE_PACKAGE:
    raise RuntimeError(f"Expected one scanned practice Experience package, got: {matching}")

level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level_editor.load_level(MAP_PACKAGE):
    raise RuntimeError(f"Saved practice map does not load: {MAP_PACKAGE}")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
settings = world.get_world_settings()
if settings.get_class().get_path_name() != "/Script/FPS.MiniWorldSettings":
    raise RuntimeError(f"Saved practice map has wrong WorldSettings: {settings.get_class().get_path_name()}")
reference = str(settings.get_editor_property("default_gameplay_experience"))
if EXPERIENCE_PACKAGE not in reference:
    raise RuntimeError(f"Saved map lost the practice Experience override: {reference}")

actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
labels = [actor.get_actor_label() for actor in actors]
if len([label for label in labels if label.startswith("PlayerStart_")]) != 4:
    raise RuntimeError("Practice map lost its four player starts")
if len([label for label in labels if label.startswith("Cover_")]) != 6:
    raise RuntimeError("Practice map lost its six cover pieces")

unreal.log(f"MINI_TASK04_ASSET_INSTANCES={EXPERIENCE_PACKAGE},{ACTION_SET_PACKAGE},{PAWN_DATA_PACKAGE}")
unreal.log(f"MINI_TASK04_SAVED_MAP_EXPERIENCE={reference}")
unreal.log("MINI_TASK04_ASSETS_VERIFIED")
