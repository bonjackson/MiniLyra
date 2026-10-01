"""Validate the relocated art assets and the saved practice map in Unreal."""
import json
from pathlib import Path

import unreal


MAP_PATH = "/Game/Mini/Maps/L_MiniPractice"
PROJECT = Path(__file__).resolve().parents[1]
REPORT = PROJECT / "Docs" / "Task02" / "Verification.json"
ASSETS = {
    "/Game/Mini/Characters/Mannequins/Meshes/SKM_Manny_Simple": "SkeletalMesh",
    "/Game/Mini/Characters/Mannequins/Anims/Unarmed/MM_Idle": "AnimSequence",
    "/Game/Mini/Characters/Mannequins/Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd": "AnimSequence",
    "/Game/Mini/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd": "AnimSequence",
    "/Game/Mini/Weapons/Rifle/Mesh/SK_Rifle": "SkeletalMesh",
    "/Game/Mini/Weapons/Rifle/Animations/Weap_Rifle_Fire": "AnimSequence",
    "/Game/Mini/Weapons/Pistol/Mesh/SK_Pistol": "SkeletalMesh",
    "/Game/Mini/Weapons/Pistol/Animations/Weap_Pistol_Fire": "AnimSequence",
}

for package_name, expected_class in ASSETS.items():
    asset = unreal.load_asset(package_name)
    if asset is None:
        raise RuntimeError(f"Approved asset did not load: {package_name}")
    class_name = str(asset.get_class().get_name())
    if class_name != expected_class:
        raise RuntimeError(f"Unexpected class for {package_name}: {class_name}")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
registry.scan_paths_synchronous(["/Game/Mini"], True)
package_data = registry.get_assets_by_path("/Game/Mini", True)
package_names = sorted(set(str(data.package_name) for data in package_data))
if len(package_names) < 50:
    raise RuntimeError(f"Expected the 49 art packages and map, found {len(package_names)}")
for package_name in package_names:
    if package_name != MAP_PATH and unreal.load_asset(package_name) is None:
        raise RuntimeError(f"Migrated dependency package did not load: {package_name}")

options = unreal.AssetRegistryDependencyOptions(
    include_hard_package_references=True,
    include_soft_package_references=True,
    include_searchable_names=False,
    include_hard_management_references=False,
    include_soft_management_references=False,
)
bad_references = []
for package_name in package_names:
    for dependency in registry.get_dependencies(package_name, options):
        dependency = str(dependency)
        if dependency == "/Script/LyraGame":
            bad_references.append([package_name, dependency])
        if dependency.startswith("/Game/") and not dependency.startswith("/Game/Mini/"):
            bad_references.append([package_name, dependency])
if bad_references:
    raise RuntimeError(f"Unexpected project dependencies: {bad_references}")

level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not level_editor.load_level(MAP_PATH):
    raise RuntimeError(f"Could not load map: {MAP_PATH}")
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
settings_class = world.get_world_settings().get_class().get_path_name()
if settings_class not in {"/Script/Engine.WorldSettings", "/Script/FPS.MiniWorldSettings"}:
    raise RuntimeError(f"Unexpected WorldSettings: {settings_class}")

actors = unreal.EditorLevelLibrary.get_all_level_actors()
labels = [actor.get_actor_label() for actor in actors]
required = [
    "Arena_Floor_40x30m",
    "Wall_North", "Wall_South", "Wall_East", "Wall_West",
    "PlayerStart_SW", "PlayerStart_SE", "PlayerStart_NW", "PlayerStart_NE",
    "Practice_Sun",
]
missing = [name for name in required if name not in labels]
if missing:
    raise RuntimeError(f"Required map actors are missing: {missing}")
starts = [actor for actor in actors if actor.get_class().get_path_name() == "/Script/Engine.PlayerStart"]
if len(starts) != 4:
    raise RuntimeError(f"Expected four PlayerStart actors, got {len(starts)}")
if len([label for label in labels if label.startswith("Cover_")]) != 6:
    raise RuntimeError("Expected six cover pieces")
assembled = unreal.EditorAssetLibrary.does_asset_exist(
    "/Game/Mini/System/ActionSets/DA_MiniCombatActionSet.DA_MiniCombatActionSet"
)
board_count = len([label for label in labels if label.endswith("_Board")])
if assembled:
    if board_count != 0 or any(label.endswith("_Bullseye") for label in labels):
        raise RuntimeError("Obsolete static boards still block Experience-owned targets")
    if len([label for label in labels if label.endswith("_Stand")]) != 3:
        raise RuntimeError("Expected the three retained practice target stands")
else:
    if board_count != 3:
        raise RuntimeError("Expected three visible target boards")

floor = next(actor for actor in actors if actor.get_actor_label() == "Arena_Floor_40x30m")
scale = floor.get_actor_scale3d()
if abs(scale.x - 40) > 0.001 or abs(scale.y - 30) > 0.001:
    raise RuntimeError(f"Unexpected arena floor scale: {scale}")

report = {
    "map": MAP_PATH,
    "world_settings": settings_class,
    "mini_package_count": len(package_names),
    "approved_assets_loaded": sorted(ASSETS),
    "player_starts": len(starts),
    "cover_pieces": 6,
    "visible_target_boards": board_count,
    "targets_owned_by_experience": assembled,
    "arena_size_cm": [4000, 3000],
    "unexpected_project_dependencies": bad_references,
}
REPORT.parent.mkdir(parents=True, exist_ok=True)
REPORT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
unreal.log(f"MINI_TASK02_MAP_LOADED={world.get_path_name()}")
unreal.log(f"MINI_TASK02_ASSETS_LOADED={len(ASSETS)}")
unreal.log(f"MINI_TASK02_PACKAGES_VALIDATED={len(package_names)}")
unreal.log("MINI_TASK02_VERIFICATION_PASSED")
