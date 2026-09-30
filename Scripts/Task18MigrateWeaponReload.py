"""Retarget Lyra weapon reload sequences in an isolated UE 5.8 project.

The source skeletons and sequences are staged at their original package paths.
AssetTools renames each pair together so the saved sequences refer to Mini's
already-existing weapon skeleton paths. The PowerShell entry point copies only
the two finished sequences into the real project.
"""

import unreal


WEAPONS = (
    ("Rifle", "SK_Rifle_Skeleton", "Weap_Rifle_Reload"),
    ("Pistol", "SK_Pistol_Skeleton", "Weap_Pistol_Reload"),
)

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Weapons"], True)

renames = []
for weapon, skeleton_name, sequence_name in WEAPONS:
    source_skeleton = f"/Game/Weapons/{weapon}/Mesh/{skeleton_name}"
    source_sequence = f"/Game/Weapons/{weapon}/Animations/{sequence_name}"
    skeleton = unreal.EditorAssetLibrary.load_asset(source_skeleton)
    sequence = unreal.EditorAssetLibrary.load_asset(source_sequence)
    if not isinstance(skeleton, unreal.Skeleton):
        raise RuntimeError(f"Missing staged weapon skeleton: {source_skeleton}")
    if not isinstance(sequence, unreal.AnimSequence):
        raise RuntimeError(f"Missing staged reload sequence: {source_sequence}")
    renames.extend(
        (
            unreal.AssetRenameData(skeleton, f"/Game/Mini/Weapons/{weapon}/Mesh", skeleton_name),
            unreal.AssetRenameData(sequence, f"/Game/Mini/Weapons/{weapon}/Animations", sequence_name),
        )
    )

if not unreal.AssetToolsHelpers.get_asset_tools().rename_assets(renames):
    raise RuntimeError("AssetTools failed to retarget staged reload sequences")

registry.scan_paths_synchronous(["/Game/Mini/Weapons"], True)
options = unreal.AssetRegistryDependencyOptions(
    include_hard_package_references=True,
    include_soft_package_references=True,
    include_searchable_names=False,
    include_hard_management_references=False,
    include_soft_management_references=False,
)

for weapon, skeleton_name, sequence_name in WEAPONS:
    target_skeleton = f"/Game/Mini/Weapons/{weapon}/Mesh/{skeleton_name}"
    target_sequence = f"/Game/Mini/Weapons/{weapon}/Animations/{sequence_name}"
    skeleton = unreal.EditorAssetLibrary.load_asset(target_skeleton)
    sequence = unreal.EditorAssetLibrary.load_asset(target_sequence)
    if not isinstance(skeleton, unreal.Skeleton):
        raise RuntimeError(f"Retargeted skeleton missing: {target_skeleton}")
    if not isinstance(sequence, unreal.AnimSequence):
        raise RuntimeError(f"Retargeted sequence missing: {target_sequence}")
    actual_skeleton = str(sequence.get_editor_property("skeleton").get_path_name()).split(".", 1)[0]
    if actual_skeleton != target_skeleton:
        raise RuntimeError(f"Wrong skeleton for {target_sequence}: {actual_skeleton}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(sequence, only_if_is_dirty=False):
        raise RuntimeError(f"Failed to save retargeted sequence: {target_sequence}")
    dependencies = sorted(str(value) for value in registry.get_dependencies(target_sequence, options))
    if target_skeleton not in dependencies:
        raise RuntimeError(f"Target skeleton absent from sequence dependencies: {target_sequence}: {dependencies}")
    stale = [value for value in dependencies if value.startswith("/Game/Weapons/")]
    if stale:
        raise RuntimeError(f"Old Lyra package references remain on {target_sequence}: {stale}")
    unreal.log(f"MINI_TASK18_RELOAD_OK Asset={target_sequence} Skeleton={actual_skeleton}")

unreal.log("MINI_TASK18_RELOAD_MIGRATED Count=2")
