"""Read a separately saved map; never compile, author, or save assets."""
import json
from pathlib import Path
import unreal

project = Path(unreal.Paths.project_dir())
spec = json.loads((project / "Scripts/Task23ArenaLayout.json").read_text(encoding="utf-8"))
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
    ["/Game/Mini/Maps", "/Game/Mini/System", "/MiniArena"], force_rescan=True)
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(spec["map"]):
    raise RuntimeError("Saved arena missing")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
settings = world.get_world_settings()
if not isinstance(settings, unreal.MiniWorldSettings):
    raise RuntimeError("Arena has the wrong WorldSettings class")
if "DA_MiniArenaExperience" not in str(settings.get_editor_property("default_gameplay_experience")):
    raise RuntimeError("Arena map must select production Arena through its map override")
if not settings.get_editor_property("enable_world_bounds_checks") or abs(settings.get_editor_property("kill_z") - spec["kill_z"]) > 0.01:
    raise RuntimeError("Saved KillZ/bounds checks differ")
if not settings.get_editor_property("force_no_precomputed_lighting"):
    raise RuntimeError("Arena still requires a light bake")
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
labels = {a.get_actor_label(): a for a in actors}
expected = {e["name"] for group in ("boxes", "starts", "signs") for e in spec[group]}
expected.update(("Arena_Sun", "Arena_SkyLight", "Arena_Atmosphere"))
if {name for name in labels if name.startswith("Arena_")} != expected:
    raise RuntimeError("Saved arena actor inventory differs from its manifest")
if len([a for a in actors if isinstance(a, unreal.PlayerStart)]) != 8:
    raise RuntimeError("Arena requires exactly eight starts")
if any(isinstance(a, (unreal.MiniPracticeTarget, unreal.MiniPracticeSupply)) for a in actors):
    raise RuntimeError("Training actors leaked into arena geometry")

def same_vector(actual, values):
    return max(abs(getattr(actual, channel) - value) for channel, value in zip(("x", "y", "z"), values)) < 0.1

for entry in spec["boxes"]:
    actor = labels[entry["name"]]
    if not isinstance(actor, unreal.StaticMeshActor) or not same_vector(actor.get_actor_location(), entry["center"]):
        raise RuntimeError(f"Saved shape mismatch: {entry['name']}")
    if not same_vector(actor.get_actor_scale3d(), [v / 100 for v in entry["size"]]):
        raise RuntimeError(f"Saved dimensions mismatch: {entry['name']}")
    mesh = actor.get_component_by_class(unreal.StaticMeshComponent)
    if mesh.get_editor_property("static_mesh").get_path_name() != spec["cube"]:
        raise RuntimeError(f"Unexpected mesh: {entry['name']}")
    expected_material = f"/Game/Mini/Maps/Materials/M_MiniArena_{entry['material']}.M_MiniArena_{entry['material']}"
    material = mesh.get_material(0)
    if not material or material.get_path_name() != expected_material:
        raise RuntimeError(f"Saved material mismatch: {entry['name']}")
    profile = "BlockAll" if entry.get("collision", True) else "NoCollision"
    if str(mesh.get_collision_profile_name()) != profile or mesh.get_editor_property("mobility") != unreal.ComponentMobility.STATIC:
        raise RuntimeError(f"Saved collision/mobility mismatch: {entry['name']}")
    if f"MiniArena{entry['kind']}" not in [str(t) for t in actor.get_editor_property("tags")]:
        raise RuntimeError(f"Missing runtime geometry tag: {entry['name']}")
for entry in spec["starts"]:
    actor = labels[entry["name"]]
    if not same_vector(actor.get_actor_location(), entry["position"]) or abs(actor.get_actor_rotation().yaw - entry["yaw"]) > 0.01:
        raise RuntimeError(f"Saved start mismatch: {entry['name']}")
for entry in spec["signs"]:
    text = labels[entry["name"]].get_component_by_class(unreal.TextRenderComponent)
    if str(text.get_editor_property("text")) != entry["text"]:
        raise RuntimeError(f"Missing zone label: {entry['name']}")
for key, rgb in spec["materials"].items():
    material = unreal.load_asset(f"/Game/Mini/Maps/Materials/M_MiniArena_{key}")
    actual = unreal.MaterialEditingLibrary.get_material_default_vector_parameter_value(material, "ArenaColor")
    if max(abs(getattr(actual, c) - v) for c, v in zip(("r", "g", "b"), rgb)) > 0.001:
        raise RuntimeError(f"Saved region color mismatch: {key}")
sun = labels["Arena_Sun"].get_component_by_class(unreal.DirectionalLightComponent)
sky = labels["Arena_SkyLight"].get_component_by_class(unreal.SkyLightComponent)
if sun.get_editor_property("mobility") != unreal.ComponentMobility.MOVABLE or not sky.get_editor_property("real_time_capture"):
    raise RuntimeError("Arena must be dynamically lit")
if not unreal.MiniTask22AssetSetupLibrary.verify_saved_assembly(False):
    raise RuntimeError("Production Core+Arena assembly or shared PawnData changed")
practice_path = "/Game/Mini/Maps/L_MiniPractice"
if not level.load_level(practice_path):
    raise RuntimeError("Practice map missing")
practice_settings = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_world_settings()
if "DA_MiniPracticeExperience" not in str(practice_settings.get_editor_property("default_gameplay_experience")):
    raise RuntimeError("Practice map override changed")
game = (project / "Config/DefaultGame.ini").read_text(encoding="utf-8-sig")
engine = (project / "Config/DefaultEngine.ini").read_text(encoding="utf-8-sig")
if "DefaultExperienceId=MiniExperienceDefinition:DA_MiniPracticeExperience" not in game:
    raise RuntimeError("Project fallback must remain explicit Practice until FrontEnd")
if "GameDefaultMap=/Game/Mini/Maps/L_MiniPractice" not in engine:
    raise RuntimeError("Task23 does not replace the program entry before FrontEnd")
for path in (practice_path, spec["map"]):
    if f'+MapsToCook=(FilePath="{path}")' not in game:
        raise RuntimeError(f"Gameplay map absent from packaging list: {path}")
unreal.log("MINI_TASK23_ARENA_VERIFIED Starts=8 Covers=11 Pit=1 MapOverrides=2 CoreArenaAssembly=1 FallbackPractice=1 CookMaps=2")
