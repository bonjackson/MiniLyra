"""Read-only fresh-process verification of the precise production cook label.

Check the label, merged scan configuration, native/config defaults and saved
Blueprint -> definition -> material package references. No compile, repairs,
asset creation or saves occur here.
"""

import unreal


LABEL_FOLDER = "/Game/Mini/System/Packaging"
LABEL_PATH = f"{LABEL_FOLDER}/DA_MiniPracticeCook.DA_MiniPracticeCook"
ASSET_SPECS = (
    ("/Game/Mini/Weapons/Rifle/Mesh/SK_Rifle.SK_Rifle", unreal.SkeletalMesh),
    ("/Game/Mini/Weapons/Pistol/Mesh/SK_Pistol.SK_Pistol", unreal.SkeletalMesh),
    ("/Game/Mini/Weapons/Rifle/Animations/AM_MiniRifle_Fire.AM_MiniRifle_Fire", unreal.AnimMontage),
    ("/Game/Mini/Weapons/Pistol/Animations/AM_MiniPistol_Fire.AM_MiniPistol_Fire", unreal.AnimMontage),
    ("/Game/Mini/Weapons/Rifle/Animations/AM_MiniRifle_Reload.AM_MiniRifle_Reload", unreal.AnimMontage),
    ("/Game/Mini/Weapons/Pistol/Animations/AM_MiniPistol_Reload.AM_MiniPistol_Reload", unreal.AnimMontage),
    ("/Game/Audio/SoundWaves/Weapons/sfx_Weapon_AutoRifle_MainLayer_01.sfx_Weapon_AutoRifle_MainLayer_01", unreal.SoundBase),
    ("/Game/Audio/SoundWaves/Weapons/sfx_Weapon_Pistol_MainLayer_nl_01.sfx_Weapon_Pistol_MainLayer_nl_01", unreal.SoundBase),
    ("/Game/Audio/SoundWaves/Weapons/SFX_BulletImpact_01.SFX_BulletImpact_01", unreal.SoundBase),
    ("/Game/Weapons/Rifle/Sounds/Rifle_Load01.Rifle_Load01", unreal.SoundBase),
    ("/Game/Audio/Sounds/Impacts/Lyra_Plyr_BulletImpact_01.Lyra_Plyr_BulletImpact_01", unreal.SoundBase),
    ("/Game/Audio/Sounds/Impacts/Lyra_EnemyKilled_01.Lyra_EnemyKilled_01", unreal.SoundBase),
    ("/Game/Mini/Targets/M_MiniPracticeTarget.M_MiniPracticeTarget", unreal.Material),
)
BLUEPRINT_SPECS = (
    ("/Game/Mini/UI/B_MiniUIPolicy.B_MiniUIPolicy", unreal.Blueprint),
    ("/Game/Mini/Weapons/Rifle/Animations/ABP_MiniRifleWeapon.ABP_MiniRifleWeapon", unreal.AnimBlueprint),
    ("/Game/Mini/Weapons/Pistol/Animations/ABP_MiniPistolWeapon.ABP_MiniPistolWeapon", unreal.AnimBlueprint),
)


def require_asset(path, asset_type):
    asset = unreal.load_asset(path)
    if asset is None or asset.get_path_name() != path or not isinstance(asset, asset_type):
        raise RuntimeError(f"Saved cook dependency is missing or has the wrong type: {path}")
    return asset


def require_class(path):
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError(f"Required class does not load: {path}")
    return cls


def paths_of(values):
    if any(value is None for value in values):
        raise RuntimeError("Cook label contains an unresolved explicit reference")
    return [value.get_path_name() for value in values]


def check_default(class_name, property_name, expected_path):
    cdo = unreal.get_default_object(require_class(f"/Script/FPS.{class_name}"))
    if not unreal.MiniTask20TrainingAssetLibrary.verify_soft_default(cdo, property_name, expected_path):
        raise RuntimeError(f"Cook manifest does not match {class_name}.{property_name}: expected {expected_path}")


registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(
    ["/Game/Mini", "/Game/Audio/SoundWaves/Weapons", "/Game/Audio/Sounds/Impacts", "/Game/Weapons/Rifle/Sounds"],
    force_rescan=True,
)
for path, asset_type in ASSET_SPECS:
    require_asset(path, asset_type)
blueprints = [require_asset(path, asset_type) for path, asset_type in BLUEPRINT_SPECS]
for path, _ in BLUEPRINT_SPECS:
    require_class(f"{path}_C")
label = require_asset(LABEL_PATH, unreal.PrimaryAssetLabel)
if label.get_class().get_path_name() != "/Script/Engine.PrimaryAssetLabel":
    raise RuntimeError("Cook label must be a native PrimaryAssetLabel data asset")
if (label.get_editor_property("label_assets_in_my_directory") or
        label.get_editor_property("is_runtime_label") or label.get_editor_property("include_redirectors")):
    raise RuntimeError("Cook label must keep directory labeling, runtime label and redirector inclusion disabled")
if paths_of(label.get_editor_property("explicit_assets")) != [path for path, _ in ASSET_SPECS]:
    raise RuntimeError("Saved cook label has missing, duplicate, extra or reordered explicit assets")
if paths_of(label.get_editor_property("explicit_blueprints")) != [f"{path}_C" for path, _ in BLUEPRINT_SPECS]:
    raise RuntimeError("Saved cook label must explicitly label only the policy and two weapon AnimBP classes")
rules = label.get_editor_property("rules")
if (rules.get_editor_property("priority") != 1 or rules.get_editor_property("chunk_id") != -1 or
        not rules.get_editor_property("apply_recursively") or
        rules.get_editor_property("cook_rule") != unreal.PrimaryAssetCookRule.ALWAYS_COOK):
    raise RuntimeError("Cook label must recursively AlwaysCook its explicit references with priority 1")

# A label type marked editor-only manages chunks but does not force its otherwise
# unreferenced bundles into the cook in UE 5.8. The label UObject itself remains
# editor-only because is_runtime_label is false.
if not unreal.MiniTask20TrainingAssetLibrary.verify_cook_scan():
    raise RuntimeError("PrimaryAssetLabel scan must cover only Packaging, use is_editor_only=False and recurse")

check_default("MiniUIManagerSubsystem", "DefaultUIPolicyClass", f"{BLUEPRINT_SPECS[0][0]}_C")
for index, weapon in enumerate(("Rifle", "Pistol")):
    check_default(f"Mini{weapon}EquipmentDefinition", "WeaponMesh", ASSET_SPECS[index][0])
    check_default(f"Mini{weapon}EquipmentDefinition", "WeaponAnimClass", f"{BLUEPRINT_SPECS[index + 1][0]}_C")
feedback_fields = (
    ("RifleFireMontage", 2), ("PistolFireMontage", 3),
    ("RifleReloadMontage", 4), ("PistolReloadMontage", 5),
    ("RifleFireSound", 6), ("PistolFireSound", 7), ("ImpactSound", 8),
    ("ReloadSound", 9), ("DamageSound", 10), ("DeathSound", 11),
)
for property_name, index in feedback_fields:
    check_default("MiniCombatFeedbackComponent", property_name, ASSET_SPECS[index][0])
check_default("MiniPracticeSupply", "StationMaterial", ASSET_SPECS[12][0])
check_default("MiniPracticeTargetDefinition", "BoardMaterial", ASSET_SPECS[12][0])

if not unreal.MiniTask19AssetSetupLibrary.verify_policy_blueprint(blueprints[0]):
    raise RuntimeError("Cooked policy Blueprint must retain its compiled native root layout")
for blueprint in blueprints[1:]:
    if not unreal.MiniTask18AnimAssetLibrary.verify_weapon_anim(blueprint, "DefaultSlot"):
        raise RuntimeError(f"Weapon AnimBP must retain its compiled combat slot: {blueprint.get_path_name()}")
target_path = "/Game/Mini/Targets/BP_MiniPracticeTarget.BP_MiniPracticeTarget"
definition_path = "/Game/Mini/Targets/DA_MiniPracticeTarget.DA_MiniPracticeTarget"
target = require_asset(target_path, unreal.Blueprint)
definition = require_asset(definition_path, unreal.MiniPracticeTargetDefinition)
if (not unreal.MiniTask20TrainingAssetLibrary.verify_target_blueprint(target) or
        not unreal.MiniTask20TrainingAssetLibrary.verify_target_definition(definition) or
        not unreal.MiniTask20TrainingAssetLibrary.verify_target_material(require_asset(ASSET_SPECS[12][0], unreal.Material))):
    raise RuntimeError("Training Blueprint, definition or material failed saved-default validation")

options = unreal.AssetRegistryDependencyOptions(
    include_hard_package_references=True,
    include_soft_package_references=True,
    include_searchable_names=False,
    include_hard_management_references=False,
    include_soft_management_references=False,
)
label_dependencies = {str(path) for path in registry.get_dependencies(LABEL_PATH.split(".", 1)[0], options) or []}
expected_packages = {path.split(".", 1)[0] for path, _ in ASSET_SPECS + BLUEPRINT_SPECS}
if not expected_packages.issubset(label_dependencies):
    raise RuntimeError(f"Saved label lost explicit package references: {sorted(expected_packages - label_dependencies)}")
for source_path, referenced_path in (
    ("/Game/Mini/System/ActionSets/DA_MiniPracticeActionSet", target_path),
    (target_path, definition_path),
    (definition_path, ASSET_SPECS[12][0]),
):
    source_package = source_path.split(".", 1)[0]
    referenced_package = referenced_path.split(".", 1)[0]
    dependencies = {str(path) for path in registry.get_dependencies(source_package, options) or []}
    if referenced_package not in dependencies:
        raise RuntimeError(f"Saved training cook chain is broken: {source_package} -> {referenced_package}")

unreal.log("MINI_TASK20_COOK_ASSETS_VERIFIED ExplicitAssets=13 ExplicitBlueprints=3 DirectoryLabel=0 SavedTrainingChain=1")
