"""Run once using UnrealEditor-Cmd -run=pythonscript -script=<this file>."""
import unreal

assets = unreal.AssetToolsHelpers.get_asset_tools()
palette = {
    "M_Metal": ((0.065, 0.085, 0.105), 0.8, 0.35, 0.0),
    "M_Amber": ((1.0, 0.24, 0.025), 0.35, 0.3, 2.5),
    "M_Ground": ((0.12, 0.155, 0.17), 0.1, 0.95, 0.0),
    "M_Concrete": ((0.29, 0.32, 0.33), 0.05, 0.8, 0.0),
}
for name, (color, metal, rough, emissive) in palette.items():
    path = "/Game/Materials/" + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        continue
    mat = assets.create_asset(name, "/Game/Materials", unreal.Material, unreal.MaterialFactoryNew())
    rgb = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
    rgb.set_editor_property("constant", unreal.LinearColor(*color, 1.0))
    unreal.MaterialEditingLibrary.connect_material_property(rgb, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for value, prop in [(metal, unreal.MaterialProperty.MP_METALLIC), (rough, unreal.MaterialProperty.MP_ROUGHNESS)]:
        scalar = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant)
        scalar.set_editor_property("r", value)
        unreal.MaterialEditingLibrary.connect_material_property(scalar, "", prop)
    if emissive:
        glow = unreal.MaterialEditingLibrary.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
        glow.set_editor_property("constant", unreal.LinearColor(*(c * emissive for c in color), 1.0))
        unreal.MaterialEditingLibrary.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not unreal.EditorAssetLibrary.does_asset_exist("/Game/Maps/Arena"):
    level.new_level("/Game/Maps/Arena")
    level.save_current_level()
unreal.log("QUEEN_ASSETS_READY")
