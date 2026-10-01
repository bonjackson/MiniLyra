"""Give the four arena starts a useful over-the-shoulder opening view."""

import math
import unreal


MAP_PATH = "/Game/Mini/Maps/L_MiniPractice"
TARGET_PITCH = -10.0


def angle_difference(a, b):
    return abs((a - b + 180.0) % 360.0 - 180.0)

def main():
    # Once Task 20 assembles production, this legacy author only checks it.
    # The current production/map author owns PawnData, defaults and rotations.
    if unreal.EditorAssetLibrary.does_asset_exist(
            "/Game/Mini/System/ActionSets/DA_MiniCombatActionSet.DA_MiniCombatActionSet"):
        from pathlib import Path
        verifier = Path(unreal.Paths.project_dir()) / "Scripts" / "Task11VerifyPracticeSpawns.py"
        exec(compile(verifier.read_text(encoding="utf-8-sig"), str(verifier), "exec"), {"__file__": str(verifier)})
        unreal.log("MINI_TASK11_PRACTICE_SPAWNS_CREATED ProductionPreserved=1")
        return

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

    assembled = unreal.EditorAssetLibrary.does_asset_exist(
        "/Game/Mini/System/ActionSets/DA_MiniCombatActionSet.DA_MiniCombatActionSet"
    )
    for start in starts:
        location = start.get_actor_location()
        yaw = math.degrees(math.atan2(550.0 - location.y, -location.x)) if assembled else (0.0 if location.x < 0.0 else 180.0)
        pitch = -3.0 if assembled else TARGET_PITCH
        desired = unreal.Rotator(pitch=pitch, yaw=yaw, roll=0.0)
        actual = start.get_actor_rotation()
        if (angle_difference(actual.pitch, pitch) > 0.01 or
                angle_difference(actual.yaw, yaw) > 0.01 or
                angle_difference(actual.roll, 0.0) > 0.01):
            start.modify()
            if not start.set_actor_rotation(desired, False):
                raise RuntimeError(f"Could not rotate {start.get_path_name()}")

    if not level_editor.save_current_level():
        raise RuntimeError(f"Could not save practice map {MAP_PATH}")
    unreal.log("MINI_TASK11_PRACTICE_SPAWNS_CREATED")


main()
