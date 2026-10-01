"""Read saved map and action data in a separate Editor process."""
import math
import unreal

MAP = '/Game/Mini/Maps/L_MiniPractice'
level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level.load_level(MAP):
    raise RuntimeError('Could not load the saved practice map')
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
labels = {actor.get_actor_label(): actor for actor in actors}
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if not world.get_world_settings().get_editor_property('force_no_precomputed_lighting'):
    raise RuntimeError('Practice map still expects precomputed lighting')
if any(name.startswith('Target_') and name.endswith(('_Board', '_Bullseye')) for name in labels):
    raise RuntimeError('An inert board can still block a runtime target')
if any(isinstance(actor, (unreal.MiniPracticeTarget, unreal.MiniPracticeSupply)) for actor in actors):
    raise RuntimeError('Training actors must be created by the Experience, not duplicated in the map')
if len([name for name in labels if name.startswith('Cover_')]) != 6:
    raise RuntimeError('Expected six cover pieces')
if len([name for name in labels if name.endswith('_Stand')]) != 3:
    raise RuntimeError('Expected three retained target stands')
starts = [actor for actor in actors if isinstance(actor, unreal.PlayerStart)]
if len(starts) != 4:
    raise RuntimeError('Expected exactly four player starts')
for start in starts:
    pos = start.get_actor_location()
    rotation = start.get_actor_rotation()
    desired = math.degrees(math.atan2(550 - pos.y, -pos.x))
    delta = (rotation.yaw - desired + 180) % 360 - 180
    if abs(delta) > 0.1 or abs(rotation.pitch + 3.0) > 0.1:
        raise RuntimeError(f'{start.get_actor_label()} does not face the practice lane')
sun = labels['Practice_Sun'].get_component_by_class(unreal.DirectionalLightComponent)
sky = labels['Practice_SkyLight'].get_component_by_class(unreal.SkyLightComponent)
if sun.get_editor_property('mobility') != unreal.ComponentMobility.MOVABLE or not sun.get_editor_property('atmosphere_sun_light'):
    raise RuntimeError('The sun is not a dynamic atmosphere light')
if sky.get_editor_property('mobility') != unreal.ComponentMobility.MOVABLE or not sky.get_editor_property('real_time_capture'):
    raise RuntimeError('Sky capture is not dynamic')
if not isinstance(labels['Practice_Atmosphere'], unreal.SkyAtmosphere):
    raise RuntimeError('Missing atmosphere')
practice_set = unreal.load_asset('/Game/Mini/System/ActionSets/DA_MiniPracticeActionSet')
if not practice_set:
    raise RuntimeError('Missing practice ActionSet')
actions = [action for action in practice_set.get_editor_property('actions')
           if action and action.get_class().get_path_name() == '/Script/FPS.MiniGameFeatureAction_AddActors']
if len(actions) != 1 or len(actions[0].get_editor_property('actors')) != 4:
    raise RuntimeError('Practice ActionSet must own the three targets and one supply')
unreal.log('MINI_TASK20_PRACTICE_MAP_VERIFIED SavedMap=1 Starts=4 Covers=6 StaticBoards=0 RuntimeActorEntries=4 DynamicLights=2')
