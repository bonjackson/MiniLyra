"""Verify saved policy defaults and feature UI requests in a fresh editor process."""

import unreal


POLICY_PATH = "/Game/Mini/UI/B_MiniUIPolicy.B_MiniUIPolicy"
FEATURE_PATH = "/MiniShooterCore/GameFeatureData.GameFeatureData"
ACTION_NAME = "MiniTask19_AddWidgets"

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Mini/UI", "/MiniShooterCore"], force_rescan=True)
policy = unreal.load_asset(POLICY_PATH)
feature = unreal.load_asset(FEATURE_PATH)
if policy is None or policy.get_class().get_path_name() != "/Script/Engine.Blueprint":
    raise RuntimeError("Missing saved Mini UI policy Blueprint")
if feature is None or feature.get_class().get_path_name() != "/Script/GameFeatures.GameFeatureData":
    raise RuntimeError("Missing saved MiniShooterCore feature data")
if not unreal.MiniTask19AssetSetupLibrary.verify_policy_blueprint(policy):
    raise RuntimeError("Saved policy has the wrong parent, compilation status or native root class")
if not unreal.MiniTask19AssetSetupLibrary.verify_plugin_widget_action(feature):
    raise RuntimeError("Saved feature must own exactly one Game-layer HUD and four correct elements")

actions = [action for action in feature.get_editor_property("actions")
           if action is not None and action.get_name() == ACTION_NAME]
if len(actions) != 1 or actions[0].get_class().get_path_name() != "/Script/FPS.MiniGameFeatureAction_AddWidgets":
    raise RuntimeError("Saved AddWidgets Action name or class is invalid")

options = unreal.AssetRegistryDependencyOptions(
    include_hard_package_references=True,
    include_soft_package_references=True,
    include_searchable_names=False,
    include_hard_management_references=False,
    include_soft_management_references=False,
)
for package in (POLICY_PATH.split(".", 1)[0], FEATURE_PATH.split(".", 1)[0]):
    legacy = [str(path) for path in registry.get_dependencies(package, options) or []
              if str(path).startswith(("/Script/LyraGame", "/Game/UI/", "/ShooterCore/"))]
    if legacy:
        raise RuntimeError(f"Stale Lyra UI dependencies on {package}: {legacy}")

unreal.log("MINI_TASK19_UI_ASSETS_VERIFIED Policy=1 Layouts=1 Elements=4")
