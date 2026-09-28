"""Reload and inspect the saved Task 12 assets in a fresh editor process."""

import unreal


def require(path, class_path):
    asset = unreal.load_asset(path)
    if asset is None or asset.get_class().get_path_name() != class_path:
        raise RuntimeError(f"Missing or wrong-class saved asset: {path}")
    return asset


pawn_set = require(
    "/Game/Mini/System/AbilitySets/DA_MiniPawnAbilitySet.DA_MiniPawnAbilitySet",
    "/Script/FPS.MiniAbilitySet",
)
relationships = require(
    "/Game/Mini/System/AbilitySets/DA_MiniTagRelationships.DA_MiniTagRelationships",
    "/Script/FPS.MiniAbilityTagRelationshipMapping",
)
pawn_data = require(
    "/Game/Mini/System/PawnData/DA_MiniPracticePawnData.DA_MiniPracticePawnData",
    "/Script/FPS.MiniPawnData",
)

if not unreal.MiniTask12AssetSetupLibrary.verify_practice_abilities(
    pawn_set, relationships, pawn_data
):
    raise RuntimeError("Saved Task 12 ability entries, tag rules or PawnData link differ")

unreal.log("MINI_TASK12_ASSETS_VERIFIED")
