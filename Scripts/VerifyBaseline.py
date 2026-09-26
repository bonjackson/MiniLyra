"""Load an engine-owned map using Unreal's Python commandlet; never save it."""
import unreal

MAP_PATH = "/Engine/Maps/Templates/Template_Default"
level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level_editor.load_level(MAP_PATH):
    raise RuntimeError(f"Could not load baseline map: {MAP_PATH}")

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if world is None or not world.get_path_name().startswith(MAP_PATH + "."):
    raise RuntimeError(f"Unexpected editor world: {world}")

settings_class = world.get_world_settings().get_class().get_path_name()
if settings_class != "/Script/Engine.WorldSettings":
    raise RuntimeError(f"Unexpected WorldSettings class: {settings_class}")

unreal.log(f"MINI_BASELINE_MAP_LOADED={world.get_path_name()}")
unreal.log(f"MINI_BASELINE_WORLD_SETTINGS={settings_class}")
unreal.log("MINI_BASELINE_VERIFICATION_PASSED")
