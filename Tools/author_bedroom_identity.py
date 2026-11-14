"""Author fixed enclosure and restrained face palette; keep a recovery snapshot."""
import unreal as u
from pathlib import Path
from datetime import datetime
import shutil
root=Path(u.Paths.project_dir())
ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
es=u.get_editor_subsystem(u.EditorActorSubsystem)
assert not ls.is_in_play_in_editor()
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith('/Game/Levels/Hallyway1.')
assert ls.save_current_level()
backup=root/'Documentation'/('BedroomIdentityBackup-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
backup.mkdir()
shutil.copy2(root/'Content/Levels/Hallyway1.umap',backup/'Hallyway1.umap')
shutil.copytree(root/'Plugins/RoomShift/Source',backup/'Source')
actors=es.get_all_level_actors()
palette=[(.46,.34,.28),(.28,.40,.34),(.28,.36,.46),(.40,.32,.43),(.49,.43,.32)]
asset_tools=u.AssetToolsHelpers.get_asset_tools()
asset_root='/Game/RoomShift/Bedroom'
u.EditorAssetLibrary.make_directory(asset_root)
parent=u.load_asset('/Game/RoomShift/M_Plaster')
assert parent
materials=[]
for i,color in enumerate(palette):
    path=asset_root+'/MI_Face_'+chr(65+i)
    mat=u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else asset_tools.create_asset('MI_Face_'+chr(65+i),asset_root,u.MaterialInstanceConstant,u.MaterialInstanceConstantFactoryNew())
    u.MaterialEditingLibrary.set_material_instance_parent(mat,parent)
    u.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mat,'Tint',u.LinearColor(*color,1))
    u.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mat,'Roughness',.9)
    u.EditorAssetLibrary.save_loaded_asset(mat)
    materials.append(mat)
for a in actors:
    tags=[str(t) for t in a.tags]
    a.tags=[t for t in a.tags if not str(t).startswith('RH_FOLLOW_FACE_')]
    if 'RH_PANEL' in tags and 'RH_STAGE_1' in tags:
        face=int(next(t[8:] for t in tags if t.startswith('RH_FACE_')))
        component=a.get_component_by_class(u.StaticMeshComponent)
        if component:component.set_material(0,materials[face])

cube=u.load_asset('/Engine/BasicShapes/Cube')
# Fixed outer shell is opaque even when the inner room faces rotate.
walls=[('DoorWall',(815,365,350),(1360,24,900),0),('RightWall',(130,1070,350),(24,1440,900),1),('LeftWall',(1500,1070,350),(24,1440,900),2),('WindowWall',(815,1772,350),(1400,24,900),3),('Ceiling',(815,1070,810),(1400,1440,24),4),('Floor',(815,1070,-105),(1400,1440,24),4)]
for name,position,size,face in walls:
    label='Bedroom_Enclosure_'+name
    a=next((a for a in actors if a.get_actor_label()==label),None)
    if a is None:a=es.spawn_actor_from_class(u.StaticMeshActor,u.Vector(*position))
    a.set_actor_label(label);a.set_folder_path('Bedroom/FixedEnclosure')
    a.tags=[u.Name('RH_STAGE_1'),u.Name('RH_ENCLOSURE')]
    a.static_mesh_component.set_static_mesh(cube)
    a.static_mesh_component.set_material(0,materials[face])
    a.set_actor_location(u.Vector(*position),False,False)
    a.set_actor_scale3d(u.Vector(*[n/100 for n in size]))
    a.set_actor_hidden_in_game(False)

# A fixed door-sized infill in front of the moving divider seals the original aperture.
label='Bedroom_Enclosure_DoorInfill'
a=next((a for a in actors if a.get_actor_label()==label),None)
if a is None:a=es.spawn_actor_from_class(u.StaticMeshActor,u.Vector(920,399,350))
a.set_actor_label(label);a.set_folder_path('Bedroom/FixedEnclosure')
a.tags=[u.Name('RH_STAGE_1'),u.Name('RH_ENCLOSURE')]
a.static_mesh_component.set_static_mesh(cube);a.static_mesh_component.set_material(0,materials[0])
a.set_actor_scale3d(u.Vector(2.8,.12,8.7))
assert ls.save_current_level()
print('BEDROOM_IDENTITY_SAVED',backup)
