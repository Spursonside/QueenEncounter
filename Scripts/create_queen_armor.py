"""Create original armor/core materials for the primitive Queen blockout."""
import unreal

assets = unreal.AssetToolsHelpers.get_asset_tools()
edit = unreal.MaterialEditingLibrary
for name, color, metallic, roughness, emission in [
    ('M_QueenArmor', (0.23, 0.20, 0.155), 0.65, 0.48, 0),
    ('M_QueenRed', (0.55, 0.015, 0.005), 0.25, 0.25, 5),
]:
    path = '/Game/Materials/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        continue
    mat = assets.create_asset(name, '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
    base = edit.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
    base.set_editor_property('constant', unreal.LinearColor(*color, 1))
    edit.connect_material_property(base, '', unreal.MaterialProperty.MP_BASE_COLOR)
    for value, slot in [(metallic, unreal.MaterialProperty.MP_METALLIC), (roughness, unreal.MaterialProperty.MP_ROUGHNESS)]:
        node = edit.create_material_expression(mat, unreal.MaterialExpressionConstant)
        node.set_editor_property('r', value)
        edit.connect_material_property(node, '', slot)
    if emission:
        glow = edit.create_material_expression(mat, unreal.MaterialExpressionConstant3Vector)
        glow.set_editor_property('constant', unreal.LinearColor(*(v * emission for v in color), 1))
        edit.connect_material_property(glow, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    edit.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
unreal.log('QUEEN_ARMOR_MATERIALS_READY')
