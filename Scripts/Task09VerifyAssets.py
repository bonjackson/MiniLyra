"""Verify the saved Task 09 assets in a fresh Unreal Editor process."""

import unreal


def require(path, expected_class):
    asset = unreal.load_asset(path)
    if asset is None or asset.get_class().get_path_name() != expected_class:
        raise RuntimeError(f"Missing or wrong-class saved asset: {path}")
    return asset


pawn_set = require(
    "/Game/Mini/Diagnostics/AbilitySets/DA_MiniPawnAbilitySet.DA_MiniPawnAbilitySet",
    "/Script/FPS.MiniAbilitySet",
)
feature_set = require(
    "/Game/Mini/Diagnostics/AbilitySets/DA_MiniFeatureAbilitySet.DA_MiniFeatureAbilitySet",
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
if not unreal.MiniTask09AssetSetupLibrary.verify_practice_abilities(
    pawn_set, feature_set, pawn_data, experience
):
    raise RuntimeError("Saved Task 09 entries or links differ from the expected configuration")

unreal.log("MINI_TASK09_ASSETS_VERIFIED")
