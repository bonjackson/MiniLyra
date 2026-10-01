"""Reload and inspect the saved Task 10 assets in a fresh Unreal Editor process."""

import unreal


INPUT_FOLDER = "/Game/Mini/Diagnostics/Input"
ACTION_NAMES = (
    "Move",
    "Look",
    "Jump",
    "Fire",
    "Reload",
    "SwitchWeapon",
    "Aim",
)


def require(path, expected_class):
    asset = unreal.load_asset(path)
    if asset is None or asset.get_class().get_path_name() != expected_class:
        raise RuntimeError(f"Missing or wrong-class saved asset: {path}")
    return asset


input_config = require(
    f"{INPUT_FOLDER}/DA_MiniInputConfig.DA_MiniInputConfig", "/Script/FPS.MiniInputConfig"
)
mapping_context = require(
    f"{INPUT_FOLDER}/IMC_MiniDefault.IMC_MiniDefault",
    "/Script/EnhancedInput.InputMappingContext",
)
actions = tuple(
    require(
        f"{INPUT_FOLDER}/IA_Mini{name}.IA_Mini{name}",
        "/Script/EnhancedInput.InputAction",
    )
    for name in ACTION_NAMES
)
pawn_set = require(
    "/Game/Mini/Diagnostics/AbilitySets/DA_MiniPawnAbilitySet.DA_MiniPawnAbilitySet",
    "/Script/FPS.MiniAbilitySet",
)
pawn_data = require(
    "/Game/Mini/Diagnostics/PawnData/DA_MiniDiagnosticsPawnData.DA_MiniDiagnosticsPawnData",
    "/Script/FPS.MiniPawnData",
)
experience = require(
    "/Game/Mini/Diagnostics/Experiences/DA_MiniDiagnosticsExperience.DA_MiniDiagnosticsExperience",
    "/Script/FPS.MiniExperienceDefinition",
)

if not unreal.MiniTask10AssetSetupLibrary.verify_practice_input(
    input_config, mapping_context, pawn_set, *actions, pawn_data, experience
):
    raise RuntimeError("Saved Task 10 entries, key mappings, or links differ from configuration")

unreal.log("MINI_TASK10_ASSETS_VERIFIED")
