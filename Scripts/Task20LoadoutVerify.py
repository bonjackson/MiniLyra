"""Read-only saved-asset verification in a fresh process; no authoring or saves."""

import unreal


LOADOUT_FOLDER = "/Game/Mini/System/Loadouts"
PAWN_DATA_PATHS = (
    "/Game/Mini/System/PawnData/DA_MiniPracticePawnData.DA_MiniPracticePawnData",
    "/Game/Mini/Diagnostics/PawnData/DA_MiniDiagnosticsPawnData.DA_MiniDiagnosticsPawnData",
)
PRESETS = (
    ("DA_MiniSharedCombatLoadout", "verify_shared_loadout", 2),
    ("DA_MiniRifleOnlyLoadout", "verify_rifle_only_loadout", 1),
    ("DA_MiniUnarmedLoadout", "verify_unarmed_loadout", 0),
)


def require_asset(path, class_path):
    asset = unreal.load_asset(path)
    if asset is None or asset.get_class().get_path_name() != class_path:
        raise RuntimeError(f"Saved asset missing or wrong class: {path}")
    return asset


registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Mini/System"], force_rescan=True)
library = unreal.MiniTask20LoadoutAssetLibrary
loadouts = []
for name, verify_name, expected_count in PRESETS:
    loadout = require_asset(f"{LOADOUT_FOLDER}/{name}.{name}", "/Script/FPS.MiniLoadoutDefinition")
    if not getattr(library, verify_name)(loadout):
        raise RuntimeError(f"Saved preset values or runtime validation failed: {loadout.get_path_name()}")
    if len(loadout.get_editor_property("items")) != expected_count:
        raise RuntimeError(f"Unexpected saved item count: {loadout.get_path_name()}")
    loadouts.append(loadout)

for path in PAWN_DATA_PATHS:
    pawn_data = require_asset(path, "/Script/FPS.MiniPawnData")
    if not library.verify_pawn_data_loadout(pawn_data, loadouts[0]):
        raise RuntimeError(f"Saved PawnData must reference shared loadout: {path}")

# The library creates transient fixtures only; no asset values are modified.
if not library.verify_validation_cases():
    raise RuntimeError("Loadout validation accepted a malformed slot/class/selection")
options = unreal.AssetRegistryDependencyOptions(
    include_hard_package_references=True,
    include_soft_package_references=True,
    include_searchable_names=False,
    include_hard_management_references=False,
    include_soft_management_references=False,
)
for loadout in loadouts:
    package = loadout.get_path_name().split(".", 1)[0]
    dependencies = [str(value) for value in registry.get_dependencies(package, options) or []]
    stale = [value for value in dependencies if value.startswith(("/Script/LyraGame", "/ShooterCore/"))]
    if stale:
        raise RuntimeError(f"Unexpected legacy loadout dependency on {package}: {stale}")

unreal.log("MINI_TASK20_LOADOUT_ASSETS_VERIFIED Shared=2 RifleOnly=1 Unarmed=0 PawnDataLinks=2")
