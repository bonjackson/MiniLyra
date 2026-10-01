"""Author the task 20 target assets before Assembly, Loadout and map authoring.

Run after building FPSEditor. Re-running repairs only the three owned assets in
/Game/Mini/Targets; it never adds map actors or changes Experience Actions.
"""

import unreal


TARGET_FOLDER = "/Game/Mini/Targets"
MATERIAL_NAME = "M_MiniPracticeTarget"
DEFINITION_NAME = "DA_MiniPracticeTarget"
BLUEPRINT_NAME = "BP_MiniPracticeTarget"
ACTIVE_COLOR = unreal.LinearColor(0.02, 0.65, 1.0, 1.0)
DISABLED_COLOR = unreal.LinearColor(1.0, 0.25, 0.05, 1.0)


def object_path(name):
    return f"{TARGET_FOLDER}/{name}.{name}"


def require_class(path):
    cls = unreal.load_class(None, path)
    if cls is None:
        raise RuntimeError(f"Missing task 20 class; build FPSEditor first: {path}")
    return cls


def existing_asset(name, expected_class):
    path = object_path(name)
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        return None
    asset = unreal.load_asset(path)
    if asset is None or asset.get_class() != expected_class:
        raise RuntimeError(f"Existing training asset is missing or has the wrong class: {path}")
    return asset


def create_asset(name, cls, factory):
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, TARGET_FOLDER, cls, factory)
    if asset is None:
        raise RuntimeError(f"Could not create training asset: {object_path(name)}")
    return asset


target_class = require_class("/Script/FPS.MiniPracticeTarget")
definition_class = require_class("/Script/FPS.MiniPracticeTargetDefinition")
require_class("/Script/FPS.MiniTask20TrainingAssetLibrary")
library = unreal.MiniTask20TrainingAssetLibrary
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.scan_paths_synchronous([TARGET_FOLDER], force_rescan=True)

# Preflight existing types before authoring any owned asset.
material = existing_asset(MATERIAL_NAME, unreal.Material.static_class())
definition = existing_asset(DEFINITION_NAME, definition_class)
blueprint = existing_asset(BLUEPRINT_NAME, unreal.Blueprint.static_class())
# UBlueprint.ParentClass is not exposed to UE 5.8 Python. The native
# EnsureTargetBlueprint bridge checks the direct parent before changing its CDO.

if material is None:
    material = create_asset(MATERIAL_NAME, unreal.Material, unreal.MaterialFactoryNew())
if not library.verify_target_material(material):
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property("use_material_attributes", False)
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    parameter = unreal.MaterialEditingLibrary.create_material_expression(
        material, unreal.MaterialExpressionVectorParameter, -240, 0
    )
    if parameter is None:
        raise RuntimeError("Could not create TargetColor vector parameter")
    parameter.set_editor_property("parameter_name", "TargetColor")
    parameter.set_editor_property("default_value", ACTIVE_COLOR)
    parameter.set_editor_property("use_custom_primitive_data", False)
    if not unreal.MaterialEditingLibrary.connect_material_property(
        parameter, "", unreal.MaterialProperty.MP_BASE_COLOR
    ):
        raise RuntimeError("Could not connect TargetColor to the target material's BaseColor")
    compile_errors = unreal.MaterialEditingLibrary.recompile_material(material)
    if compile_errors:
        raise RuntimeError(f"Target material compilation failed: {list(compile_errors)}")
if not library.verify_target_material(material):
    raise RuntimeError("Target material must be DefaultLit with TargetColor feeding BaseColor")

if definition is None:
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", definition_class)
    definition = create_asset(DEFINITION_NAME, definition_class, factory)
if not library.verify_target_definition(definition):
    definition.set_editor_property("display_name", unreal.Text("训练靶"))
    definition.set_editor_property("max_health", 100.0)
    definition.set_editor_property("reset_delay", 2.0)
    definition.set_editor_property("board_material", material)
    definition.set_editor_property("active_color", ACTIVE_COLOR)
    definition.set_editor_property("disabled_color", DISABLED_COLOR)
if not library.verify_target_definition(definition):
    raise RuntimeError("Target definition must contain 100 HP, 2 s reset, the board material and colors")

if blueprint is None:
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", target_class)
    blueprint = create_asset(BLUEPRINT_NAME, unreal.Blueprint, factory)
if not library.ensure_target_blueprint(blueprint, definition):
    raise RuntimeError("Could not compile BP_MiniPracticeTarget with the saved target definition on its CDO")

for asset in (material, definition, blueprint):
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset.get_path_name()}")
if not library.verify_target_blueprint(blueprint):
    raise RuntimeError("Training target Blueprint failed verification after saving")

unreal.log(f"MINI_TASK20_TRAINING_MATERIAL={material.get_path_name()}")
unreal.log(f"MINI_TASK20_TRAINING_DEFINITION={definition.get_path_name()}")
unreal.log(f"MINI_TASK20_TRAINING_BLUEPRINT={blueprint.get_path_name()}")
unreal.log("MINI_TASK20_TRAINING_ASSETS_CREATED Material=1 Definition=1 Blueprint=1 HP=100 ResetSeconds=2")
