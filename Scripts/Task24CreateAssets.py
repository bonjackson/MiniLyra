"""Author only the fixed FrontEnd Experience and independent menu map."""
import unreal

folder = "/Game/Mini/System/Experiences"
name = "DA_MiniFrontEndExperience"
path = f"{folder}/{name}.{name}"
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Mini/System", "/Game/Mini/Maps"], force_rescan=True)
experience = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
if experience is not None and experience.get_class().get_path_name() != "/Script/FPS.MiniExperienceDefinition":
    raise RuntimeError("Refusing to replace a foreign FrontEnd asset")
if experience is None:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.MiniExperienceDefinition)
    experience = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.MiniExperienceDefinition, factory)
if experience is None or not unreal.MiniTask24AssetSetupLibrary.ensure_front_end_experience(experience):
    raise RuntimeError("Could not author the bounded FrontEnd UI Experience")
if not unreal.EditorAssetLibrary.save_loaded_asset(experience, only_if_is_dirty=True):
    raise RuntimeError("Could not save FrontEnd Experience")

map_path = "/Game/Mini/Maps/L_MiniFrontEnd"
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if unreal.EditorAssetLibrary.does_asset_exist(map_path):
    if not level.load_level(map_path):
        raise RuntimeError("Could not load FrontEnd map")
elif not level.new_level(map_path):
    raise RuntimeError("Could not create independent FrontEnd map")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
settings = world.get_world_settings()
if not isinstance(settings, unreal.MiniWorldSettings):
    raise RuntimeError("FrontEnd requires MiniWorldSettings")
settings.set_editor_property("default_gameplay_experience", experience)
settings.set_editor_property("force_no_precomputed_lighting", True)
actor_system = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = {a.get_actor_label(): a for a in actor_system.get_all_level_actors()}
for label, cls, position in (
    ("FrontEnd_PlayerStart", unreal.PlayerStart, (0, 0, 100)),
    ("FrontEnd_Camera", unreal.CameraActor, (0, 0, 200)),
):
    actor = actors.get(label)
    if actor is not None and not isinstance(actor, cls):
        raise RuntimeError(f"Owned FrontEnd actor has wrong class: {label}")
    if actor is None:
        actor = actor_system.spawn_actor_from_class(cls, unreal.Vector(*position))
        actor.set_actor_label(label)
    actor.set_actor_location(unreal.Vector(*position), False, False)
if not level.save_current_level():
    raise RuntimeError("Could not save FrontEnd map")
unreal.log("MINI_TASK24_ASSETS_CREATED FrontEndExperience=1 UIAction=1 CombatPawn=0")
