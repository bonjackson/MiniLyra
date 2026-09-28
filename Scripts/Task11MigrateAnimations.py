"""Move selected UE 5.8 mannequin sequences inside an isolated project.

The PowerShell entry point copies only the finished animation assets into the
real project after this script has saved and checked their package references.
"""

import unreal


ANIMATIONS = [
    ("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Jump", "/Game/Mini/Characters/Mannequins/Anims/Unarmed/Jump"),
    ("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Fall_Loop", "/Game/Mini/Characters/Mannequins/Anims/Unarmed/Jump"),
    ("/Game/Characters/Mannequins/Anims/Unarmed/Jump/MM_Land", "/Game/Mini/Characters/Mannequins/Anims/Unarmed/Jump"),
    ("/Game/Characters/Mannequins/Anims/Rifle/MF_Rifle_Idle_ADS", "/Game/Mini/Characters/Mannequins/Anims/Rifle"),
    ("/Game/Characters/Mannequins/Anims/Rifle/Jog/MF_Rifle_Jog_Fwd", "/Game/Mini/Characters/Mannequins/Anims/Rifle/Jog"),
]
SKELETON_SOURCE = "/Game/Characters/Mannequins/Meshes/SK_Mannequin"
SKELETON_DESTINATION = "/Game/Mini/Characters/Mannequins/Meshes/SK_Mannequin"
QUINN_SOURCE = "/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple"

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Characters"], True)
assets = []
for source in [SKELETON_SOURCE, QUINN_SOURCE] + [item[0] for item in ANIMATIONS]:
    asset = unreal.EditorAssetLibrary.load_asset(source)
    if not asset:
        raise RuntimeError(f"Missing staged template asset: {source}")
    assets.append(asset)

renames = [
    unreal.AssetRenameData(assets[0], "/Game/Mini/Characters/Mannequins/Meshes", "SK_Mannequin"),
    unreal.AssetRenameData(assets[1], "/Game/Mini/Characters/Mannequins/Meshes", "SKM_Quinn_Simple"),
]
renames.extend(
    unreal.AssetRenameData(asset, target_dir, source.rsplit("/", 1)[-1])
    for asset, (source, target_dir) in zip(assets[2:], ANIMATIONS)
)
if not unreal.AssetToolsHelpers.get_asset_tools().rename_assets(renames):
    raise RuntimeError("AssetTools failed to rename template assets into /Game/Mini")
registry.scan_paths_synchronous(["/Game/Mini"], True)

options = unreal.AssetRegistryDependencyOptions(
    include_hard_package_references=True,
    include_soft_package_references=True,
    include_searchable_names=False,
    include_hard_management_references=False,
    include_soft_management_references=False,
)
for source, target_dir in ANIMATIONS:
    target = f"{target_dir}/{source.rsplit('/', 1)[-1]}"
    asset = unreal.EditorAssetLibrary.load_asset(target)
    if not isinstance(asset, unreal.AnimSequence):
        raise RuntimeError(f"Migrated asset is not an AnimSequence: {target}")
    if str(asset.get_editor_property("skeleton").get_path_name()).split(".", 1)[0] != SKELETON_DESTINATION:
        raise RuntimeError(f"Animation skeleton was not renamed with asset: {target}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save animation: {target}")
    dependencies = [str(dependency) for dependency in (registry.get_dependencies(target, options) or [])]
    stale = [dependency for dependency in dependencies if dependency.startswith("/Game/Characters/")]
    if stale:
        raise RuntimeError(f"Old template references remain on {target}: {stale}")
    unreal.log(f"MINI_TASK11_ANIMATION_OK Asset={target} Dependencies={dependencies}")

unreal.log("MINI_TASK11_ANIMATIONS_MIGRATED Count=5")
