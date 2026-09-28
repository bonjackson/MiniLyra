"""Set the two native camera modes on the practice PawnData."""

import unreal

PAWN_DATA = "/Game/Mini/System/PawnData/DA_MiniPracticePawnData.DA_MiniPracticePawnData"
NORMAL_CLASS = "/Script/FPS.MiniCameraMode_ThirdPerson"
AIM_CLASS = "/Script/FPS.MiniCameraMode_Aim"

pawn_data = unreal.load_asset(PAWN_DATA)
normal = unreal.load_class(None, NORMAL_CLASS)
aim = unreal.load_class(None, AIM_CLASS)
if pawn_data is None or normal is None or aim is None:
    raise RuntimeError("Task 11 camera modes or practice PawnData are unavailable")

pawn_data.set_editor_property("default_camera_mode", normal)
pawn_data.set_editor_property("aim_camera_mode", aim)
if not unreal.EditorAssetLibrary.save_loaded_asset(pawn_data, only_if_is_dirty=False):
    raise RuntimeError("Could not save Task 11 PawnData camera mode references")
if pawn_data.get_editor_property("default_camera_mode") != normal or \
        pawn_data.get_editor_property("aim_camera_mode") != aim:
    raise RuntimeError("PawnData camera mode references did not persist in memory")

unreal.log("MINI_TASK11_CAMERA_ASSETS_CREATED")
