"""Give the four arena starts a useful over-the-shoulder opening view."""

import unreal


MAP_PATH = "/Game/Mini/Maps/L_MiniPractice"
TARGET_PITCH = -10.0


def angle_difference(a, b):
    return abs((a - b + 180.0) % 360.0 - 180.0)

level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level_editor.load_level(MAP_PATH):
    raise RuntimeError(f"Could not load practice map {MAP_PATH}")
actor_editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
starts = [
    actor for actor in actor_editor.get_all_level_actors()
    if isinstance(actor, unreal.PlayerStart)
]
if len(starts) != 4:
    raise RuntimeError(f"Expected four practice PlayerStarts, got {len(starts)}")

for start in starts:
    location = start.get_actor_location()
    yaw = 0.0 if location.x < 0.0 else 180.0
    desired = unreal.Rotator(pitch=TARGET_PITCH, yaw=yaw, roll=0.0)
    actual = start.get_actor_rotation()
    if (angle_difference(actual.pitch, TARGET_PITCH) > 0.01 or
            angle_difference(actual.yaw, yaw) > 0.01 or
            angle_difference(actual.roll, 0.0) > 0.01):
        start.modify()
        if not start.set_actor_rotation(desired, False):
            raise RuntimeError(f"Could not rotate {start.get_path_name()}")

if not level_editor.save_current_level():
    raise RuntimeError(f"Could not save practice map {MAP_PATH}")
unreal.log("MINI_TASK11_PRACTICE_SPAWNS_CREATED")
