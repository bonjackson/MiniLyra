"""Read independently saved FrontEnd and gameplay maps, without repair/save."""
from pathlib import Path
import unreal

project = Path(unreal.Paths.project_dir())
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Mini/System", "/Game/Mini/Maps", "/MiniArena"], force_rescan=True)
experience = unreal.load_asset("/Game/Mini/System/Experiences/DA_MiniFrontEndExperience.DA_MiniFrontEndExperience")
if not unreal.MiniTask24AssetSetupLibrary.verify_front_end_experience(experience):
    raise RuntimeError("Saved FrontEnd data must contain one native Menu Action and no gameplay data")
if not unreal.MiniTask22AssetSetupLibrary.verify_saved_assembly(False):
    raise RuntimeError("Production arena assembly changed")
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for suffix in ("FrontEnd", "Practice", "Arena"):
    map_path = f"/Game/Mini/Maps/L_Mini{suffix}"
    if not level.load_level(map_path):
        raise RuntimeError(f"Saved map missing: {map_path}")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    settings = world.get_world_settings()
    expected = f"/Game/Mini/System/Experiences/DA_Mini{suffix}Experience.DA_Mini{suffix}Experience"
    if not isinstance(settings, unreal.MiniWorldSettings) or str(settings.get_editor_property("default_gameplay_experience").get_path_name()) != expected:
        raise RuntimeError(f"Wrong Experience map override: {suffix}")
    if suffix == "FrontEnd":
        inventory = actors.get_all_level_actors()
        if len([a for a in inventory if isinstance(a, unreal.PlayerStart)]) != 1:
            raise RuntimeError("FrontEnd requires one login start")
        if any(isinstance(a, (unreal.MiniCharacter, unreal.MiniPracticeTarget, unreal.MiniPracticeSupply)) for a in inventory):
            raise RuntimeError("Gameplay actor leaked into FrontEnd")

game = (project / "Config/DefaultGame.ini").read_text(encoding="utf-8-sig")
engine = (project / "Config/DefaultEngine.ini").read_text(encoding="utf-8-sig")
for setting in ("GameDefaultMap", "EditorStartupMap"):
    if f"{setting}=/Game/Mini/Maps/L_MiniFrontEnd" not in engine:
        raise RuntimeError(f"Wrong default {setting}")
if "DefaultExperienceId=MiniExperienceDefinition:DA_MiniFrontEndExperience" not in game:
    raise RuntimeError("Wrong project Experience fallback")
for suffix in ("FrontEnd", "Practice", "Arena"):
    if f'+MapsToCook=(FilePath="/Game/Mini/Maps/L_Mini{suffix}")' not in game:
        raise RuntimeError(f"Missing map cook entry: {suffix}")
unreal.log("MINI_TASK24_ASSETS_VERIFIED FrontEndExperience=1 MapOverrides=3 CombatPawn=0 CookMaps=3")
