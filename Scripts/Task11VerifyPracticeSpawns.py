"""Reload the practice map and check each saved spawn orientation."""

import unreal


def angle_difference(a, b):
    return abs((a - b + 180.0) % 360.0 - 180.0)


level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level_editor.load_level("/Game/Mini/Maps/L_MiniPractice"):
    raise RuntimeError("Saved practice map did not reload")
actor_editor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
starts = [
    actor for actor in actor_editor.get_all_level_actors()
    if isinstance(actor, unreal.PlayerStart)
]
if len(starts) != 4:
    raise RuntimeError(f"Saved practice map has {len(starts)} PlayerStarts, expected 4")
for start in starts:
    location = start.get_actor_location()
    expected = unreal.Rotator(
        pitch=-10.0, yaw=0.0 if location.x < 0.0 else 180.0, roll=0.0
    )
    actual = start.get_actor_rotation()
    if (angle_difference(actual.pitch, expected.pitch) > 0.01 or
            angle_difference(actual.yaw, expected.yaw) > 0.01 or
            angle_difference(actual.roll, expected.roll) > 0.01):
        raise RuntimeError(f"Bad saved spawn rotation at {location}: {actual}")

unreal.log("MINI_TASK11_PRACTICE_SPAWNS_VERIFIED")
