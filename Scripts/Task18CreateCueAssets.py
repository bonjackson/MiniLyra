"""Create the feature-scoped Cue path Action and a no-effect CueNotify scan probe."""

import unreal


FEATURE_DATA = "/MiniShooterCore/GameFeatureData.GameFeatureData"
CUE_DIRECTORY = "/MiniShooterCore/GameplayCues"
CUE_NAME = "GC_Mini_AssetProbe"
CUE_PATH = f"{CUE_DIRECTORY}/{CUE_NAME}.{CUE_NAME}"
ACTION_NAME = "MiniTask18_AddGameplayCuePath"
PROBE_TAG = "GameplayCue.Mini.AssetProbe"


def require_class(path):
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError(f"Missing required class: {path}")
    return cls


feature = unreal.load_asset(FEATURE_DATA)
if feature is None or feature.get_class().get_path_name() != "/Script/GameFeatures.GameFeatureData":
    raise RuntimeError(f"Missing feature data: {FEATURE_DATA}")

require_class("/Script/FPS.MiniGameFeatureAction_AddGameplayCuePath")
if not unreal.MiniTask18CueAssetSetupLibrary.ensure_plugin_cue_action(feature):
    raise RuntimeError("Could not configure plugin Cue path Action")
action = [
    candidate for candidate in feature.get_editor_property("actions")
    if candidate.get_name() == ACTION_NAME
][0]

# The commandlet may read a cached registry that predates this plugin folder.
unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
    [CUE_DIRECTORY], force_rescan=True
)
cue_blueprint = unreal.load_asset(CUE_PATH)
if cue_blueprint is None:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property(
        "parent_class", require_class("/Script/GameplayAbilities.GameplayCueNotify_Static")
    )
    cue_blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        CUE_NAME, CUE_DIRECTORY, unreal.Blueprint, factory
    )
if cue_blueprint is None or cue_blueprint.get_class().get_path_name() != "/Script/Engine.Blueprint":
    raise RuntimeError(f"Missing or wrong-class CueNotify blueprint: {CUE_PATH}")

# UE derives GameplayCue.Mini.AssetProbe from GC_Mini_AssetProbe on its CDO.
cue_class = unreal.EditorAssetLibrary.load_blueprint_class(CUE_PATH)
if not unreal.MiniTask18CueAssetSetupLibrary.verify_probe_cue_class(cue_class):
    raise RuntimeError(f"Cue asset name did not derive {PROBE_TAG}")

for asset in (feature, cue_blueprint):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")

if not unreal.MiniTask18CueAssetSetupLibrary.verify_plugin_cue_action(feature):
    raise RuntimeError("Feature Cue path Action failed native verification")
if not unreal.MiniTask18CueAssetSetupLibrary.verify_probe_cue_class(cue_class):
    raise RuntimeError("Saved CueNotify class failed native verification")

unreal.log(f"MINI_TASK18_FEATURE_ACTION={action.get_path_name()}")
unreal.log(f"MINI_TASK18_CUE_NOTIFY={cue_blueprint.get_path_name()}")
unreal.log("MINI_TASK18_CUE_ASSETS_CREATED")
