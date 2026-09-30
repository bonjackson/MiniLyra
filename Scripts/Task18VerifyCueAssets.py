"""Fresh-process verification of the saved feature Cue Action and scan asset."""

import unreal


FEATURE_DATA = "/MiniShooterCore/GameFeatureData.GameFeatureData"
CUE_DIRECTORY = "/MiniShooterCore/GameplayCues"
CUE_PATH = f"{CUE_DIRECTORY}/GC_Mini_AssetProbe.GC_Mini_AssetProbe"

feature = unreal.load_asset(FEATURE_DATA)
if feature is None or feature.get_class().get_path_name() != "/Script/GameFeatures.GameFeatureData":
    raise RuntimeError("Saved GameFeatureData is missing")
actions = [
    action for action in feature.get_editor_property("actions")
    if action.get_name() == "MiniTask18_AddGameplayCuePath"
]
if len(actions) != 1 or actions[0].get_class().get_path_name() != "/Script/FPS.MiniGameFeatureAction_AddGameplayCuePath":
    raise RuntimeError("Saved feature must contain exactly one Cue path Action")
if not unreal.MiniTask18CueAssetSetupLibrary.verify_plugin_cue_action(feature):
    raise RuntimeError("Saved Cue path Action has the wrong plugin package path")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous([CUE_DIRECTORY], force_rescan=True)
cue_class = unreal.EditorAssetLibrary.load_blueprint_class(CUE_PATH)
if not unreal.MiniTask18CueAssetSetupLibrary.verify_probe_cue_class(cue_class):
    raise RuntimeError("Saved CueNotify class/tag/registry name is invalid")

unreal.log("MINI_TASK18_CUE_ASSETS_VERIFIED")
