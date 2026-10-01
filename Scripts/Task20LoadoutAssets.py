"""Create task 20 loadout assets and set only the two owned PawnData loadout links.

Run after the task 20 assembly authoring has created Diagnostics PawnData and
after building FPSEditor. This script does not create/copy PawnData or Actions.
"""

import unreal


LOADOUT_FOLDER = "/Game/Mini/System/Loadouts"
PAWN_DATA_PATHS = (
    "/Game/Mini/System/PawnData/DA_MiniPracticePawnData.DA_MiniPracticePawnData",
    "/Game/Mini/Diagnostics/PawnData/DA_MiniDiagnosticsPawnData.DA_MiniDiagnosticsPawnData",
)
PRESETS = (
    ("DA_MiniSharedCombatLoadout", "configure_shared_loadout", "verify_shared_loadout"),
    ("DA_MiniRifleOnlyLoadout", "configure_rifle_only_loadout", "verify_rifle_only_loadout"),
    ("DA_MiniUnarmedLoadout", "configure_unarmed_loadout", "verify_unarmed_loadout"),
)


def require_class(path):
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError(f"Missing task 20 class; build FPSEditor first: {path}")
    return cls


def require_asset(path, cls):
    asset = unreal.load_asset(path)
    if asset is None or asset.get_class() != cls:
        raise RuntimeError(f"Required existing asset missing or wrong class: {path}")
    return asset


def ensure_loadout(name, cls):
    path = f"{LOADOUT_FOLDER}/{name}.{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return require_asset(path, cls)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, LOADOUT_FOLDER, cls, factory)
    if asset is None:
        raise RuntimeError(f"Could not create loadout: {path}")
    return asset


def path_of(value):
    return value.get_path_name() if value is not None else None


def other_pawn_data_fields(pawn_data):
    return {
        "pawn_class": path_of(pawn_data.get_editor_property("pawn_class")),
        "ability_sets": [path_of(value) for value in pawn_data.get_editor_property("ability_sets")],
        "tag_relationship_mapping": path_of(pawn_data.get_editor_property("tag_relationship_mapping")),
        "input_config": path_of(pawn_data.get_editor_property("input_config")),
        "default_camera_mode": path_of(pawn_data.get_editor_property("default_camera_mode")),
        "aim_camera_mode": path_of(pawn_data.get_editor_property("aim_camera_mode")),
    }


loadout_class = require_class("/Script/FPS.MiniLoadoutDefinition")
pawn_data_class = require_class("/Script/FPS.MiniPawnData")
require_class("/Script/FPS.MiniTask20LoadoutAssetLibrary")
require_class("/Script/FPS.MiniRifleItemDefinition")
require_class("/Script/FPS.MiniPistolItemDefinition")
library = unreal.MiniTask20LoadoutAssetLibrary
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Mini/System"], force_rescan=True)

# Check both assembly-owned PawnData assets before authoring anything. Creating
# Diagnostics PawnData is intentionally left to the task 20 assembly pipeline.
pawn_data_assets = [require_asset(path, pawn_data_class) for path in PAWN_DATA_PATHS]
previous_fields = [other_pawn_data_fields(asset) for asset in pawn_data_assets]
loadouts = []
for name, configure_name, verify_name in PRESETS:
    loadout = ensure_loadout(name, loadout_class)
    if not getattr(library, configure_name)(loadout):
        raise RuntimeError(f"Native loadout authoring rejected {loadout.get_path_name()}")
    if not getattr(library, verify_name)(loadout):
        raise RuntimeError(f"Native loadout verification failed on {loadout.get_path_name()}")
    loadouts.append(loadout)

shared_loadout = loadouts[0]
for index, pawn_data in enumerate(pawn_data_assets):
    if not library.configure_pawn_data_loadout(pawn_data, shared_loadout):
        raise RuntimeError(f"Could not set DefaultLoadout on {pawn_data.get_path_name()}")
    if other_pawn_data_fields(pawn_data) != previous_fields[index]:
        raise RuntimeError(f"Loadout authoring modified other PawnData fields: {pawn_data.get_path_name()}")

if not library.verify_validation_cases():
    raise RuntimeError("Transient loadout boundary validation failed")
for asset in loadouts + pawn_data_assets:
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")
for pawn_data in pawn_data_assets:
    if not library.verify_pawn_data_loadout(pawn_data, shared_loadout):
        raise RuntimeError(f"Saved PawnData loadout link failed verification: {pawn_data.get_path_name()}")

unreal.log("MINI_TASK20_LOADOUT_ASSETS_CREATED Shared=2 RifleOnly=1 Unarmed=0 PawnDataLinks=2")
