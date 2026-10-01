"""Read the saved FFA assets without authoring, compiling or saving them."""
import json
from pathlib import Path
import unreal

PROJECT = Path(unreal.Paths.project_dir())
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/MiniArena", "/Game/Mini/System", "/Game/Mini/Diagnostics"], force_rescan=True)
descriptor = json.loads((PROJECT / "Plugins/GameFeatures/MiniArena/MiniArena.uplugin").read_text(encoding="utf-8-sig"))
if descriptor.get("Modules") or not descriptor.get("CanContainContent") or not descriptor.get("ExplicitlyLoaded"):
    raise RuntimeError("MiniArena must remain an explicitly loaded content-only GameFeature")
for diagnostic in (False, True):
    if not unreal.MiniTask22AssetSetupLibrary.verify_saved_assembly(diagnostic):
        raise RuntimeError(f"Saved fixed FFA classes, values, CDO or stock server-only component entries invalid: {diagnostic}")
practice = unreal.load_asset("/Game/Mini/System/Experiences/DA_MiniPracticeExperience.DA_MiniPracticeExperience")
combat = unreal.load_asset("/Game/Mini/System/ActionSets/DA_MiniCombatActionSet.DA_MiniCombatActionSet")
practice_set = unreal.load_asset("/Game/Mini/System/ActionSets/DA_MiniPracticeActionSet.DA_MiniPracticeActionSet")
if practice is None or list(practice.get_editor_property("action_sets")) != [combat, practice_set]:
    raise RuntimeError("Training Experience no longer owns only Combat and Practice")
for asset in (practice, combat, practice_set):
    if list(asset.get_editor_property("game_features_to_enable")) != ["MiniShooterCore"]:
        raise RuntimeError("An Arena dependency leaked into training")
if unreal.EditorAssetLibrary.does_asset_exist("/MiniArena/GameFeatureData.GameFeatureData"):
    raise RuntimeError("Duplicate Arena GameFeatureData or redirector remains")
for kind, names in (
    ("Experiences", ("DA_MiniArenaExperience", "DA_MiniFFADiagnosticsExperience")),
    ("ActionSets", ("DA_MiniArenaActionSet", "DA_MiniFFADiagnosticsActionSet")),
):
    assets = []
    for root in ("/Game/Mini/System", "/Game/Mini/Diagnostics"):
        assets.extend(registry.get_assets_by_path(f"{root}/{kind}", recursive=True))
    for name in names:
        if sum(str(asset.asset_name) == name for asset in assets) != 1:
            raise RuntimeError(f"Ambiguous primary asset name: {name}")
unreal.log("MINI_TASK22_ASSETS_VERIFIED Production=0/300/5 Diagnostics=0/20/3 Match=2/10/2/3 ServerOnly=1 TrainingPreserved=1")
