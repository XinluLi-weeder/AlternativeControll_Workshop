import unreal

root = '/Game/RoomShift'
unreal.EditorAssetLibrary.make_directory(root)
tools = unreal.AssetToolsHelpers.get_asset_tools()
material = unreal.load_asset(root + '/M_Surface')
if material is None:
    material = tools.create_asset('M_Surface', root, unreal.Material, unreal.MaterialFactoryNew())
    color = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -300, 0)
    color.set_editor_property('parameter_name', 'Tint')
    color.set_editor_property('default_value', unreal.LinearColor(.4, .5, .6, 1))
    unreal.MaterialEditingLibrary.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    rough = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionConstant, -300, 160)
    rough.set_editor_property('r', .65)
    unreal.MaterialEditingLibrary.connect_material_property(rough, '', unreal.MaterialProperty.MP_ROUGHNESS)
    unreal.MaterialEditingLibrary.recompile_material(material)
    unreal.EditorAssetLibrary.save_loaded_asset(material)

level = root + '/RoomShift'
if not unreal.EditorAssetLibrary.does_asset_exist(level):
    subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not subsystem.new_level(level):
        raise RuntimeError('Could not create RoomShift level')
    # Runtime GameMode constructs the room, puzzle, camera and lights at Play.
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode', unreal.load_class(None, '/Script/RoomShift.RoomShiftMode'))
    subsystem.save_current_level()
unreal.log('ROOMSHIFT_ASSETS_READY')
