"""Read package metadata in the isolated Lyra content copy, without loading gameplay assets."""
import json
from pathlib import Path

import unreal

PROJECT = Path(__file__).resolve().parents[1]
REPORT = PROJECT / "Docs" / "Task02" / "SourceAssetAudit.json"
SEEDS = [
    "/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple",
    "/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle",
    "/Game/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd",
    "/Game/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd",
    "/Game/Weapons/Rifle/Mesh/SK_Rifle",
    "/Game/Weapons/Pistol/Mesh/SK_Pistol",
    "/Game/Weapons/Rifle/Mesh/SM_Rifle",
    "/Game/Weapons/Pistol/Mesh/SM_Pistol",
    "/Game/Weapons/Rifle/Animations/Weap_Rifle_Fire",
    "/Game/Weapons/Pistol/Animations/Weap_Pistol_Fire",
]
EXCLUDED_EXAMPLES = [
    "/Game/Characters/Heroes/B_Hero_Default",
    "/Game/System/Experiences/B_LyraDefaultExperience",
    "/Game/Characters/Heroes/SimplePawnData/SimplePawnData",
    "/Game/Weapons/GA_Weapon_Fire",
    "/Game/Weapons/Pistol/GA_Weapon_Reload_Pistol",
]

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
registry.scan_paths_synchronous(["/Game"], True)


def dependency_options(hard, soft):
    return unreal.AssetRegistryDependencyOptions(
        include_hard_package_references=hard,
        include_soft_package_references=soft,
        include_searchable_names=False,
        include_hard_management_references=False,
        include_soft_management_references=False,
    )


def describe(package):
    assets = registry.get_assets_by_package_name(package)
    return {
        "package": package,
        "classes": [
            f"{asset.asset_class_path.package_name}.{asset.asset_class_path.asset_name}"
            for asset in assets
        ],
        "hard": sorted(str(x) for x in registry.get_dependencies(package, dependency_options(True, False))),
        "soft": sorted(str(x) for x in registry.get_dependencies(package, dependency_options(False, True))),
    }


nodes = {}
pending = list(SEEDS)
while pending:
    package = pending.pop()
    if package in nodes or not package.startswith("/Game/"):
        continue
    node = describe(package)
    if not node["classes"]:
        raise RuntimeError(f"Package missing from audit content: {package}")
    nodes[package] = node
    pending.extend(node["hard"] + node["soft"])

report = {
    "source_project": "F:/LyraStarterGame",
    "method": "Unreal AssetRegistry hard/soft package dependency graph (same package edges displayed by Reference Viewer)",
    "scope": "Lyra source-project weapons plus copied UE 5.8 mannequin-template assets; engine/plugin module dependencies are recorded but not recursively scanned",
    "seeds": SEEDS,
    "nodes": sorted(nodes.values(), key=lambda node: node["package"]),
    "excluded_examples": [describe(package) for package in EXCLUDED_EXAMPLES],
}
REPORT.parent.mkdir(parents=True, exist_ok=True)
REPORT.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
unreal.log(f"MINI_ASSET_AUDIT_PASSED packages={len(nodes)} report={REPORT}")
