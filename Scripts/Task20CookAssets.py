"""Create a precise cook label for native/config soft references used in practice.

Run after Training, Assembly, Loadout and map authoring. The label covers only
the production UI policy, two weapon meshes, two weapon AnimBPs, four montages,
six sounds and the shared target/supply material. Their saved dependencies cook
recursively; directory labeling stays disabled.
"""

import unreal


LABEL_FOLDER = "/Game/Mini/System/Packaging"
LABEL_NAME = "DA_MiniPracticeCook"
LABEL_PATH = f"{LABEL_FOLDER}/{LABEL_NAME}.{LABEL_NAME}"
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
        raise RuntimeError(f"Cook label dependency is missing or has the wrong type: {path}")
    return asset


def paths_of(values):
    if any(value is None for value in values):
        raise RuntimeError("Cook label contains an unresolved explicit reference")
    return [value.get_path_name() for value in values]


registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(
    ["/Game/Mini", "/Game/Audio/SoundWaves/Weapons", "/Game/Audio/Sounds/Impacts", "/Game/Weapons/Rifle/Sounds"],
    force_rescan=True,
)
assets = [require_asset(path, asset_type) for path, asset_type in ASSET_SPECS]
blueprint_classes = []
for path, asset_type in BLUEPRINT_SPECS:
    require_asset(path, asset_type)
    cls = unreal.load_class(None, f"{path}_C")
    if cls is None:
        raise RuntimeError(f"Cook label Blueprint class does not load: {path}_C")
    blueprint_classes.append(cls)

label = unreal.load_asset(LABEL_PATH)
if label is None:
    if unreal.EditorAssetLibrary.does_asset_exist(LABEL_PATH):
        raise RuntimeError(f"Existing cook label failed to load: {LABEL_PATH}")
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.PrimaryAssetLabel.static_class())
    label = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        LABEL_NAME, LABEL_FOLDER, unreal.PrimaryAssetLabel, factory
    )
if label is None or label.get_class().get_path_name() != "/Script/Engine.PrimaryAssetLabel":
    raise RuntimeError(f"Cook label is missing or has the wrong native class: {LABEL_PATH}")

label.set_editor_property("label_assets_in_my_directory", False)
label.set_editor_property("is_runtime_label", False)
label.set_editor_property("include_redirectors", False)
label.set_editor_property("explicit_assets", assets)
label.set_editor_property("explicit_blueprints", blueprint_classes)
rules = unreal.PrimaryAssetRules()
rules.set_editor_property("priority", 1)
rules.set_editor_property("chunk_id", -1)
rules.set_editor_property("apply_recursively", True)
rules.set_editor_property("cook_rule", unreal.PrimaryAssetCookRule.ALWAYS_COOK)
label.set_editor_property("rules", rules)
if not unreal.EditorAssetLibrary.save_loaded_asset(label, only_if_is_dirty=False):
    raise RuntimeError(f"Could not save {LABEL_PATH}")
if paths_of(label.get_editor_property("explicit_assets")) != [path for path, _ in ASSET_SPECS]:
    raise RuntimeError("Cook label lost its exact explicit asset list after saving")
if paths_of(label.get_editor_property("explicit_blueprints")) != [f"{path}_C" for path, _ in BLUEPRINT_SPECS]:
    raise RuntimeError("Cook label lost its exact Blueprint class list after saving")

unreal.log(f"MINI_TASK20_COOK_LABEL={LABEL_PATH}")
unreal.log("MINI_TASK20_COOK_ASSETS_CREATED ExplicitAssets=13 ExplicitBlueprints=3 DirectoryLabel=0")
