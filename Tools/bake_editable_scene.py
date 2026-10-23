"""Populate the saved level with selectable meshes that the runtime puzzle reuses."""

import unreal


level = '/Game/RoomShift/RoomShift'
assets_root = '/Game/RoomShift/EditableMaterials'
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if not levels.load_level(level):
    raise RuntimeError('Could not load ' + level)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
actors = unreal.EditorLevelLibrary.get_all_level_actors()
if any('RS_AUTHORED' in [str(tag) for tag in actor.get_editor_property('tags')] for actor in actors):
    raise RuntimeError('Editable scene already exists; refusing to replace editor changes')

puzzle_class = unreal.load_class(None, '/Script/RoomShift.RoomShiftPuzzle')
if not puzzle_class:
    raise RuntimeError('RoomShift C++ module is not loaded')
puzzle = unreal.EditorLevelLibrary.spawn_actor_from_class(puzzle_class, unreal.Vector(0, 0, 0))
if not puzzle:
    raise RuntimeError('Could not spawn RoomShiftPuzzle')
puzzle.set_actor_label('RoomShiftPuzzle — 编辑旋转轴中心')
puzzle.bake_editable_scene()

unreal.EditorAssetLibrary.make_directory(assets_root)
tool = unreal.AssetToolsHelpers.get_asset_tools()
material_lib = unreal.MaterialEditingLibrary
instances = {}
mesh_count = 0
for actor in unreal.EditorLevelLibrary.get_all_level_actors():
    if 'RS_AUTHORED' not in [str(tag) for tag in actor.get_editor_property('tags')]:
        continue
    component = actor.get_component_by_class(unreal.StaticMeshComponent)
    material = component.get_material(0) if component else None
    if not material:
        raise RuntimeError('Missing material on ' + actor.get_actor_label())
    parent = material.get_editor_property('parent')
    if not parent:
        raise RuntimeError('Missing material parent on ' + actor.get_actor_label())
    tint = material.get_vector_parameter_value('Tint')
    roughness = material.get_scalar_parameter_value('Roughness')
    metallic = material.get_scalar_parameter_value('Metallic')
    key = (parent.get_path_name(), round(tint.r, 5), round(tint.g, 5), round(tint.b, 5), round(roughness, 5), round(metallic, 5))
    if key not in instances:
        name = 'MI_Editable_%03d' % len(instances)
        instance = tool.create_asset(name, assets_root, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        instance.set_editor_property('parent', parent)
        material_lib.set_material_instance_vector_parameter_value(instance, 'Tint', tint)
        material_lib.set_material_instance_scalar_parameter_value(instance, 'Roughness', roughness)
        material_lib.set_material_instance_scalar_parameter_value(instance, 'Metallic', metallic)
        unreal.EditorAssetLibrary.save_loaded_asset(instance)
        instances[key] = instance
    component.set_material(0, instances[key])
    mesh_count += 1

if mesh_count < 400:
    raise RuntimeError('Only %d editable meshes were generated' % mesh_count)
if not levels.save_current_level():
    raise RuntimeError('Could not save editable RoomShift level')
unreal.log('ROOMSHIFT_EDITABLE_SCENE_READY meshes=%d materials=%d' % (mesh_count, len(instances)))
