"""Read saved runtime variants without editing production or diagnostic assets."""

import unreal


SYSTEM = "/Game/Mini/System"
DIAGNOSTICS = "/Game/Mini/Diagnostics"
COPY_FIELDS = (
    "pawn_class", "ability_sets", "tag_relationship_mapping", "input_config",
    "default_camera_mode", "aim_camera_mode",
)
VARIANTS = (
    ("RifleOnly", "DA_MiniRifleOnlyLoadout", "verify_rifle_only_loadout"),
    ("Unarmed", "DA_MiniUnarmedLoadout", "verify_unarmed_loadout"),
)


def require(path, class_path):
    asset = unreal.load_asset(path)
    if asset is None or asset.get_class().get_path_name() != class_path:
        raise RuntimeError(f"Saved configuration fixture is missing or invalid: {path}")
    return asset


pawn = require(f"{SYSTEM}/PawnData/DA_MiniPracticePawnData.DA_MiniPracticePawnData", "/Script/FPS.MiniPawnData")
practice = require(f"{SYSTEM}/Experiences/DA_MiniPracticeExperience.DA_MiniPracticeExperience", "/Script/FPS.MiniExperienceDefinition")
combat = require(f"{SYSTEM}/ActionSets/DA_MiniCombatActionSet.DA_MiniCombatActionSet", "/Script/FPS.MiniExperienceActionSet")
practice_set = require(f"{SYSTEM}/ActionSets/DA_MiniPracticeActionSet.DA_MiniPracticeActionSet", "/Script/FPS.MiniExperienceActionSet")
shared = require(f"{SYSTEM}/Loadouts/DA_MiniSharedCombatLoadout.DA_MiniSharedCombatLoadout", "/Script/FPS.MiniLoadoutDefinition")
if (practice.get_editor_property("default_pawn_data") != pawn or
        list(practice.get_editor_property("action_sets")) != [combat, practice_set] or
        pawn.get_editor_property("default_loadout") != shared):
    raise RuntimeError("Configuration fixtures changed production's Practice/Combat/loadout composition")
if not unreal.MiniTask20LoadoutAssetLibrary.verify_shared_loadout(shared):
    raise RuntimeError("Production no longer uses its two-slot shared loadout")

for variant, loadout_name, verify_name in VARIANTS:
    loadout = require(f"{SYSTEM}/Loadouts/{loadout_name}.{loadout_name}", "/Script/FPS.MiniLoadoutDefinition")
    variant_pawn = require(f"{DIAGNOSTICS}/PawnData/DA_Mini{variant}PawnData.DA_Mini{variant}PawnData", "/Script/FPS.MiniPawnData")
    experience = require(f"{DIAGNOSTICS}/Experiences/DA_Mini{variant}Experience.DA_Mini{variant}Experience", "/Script/FPS.MiniExperienceDefinition")
    if (experience.get_editor_property("default_pawn_data") != variant_pawn or
            list(experience.get_editor_property("action_sets")) != [combat] or
            list(experience.get_editor_property("actions")) or
            list(experience.get_editor_property("game_features_to_enable")) != ["MiniShooterCore"] or
            variant_pawn.get_editor_property("default_loadout") != loadout):
        raise RuntimeError(f"Variant lost its combat-only fixture configuration: {variant}")
    for field in COPY_FIELDS:
        if variant_pawn.get_editor_property(field) != pawn.get_editor_property(field):
            raise RuntimeError(f"{variant} uses a probe or differs from production on {field}")
    if not getattr(unreal.MiniTask20LoadoutAssetLibrary, verify_name)(loadout):
        raise RuntimeError(f"Saved variant loadout failed native validation: {variant}")

unreal.log("MINI_TASK20_CONFIG_VARIANTS_VERIFIED RifleOnly=1 Unarmed=1 PracticeActors=0 ProductionPreserved=1")
