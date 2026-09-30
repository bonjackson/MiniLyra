"""Verify saved Task 18 montage and AnimBP references in a fresh editor process."""

import unreal


SLOT = "DefaultSlot"
BODY_FOLDER = "/Game/Mini/Characters/Mannequins/Anims"
body = unreal.EditorAssetLibrary.load_asset(f"{BODY_FOLDER}/ABP_MiniPractice")
if not isinstance(body, unreal.AnimBlueprint):
    raise RuntimeError("Missing body animation Blueprint")

body_sequences = [
    f"{BODY_FOLDER}/Rifle/MF_Rifle_Idle_ADS",
    f"{BODY_FOLDER}/Rifle/Jog/MF_Rifle_Jog_Fwd",
    f"{BODY_FOLDER}/Unarmed/Jump/MM_Jump",
    f"{BODY_FOLDER}/Unarmed/Jump/MM_Fall_Loop",
    f"{BODY_FOLDER}/Unarmed/Jump/MM_Land",
]
sequences = [unreal.EditorAssetLibrary.load_asset(path) for path in body_sequences]
if any(not isinstance(seq, unreal.AnimSequence) for seq in sequences):
    raise RuntimeError("Body locomotion sequence is missing")
if not unreal.MiniTask11AnimAssetLibrary.verify_practice_anim(body, *sequences):
    raise RuntimeError("Task 11 body locomotion graph has regressed")
if not unreal.MiniTask18AnimAssetLibrary.verify_character_slot(body, SLOT):
    raise RuntimeError("Saved body combat slot does not follow the locomotion graph")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous(["/Game/Mini/Characters", "/Game/Mini/Weapons"], True)
deps = unreal.AssetRegistryDependencyOptions(
    include_hard_package_references=True,
    include_soft_package_references=True,
    include_searchable_names=False,
    include_hard_management_references=False,
    include_soft_management_references=False,
)


def reject_legacy_dependencies(package):
    legacy = [
        str(path)
        for path in (registry.get_dependencies(package, deps) or [])
        if str(path).startswith(("/Game/Weapons/", "/Script/LyraGame"))
    ]
    if legacy:
        raise RuntimeError(f"Stale Lyra dependency on {package}: {legacy}")


reject_legacy_dependencies(f"{BODY_FOLDER}/ABP_MiniPractice")

for weapon in ("Rifle", "Pistol"):
    folder = f"/Game/Mini/Weapons/{weapon}/Animations"
    skeleton_path = f"/Game/Mini/Weapons/{weapon}/Mesh/SK_{weapon}_Skeleton"
    skeleton = unreal.EditorAssetLibrary.load_asset(skeleton_path)
    anim_bp = unreal.EditorAssetLibrary.load_asset(f"{folder}/ABP_Mini{weapon}Weapon")
    if not isinstance(skeleton, unreal.Skeleton) or not isinstance(anim_bp, unreal.AnimBlueprint):
        raise RuntimeError(f"Missing saved {weapon} weapon skeleton or AnimBP")
    if anim_bp.get_editor_property("target_skeleton") != skeleton:
        raise RuntimeError(f"{weapon} weapon AnimBP uses a different skeleton")
    if not unreal.MiniTask18AnimAssetLibrary.verify_weapon_anim(anim_bp, SLOT):
        raise RuntimeError(f"Saved {weapon} weapon slot AnimGraph is invalid")
    reject_legacy_dependencies(f"{folder}/ABP_Mini{weapon}Weapon")

    for action in ("Fire", "Reload"):
        sequence_path = f"{folder}/Weap_{weapon}_{action}"
        montage_path = f"{folder}/AM_Mini{weapon}_{action}"
        sequence = unreal.EditorAssetLibrary.load_asset(sequence_path)
        montage = unreal.EditorAssetLibrary.load_asset(montage_path)
        if not isinstance(sequence, unreal.AnimSequence) or not isinstance(montage, unreal.AnimMontage):
            raise RuntimeError(f"Missing saved {weapon} {action} sequence/montage")
        if sequence.get_editor_property("skeleton") != skeleton:
            raise RuntimeError(f"Saved {weapon} {action} sequence has a stale skeleton")
        if not unreal.MiniTask18AnimAssetLibrary.verify_montage(montage, sequence, SLOT):
            raise RuntimeError(f"Saved {weapon} {action} montage has an invalid track/slot")
        for package in (sequence_path, montage_path):
            reject_legacy_dependencies(package)
        unreal.log(f"MINI_TASK18_ANIMATION_OK Weapon={weapon} Action={action} Slot={SLOT}")

unreal.log("MINI_TASK18_ANIMATION_ASSETS_VERIFIED Slot=DefaultSlot Montages=4")
