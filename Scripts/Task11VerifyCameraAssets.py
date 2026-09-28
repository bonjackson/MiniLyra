"""Reload the practice PawnData camera mode references in a new editor process."""

import unreal

PAWN_DATA = "/Game/Mini/System/PawnData/DA_MiniPracticePawnData.DA_MiniPracticePawnData"
data = unreal.load_asset(PAWN_DATA)
if data is None:
    raise RuntimeError("Task 11 practice PawnData was not saved")
expected = {
    "default_camera_mode": "/Script/FPS.MiniCameraMode_ThirdPerson",
    "aim_camera_mode": "/Script/FPS.MiniCameraMode_Aim",
}
for property_name, class_path in expected.items():
    actual = data.get_editor_property(property_name)
    if actual is None or actual.get_path_name() != class_path:
        raise RuntimeError(f"Task 11 {property_name} is {actual}, expected {class_path}")

unreal.log("MINI_TASK11_CAMERA_ASSETS_VERIFIED")
