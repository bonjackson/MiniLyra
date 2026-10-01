"""Replace inert boards with Experience-owned targets; save a dynamically lit map."""
import math
import unreal

MAP = '/Game/Mini/Maps/L_MiniPractice'
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
if not level.load_level(MAP):
    raise RuntimeError('Practice map did not load')
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('force_no_precomputed_lighting', True)

for actor in actors.get_all_level_actors():
    label = actor.get_actor_label()
    if label.startswith('Target_') and label.endswith(('_Board', '_Bullseye')):
        if not actors.destroy_actor(actor):
            raise RuntimeError(f'Could not remove obsolete blocking placeholder: {label}')

by_label = {actor.get_actor_label(): actor for actor in actors.get_all_level_actors()}
for name in ('PlayerStart_SW', 'PlayerStart_SE', 'PlayerStart_NW', 'PlayerStart_NE'):
    start = by_label.get(name)
    if not start or not isinstance(start, unreal.PlayerStart):
        raise RuntimeError(f'Missing player start: {name}')
    pos = start.get_actor_location()
    yaw = math.degrees(math.atan2(550.0 - pos.y, -pos.x))
    start.set_actor_rotation(unreal.Rotator(pitch=-3.0, yaw=yaw, roll=0.0), False)

sun = by_label.get('Practice_Sun')
if not isinstance(sun, unreal.DirectionalLight):
    raise RuntimeError('Practice_Sun must remain the existing directional light')
sun.set_actor_rotation(unreal.Rotator(pitch=-45.0, yaw=-35.0, roll=0.0), False)
light = sun.get_component_by_class(unreal.DirectionalLightComponent)
light.set_mobility(unreal.ComponentMobility.MOVABLE)
light.set_intensity(5.0)
light.set_editor_property('atmosphere_sun_light', True)

def ensure_actor(label, cls):
    actor = by_label.get(label)
    if actor and not isinstance(actor, cls):
        raise RuntimeError(f'{label} has the wrong class')
    if actor is None:
        actor = actors.spawn_actor_from_class(cls, unreal.Vector(0, 0, 900))
        actor.set_actor_label(label)
    return actor

ensure_actor('Practice_Atmosphere', unreal.SkyAtmosphere)
sky = ensure_actor('Practice_SkyLight', unreal.SkyLight)
sky_light = sky.get_component_by_class(unreal.SkyLightComponent)
sky_light.set_mobility(unreal.ComponentMobility.MOVABLE)
sky_light.set_editor_property('real_time_capture', True)
sky_light.set_intensity(1.2)

if not level.save_current_level():
    raise RuntimeError('Could not save the practice map')
unreal.log('MINI_TASK20_PRACTICE_MAP_CREATED TargetsOwnedByExperience=3 SupplyOwnedByExperience=1 DynamicLights=2 Starts=4')
