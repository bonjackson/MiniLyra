"""Read-only checks of saved target assets in a fresh Unreal Editor process.

This script never compiles a Blueprint, recompiles a material, repairs a field,
creates an asset or saves a package. Run it after Task20TrainingAssets.py.
"""

import unreal


TARGET_FOLDER = "/Game/Mini/Targets"
MATERIAL_PATH = f"{TARGET_FOLDER}/M_MiniPracticeTarget.M_MiniPracticeTarget"
DEFINITION_PATH = f"{TARGET_FOLDER}/DA_MiniPracticeTarget.DA_MiniPracticeTarget"
BLUEPRINT_PATH = f"{TARGET_FOLDER}/BP_MiniPracticeTarget.BP_MiniPracticeTarget"


def require_asset(path, class_path):
    asset = unreal.load_asset(path)
    if asset is None or asset.get_path_name() != path or asset.get_class().get_path_name() != class_path:
        raise RuntimeError(f"Saved training asset is missing or has the wrong type: {path}")
    return asset


registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous([TARGET_FOLDER], force_rescan=True)
material = require_asset(MATERIAL_PATH, "/Script/Engine.Material")
definition = require_asset(DEFINITION_PATH, "/Script/FPS.MiniPracticeTargetDefinition")
blueprint = require_asset(BLUEPRINT_PATH, "/Script/Engine.Blueprint")
library = unreal.MiniTask20TrainingAssetLibrary

if not library.verify_target_material(material):
    raise RuntimeError("Saved material must be DefaultLit with a TargetColor vector parameter connected to BaseColor")
if not library.verify_target_definition(definition):
    raise RuntimeError("Saved target definition lost its 100 HP, 2 s reset, material reference or colors")
if not library.verify_target_blueprint(blueprint):
    raise RuntimeError("Saved target Blueprint has the wrong type, parent, compilation status or CDO definition")
target_class = unreal.load_class(None, f"{BLUEPRINT_PATH}_C")
if target_class is None:
    raise RuntimeError("Saved target Blueprint generated class does not load")
target_cdo = unreal.get_default_object(target_class)
if target_cdo.get_editor_property("target_definition") != definition:
    raise RuntimeError("Saved target Blueprint CDO does not reference DA_MiniPracticeTarget")

options = unreal.AssetRegistryDependencyOptions(
    include_hard_package_references=True,
    include_soft_package_references=True,
    include_searchable_names=False,
    include_hard_management_references=False,
    include_soft_management_references=False,
)
for source_path, referenced_path in (
    (BLUEPRINT_PATH, DEFINITION_PATH),
    (DEFINITION_PATH, MATERIAL_PATH),
):
    source_package = source_path.split(".", 1)[0]
    referenced_package = referenced_path.split(".", 1)[0]
    dependencies = {str(path) for path in registry.get_dependencies(source_package, options) or []}
    if referenced_package not in dependencies:
        raise RuntimeError(f"Saved package lost its required reference: {source_package} -> {referenced_package}")

unreal.log("MINI_TASK20_TRAINING_ASSETS_VERIFIED Material=1 Definition=1 Blueprint=1 HP=100 ResetSeconds=2")
