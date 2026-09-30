"""Idempotently author the practice UI policy and the feature-owned HUD requests."""

import unreal


POLICY_FOLDER = "/Game/Mini/UI"
POLICY_NAME = "B_MiniUIPolicy"
POLICY_PATH = f"{POLICY_FOLDER}/{POLICY_NAME}.{POLICY_NAME}"
FEATURE_PATH = "/MiniShooterCore/GameFeatureData.GameFeatureData"
ACTION_NAME = "MiniTask19_AddWidgets"


def require_class(path):
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError(f"Missing Task 19 class: {path}")
    return cls


policy_parent = require_class("/Script/FPS.MiniGameUIPolicy")
for name in (
    "MiniPrimaryGameLayout", "MiniHUDLayout", "MiniHUDHealthWidget",
    "MiniHUDAmmoWidget", "MiniHUDCrosshairWidget", "MiniHUDMatchWidget",
    "MiniGameFeatureAction_AddWidgets", "MiniTask19AssetSetupLibrary",
):
    require_class(f"/Script/FPS.{name}")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous([POLICY_FOLDER, "/MiniShooterCore"], force_rescan=True)
feature = unreal.load_asset(FEATURE_PATH)
if feature is None or feature.get_class().get_path_name() != "/Script/GameFeatures.GameFeatureData":
    raise RuntimeError(f"Missing feature data: {FEATURE_PATH}")

previous_actions = [
    (action.get_path_name(), action.get_class().get_path_name())
    for action in feature.get_editor_property("actions")
    if action is not None and action.get_name() != ACTION_NAME
]

policy = unreal.load_asset(POLICY_PATH)
if policy is None:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", policy_parent)
    policy = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        POLICY_NAME, POLICY_FOLDER, unreal.Blueprint, factory
    )
if policy is None or policy.get_class().get_path_name() != "/Script/Engine.Blueprint":
    raise RuntimeError(f"Wrong policy asset class: {POLICY_PATH}")
if not unreal.MiniTask19AssetSetupLibrary.ensure_policy_blueprint(policy):
    raise RuntimeError("Could not author native root LayoutClass on the policy Blueprint")
if not unreal.MiniTask19AssetSetupLibrary.ensure_plugin_widget_action(feature):
    raise RuntimeError("Could not author the unique practice AddWidgets Action")

current_actions = [
    (action.get_path_name(), action.get_class().get_path_name())
    for action in feature.get_editor_property("actions")
    if action is not None and action.get_name() != ACTION_NAME
]
if current_actions != previous_actions:
    raise RuntimeError("Task 19 authoring changed another feature Action")

for asset in (policy, feature):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")
if not unreal.MiniTask19AssetSetupLibrary.verify_policy_blueprint(policy):
    raise RuntimeError("Policy defaults failed native verification")
if not unreal.MiniTask19AssetSetupLibrary.verify_plugin_widget_action(feature):
    raise RuntimeError("Feature UI requests failed native verification")

unreal.log(f"MINI_TASK19_POLICY={policy.get_path_name()}")
unreal.log("MINI_TASK19_UI_ASSETS_CREATED Layouts=1 Elements=4")
