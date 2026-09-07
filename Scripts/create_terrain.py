"""Generate a reproducible low-resolution Landscape in a separate test map."""
import unreal

assets = unreal.AssetToolsHelpers.get_asset_tools()
mat = unreal.EditorAssetLibrary.load_asset('/Game/Materials/M_Terrain') if unreal.EditorAssetLibrary.does_asset_exist('/Game/Materials/M_Terrain') else None
if not mat:
    mat = assets.create_asset('M_Terrain', '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
    edit = unreal.MaterialEditingLibrary
    def node(cls):
        return edit.create_material_expression(mat, cls)
    def color(rgb):
        n = node(unreal.MaterialExpressionConstant3Vector)
        n.set_editor_property('constant', unreal.LinearColor(*rgb, 1))
        return n
    position = node(unreal.MaterialExpressionWorldPosition)
    noise = node(unreal.MaterialExpressionNoise)
    noise.set_editor_property('scale', 0.0015)
    noise.set_editor_property('levels', 3)
    noise.set_editor_property('output_min', 0.0)
    noise.set_editor_property('output_max', 1.0)
    edit.connect_material_expressions(position, '', noise, 'Position')
    mix = node(unreal.MaterialExpressionLinearInterpolate)
    edit.connect_material_expressions(color((0.12, 0.16, 0.075)), '', mix, 'A')
    edit.connect_material_expressions(color((0.32, 0.25, 0.15)), '', mix, 'B')
    edit.connect_material_expressions(noise, '', mix, 'Alpha')
    edit.connect_material_property(mix, '', unreal.MaterialProperty.MP_BASE_COLOR)
    rough = node(unreal.MaterialExpressionConstant)
    rough.set_editor_property('r', 0.95)
    edit.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    edit.recompile_material(mat)
    unreal.EditorAssetLibrary.save_loaded_asset(mat)

level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
path = '/Game/Maps/ArthropodTerrain' if '-QueenArthropodTerrain' in unreal.SystemLibrary.get_command_line() else '/Game/Maps/NoiseTerrain'
if unreal.EditorAssetLibrary.does_asset_exist(path):
    level.load_level(path)
    if '-QueenRegenerateTerrain' in unreal.SystemLibrary.get_command_line():
        actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        for actor in actors.get_all_level_actors():
            if actor.get_actor_label().startswith('Queen_NoiseLandscape_Seed'):
                actors.destroy_actor(actor)
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
        if not unreal.QueenTerrainLibrary.create_test_landscape(world, 20260906):
            raise RuntimeError('Landscape regeneration failed')
        level.save_current_level()
else:
    level.new_level(path)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not unreal.QueenTerrainLibrary.create_test_landscape(world, 20260906):
        raise RuntimeError('Landscape generation failed')
    level.save_current_level()
unreal.log('QUEEN_NOISE_TERRAIN_READY')

