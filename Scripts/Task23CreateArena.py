"""Author only the independent arena map and its seven small graybox materials."""
import json
from pathlib import Path
import unreal

project = Path(unreal.Paths.project_dir())
spec = json.loads((project / "Scripts/Task23ArenaLayout.json").read_text(encoding="utf-8"))
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
material_folder = "/Game/Mini/Maps/Materials"
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
    ["/Game/Mini/Maps", "/Game/Mini/System"], force_rescan=True)

experience = unreal.load_asset(spec["experience"])
cube = unreal.load_asset(spec["cube"])
if not experience or not cube:
    raise RuntimeError("Arena Experience or Engine cube missing; complete task 22 first")
materials = {}
for key, rgb in spec["materials"].items():
    name = f"M_MiniArena_{key}"
    path = f"{material_folder}/{name}"
    material = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if material and not isinstance(material, unreal.Material):
        raise RuntimeError(f"Refusing to replace foreign asset: {path}")
    if not material:
        material = asset_tools.create_asset(name, material_folder, unreal.Material, unreal.MaterialFactoryNew())
        if material is None:
            raise RuntimeError(f"Could not create owned arena material: {path}")
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
        color = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -250, 0)
        color.set_editor_property("parameter_name", "ArenaColor")
        color.set_editor_property("default_value", unreal.LinearColor(*rgb, 1.0))
        unreal.MaterialEditingLibrary.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
        roughness = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant, -250, 150)
        roughness.set_editor_property("r", 0.85)
        unreal.MaterialEditingLibrary.connect_material_property(roughness, "", unreal.MaterialProperty.MP_ROUGHNESS)
        errors = unreal.MaterialEditingLibrary.recompile_material(material)
        if errors:
            raise RuntimeError(f"Material compile failed: {path}: {list(errors)}")
        if not unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False):
            raise RuntimeError(f"Could not save {path}")
    actual = unreal.MaterialEditingLibrary.get_material_default_vector_parameter_value(material, "ArenaColor")
    if max(abs(getattr(actual, channel) - value) for channel, value in zip(("r", "g", "b"), rgb)) > 0.001:
        raise RuntimeError(f"Existing arena color differs from the manifest: {path}")
    materials[key] = material

if unreal.EditorAssetLibrary.does_asset_exist(spec["map"]):
    if not level.load_level(spec["map"]):
        raise RuntimeError("Could not load the existing arena")
elif not level.new_level(spec["map"]):
    raise RuntimeError("Could not create the independent arena level")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
settings = world.get_world_settings()
if not isinstance(settings, unreal.MiniWorldSettings):
    raise RuntimeError("Arena must use MiniWorldSettings")
settings.set_editor_property("default_gameplay_experience", experience)
settings.set_editor_property("force_no_precomputed_lighting", True)
settings.set_editor_property("enable_world_bounds_checks", True)
settings.set_editor_property("kill_z", spec["kill_z"])

existing = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
expected = {entry["name"] for group in ("boxes", "starts", "signs") for entry in spec[group]}
expected.update(("Arena_Sun", "Arena_SkyLight", "Arena_Atmosphere"))
for name, actor in existing.items():
    if name.startswith("Arena_") and name not in expected:
        if not actors.destroy_actor(actor):
            raise RuntimeError(f"Could not remove obsolete owned arena actor {name}")

def ensure(name, cls, position, yaw=0):
    actor = existing.get(name)
    if actor and not isinstance(actor, cls):
        raise RuntimeError(f"Existing owned actor class mismatch: {name}")
    if not actor:
        actor = actors.spawn_actor_from_class(cls, unreal.Vector(*position))
        actor.set_actor_label(name)
    actor.set_actor_location(unreal.Vector(*position), False, False)
    actor.set_actor_rotation(unreal.Rotator(pitch=0, yaw=yaw, roll=0), False)
    return actor

for entry in spec["boxes"]:
    actor = ensure(entry["name"], unreal.StaticMeshActor, entry["center"])
    mesh = actor.get_component_by_class(unreal.StaticMeshComponent)
    mesh.set_static_mesh(cube)
    mesh.set_material(0, materials[entry["material"]])
    mesh.set_mobility(unreal.ComponentMobility.STATIC)
    mesh.set_collision_profile_name("BlockAll" if entry.get("collision", True) else "NoCollision")
    actor.set_actor_scale3d(unreal.Vector(*(value / 100 for value in entry["size"])))
    actor.set_editor_property("tags", ["MiniArenaGeometry", f"MiniArena{entry['kind']}"])
for entry in spec["starts"]:
    actor = ensure(entry["name"], unreal.PlayerStart, entry["position"], entry["yaw"])
    actor.set_editor_property("tags", ["MiniArenaSpawn"])
for entry in spec["signs"]:
    actor = ensure(entry["name"], unreal.TextRenderActor, entry["position"], entry["yaw"])
    text = actor.get_component_by_class(unreal.TextRenderComponent)
    text.set_text(unreal.Text(entry["text"]))
    text.set_world_size(80)
    text.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
    text.set_text_render_color(unreal.Color(*entry["color"], 255))
    actor.set_editor_property("tags", ["MiniArenaSign"])
sun = ensure("Arena_Sun", unreal.DirectionalLight, [0, 0, 900])
sun.set_actor_rotation(unreal.Rotator(pitch=-55, yaw=-35, roll=0), False)
light = sun.get_component_by_class(unreal.DirectionalLightComponent)
light.set_mobility(unreal.ComponentMobility.MOVABLE)
light.set_intensity(5)
light.set_editor_property("atmosphere_sun_light", True)
sky = ensure("Arena_SkyLight", unreal.SkyLight, [0, 0, 800])
sky_light = sky.get_component_by_class(unreal.SkyLightComponent)
sky_light.set_mobility(unreal.ComponentMobility.MOVABLE)
sky_light.set_editor_property("real_time_capture", True)
sky_light.set_intensity(1.2)
ensure("Arena_Atmosphere", unreal.SkyAtmosphere, [0, 0, 0])
if not level.save_current_level():
    raise RuntimeError("Could not save the independent arena")
unreal.log("MINI_TASK23_ARENA_CREATED Starts=8 Covers=11 Pit=1 DefaultArenaExperience=1")
