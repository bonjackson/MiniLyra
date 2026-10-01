"""Idempotently author Task 12 ability entries and tag relationships."""

import unreal


SET_PATH = "/Game/Mini/Diagnostics/AbilitySets/DA_MiniPawnAbilitySet.DA_MiniPawnAbilitySet"
PAWN_DATA_PATH = "/Game/Mini/Diagnostics/PawnData/DA_MiniDiagnosticsPawnData.DA_MiniDiagnosticsPawnData"
RELATIONSHIP_FOLDER = "/Game/Mini/Diagnostics/AbilitySets"
RELATIONSHIP_NAME = "DA_MiniTagRelationships"
RELATIONSHIP_PATH = (
    f"{RELATIONSHIP_FOLDER}/{RELATIONSHIP_NAME}.{RELATIONSHIP_NAME}"
)


def require_class(path):
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError(f"Required Task 12 class is unavailable: {path}")
    return cls


def require_asset(path, cls):
    asset = unreal.load_asset(path)
    if asset is None or asset.get_class() != cls:
        raise RuntimeError(f"Required asset missing or wrong class: {path}")
    return asset


set_class = require_class("/Script/FPS.MiniAbilitySet")
pawn_data_class = require_class("/Script/FPS.MiniPawnData")
relationships_class = require_class("/Script/FPS.MiniAbilityTagRelationshipMapping")
require_class("/Script/FPS.MiniGameplayAbility_Jump")
require_class("/Script/FPS.MiniGameplayAbility_Aim")

pawn_set = require_asset(SET_PATH, set_class)
pawn_data = require_asset(PAWN_DATA_PATH, pawn_data_class)
if unreal.EditorAssetLibrary.does_asset_exist(RELATIONSHIP_PATH):
    relationships = require_asset(RELATIONSHIP_PATH, relationships_class)
else:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", relationships_class)
    relationships = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        RELATIONSHIP_NAME, RELATIONSHIP_FOLDER, relationships_class, factory
    )
    if relationships is None:
        raise RuntimeError(f"Could not create {RELATIONSHIP_PATH}")

if not unreal.MiniTask12AssetSetupLibrary.configure_practice_abilities(
    pawn_set, relationships, pawn_data
):
    raise RuntimeError("Task 12 asset bridge rejected existing assets")

for asset in (pawn_set, relationships, pawn_data):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")

if not unreal.MiniTask12AssetSetupLibrary.verify_practice_abilities(
    pawn_set, relationships, pawn_data
):
    raise RuntimeError("Task 12 asset bridge failed in-process verification")

unreal.log(f"MINI_TASK12_PAWN_SET={pawn_set.get_path_name()}")
unreal.log(f"MINI_TASK12_TAG_RELATIONSHIPS={relationships.get_path_name()}")
unreal.log("MINI_TASK12_ASSETS_CREATED")
