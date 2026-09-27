"""Drive three real Play In Editor sessions in one UnrealEditor process.

Launched by VerifyTask06PIE.ps1 with -ExecutePythonScript. The editor's
keep-alive flag is necessary because that command normally quits as soon as
the Python file returns. C++ Experience and marker logs remain the source of
truth for the PowerShell verifier.
"""

import time
import traceback

import unreal


class Task06PIEProbe:
    cycles = 3
    warmup_seconds = 3.0
    play_seconds = 10.0
    between_cycles_seconds = 2.0
    transition_timeout_seconds = 35.0

    def __init__(self):
        self.level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
        self.unreal_editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
        self.index = 1
        self.phase = "warmup"
        self.phase_started = time.monotonic()
        self.handle = None

    def finish(self, success, reason=""):
        if self.phase == "finished":
            return
        self.phase = "finished"
        if success:
            unreal.log("MINI_TASK06_PIE_SCRIPT_DONE Cycles=3")
        else:
            unreal.log_error("MINI_TASK06_PIE_SCRIPT_FAILED: " + reason)
            if self.level_editor.is_in_play_in_editor():
                self.level_editor.editor_request_end_play()
        if self.handle is not None:
            unreal.unregister_slate_post_tick_callback(self.handle)
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)

    def tick(self, delta_time):
        try:
            self._tick()
        except Exception:
            self.finish(False, traceback.format_exc())

    def _tick(self):
        now = time.monotonic()
        elapsed = now - self.phase_started

        if self.phase == "warmup":
            if elapsed < self.warmup_seconds:
                return
            world = self.unreal_editor.get_editor_world()
            world_path = world.get_path_name() if world else "None"
            unreal.log("MINI_TASK06_PIE_EDITOR_WORLD: " + world_path)
            if "/Game/Mini/Maps/L_MiniPractice" not in world_path:
                self.finish(False, "Unexpected editor map: " + world_path)
                return
            if self.level_editor.is_in_play_in_editor():
                self.finish(False, "Editor already has an active PIE session")
                return
            unreal.log("MINI_TASK06_PIE_REQUEST_BEGIN Index={}".format(self.index))
            self.level_editor.editor_request_begin_play()
            self.phase = "await_play"
            self.phase_started = now
            return

        if self.phase == "await_play":
            if self.level_editor.is_in_play_in_editor() and self.unreal_editor.get_game_world():
                unreal.log("MINI_TASK06_PIE_ACTIVE Index={}".format(self.index))
                self.phase = "playing"
                self.phase_started = now
            elif elapsed > self.transition_timeout_seconds:
                self.finish(False, "PIE did not start for cycle {}".format(self.index))
            return

        if self.phase == "playing":
            if not self.level_editor.is_in_play_in_editor():
                self.finish(False, "PIE stopped early in cycle {}".format(self.index))
                return
            if elapsed >= self.play_seconds:
                unreal.log("MINI_TASK06_PIE_REQUEST_END Index={}".format(self.index))
                self.level_editor.editor_request_end_play()
                self.phase = "await_end"
                self.phase_started = now
            return

        if self.phase == "await_end":
            if not self.level_editor.is_in_play_in_editor():
                unreal.log("MINI_TASK06_PIE_ENDED Index={}".format(self.index))
                self.phase = "between_cycles"
                self.phase_started = now
            elif elapsed > self.transition_timeout_seconds:
                self.finish(False, "PIE did not end for cycle {}".format(self.index))
            return

        if self.phase == "between_cycles" and elapsed >= self.between_cycles_seconds:
            if self.index == self.cycles:
                self.finish(True)
                return
            self.index += 1
            unreal.log("MINI_TASK06_PIE_REQUEST_BEGIN Index={}".format(self.index))
            self.level_editor.editor_request_begin_play()
            self.phase = "await_play"
            self.phase_started = now


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
_task06_pie_probe = Task06PIEProbe()
_task06_pie_probe.handle = unreal.register_slate_post_tick_callback(_task06_pie_probe.tick)
unreal.log("MINI_TASK06_PIE_SCRIPT_STARTED")
