"""Author the four full-scale, transform-driven puzzles in Hallyway1."""
import ast, json, math, shutil
from datetime import datetime
from pathlib import Path
import unreal as u

PROJECT=Path(u.Paths.project_dir())
ROOT='/Game/RoomShift/Hallway'  # namespace used by the imported GeometryScript helpers
LEVEL='/Game/Levels/Hallyway1'
PUZZLE_ROOT='/Game/RoomShift/Hallway/Fragments/Hallyway1'
ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
es=u.get_editor_subsystem(u.EditorActorSubsystem)
assert ls.load_level(LEVEL)
actors=es.get_all_level_actors()
by_name={a.get_actor_label():a for a in actors}
directors=[a for a in actors if a.get_class().get_name()=='HallwayPuzzle']
assert len(directors)==1
director=directors[0]
assert 'RH_BEDROOM_PUZZLE_V1' not in [str(t) for t in director.tags]

backup=PROJECT/'Documentation'/('Hallyway1BeforePuzzle-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
backup.mkdir(parents=True,exist_ok=False)
shutil.copy2(PROJECT/'Content/Levels/Hallyway1.umap',backup/'Hallyway1.umap')

tree=ast.parse((PROJECT/'Tools/setup_hallway.py').read_text(encoding='utf-8'))
helpers=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in ('fragment','spawn_mesh','tags')]
exec(compile(ast.Module(body=helpers,type_ignores=[]),'hallyway1_puzzle_helpers','exec'))
u.EditorAssetLibrary.make_directory(PUZZLE_ROOT)

# item id, actor label, display asset name, faces, goals, cut-axis, target mesh-bounds center
specs=[
    (3,'SM_Bed','BedroomBed',[1,2,4],[1,1,2],0,(700,1030,200)),
    (4,'SM_Cabinet_2','BedroomCabinet',[1,4],[1,2],1,(600,800,300)),
    (5,'SM_Coffee_Table_03','BedroomTable',[1,2,4],[1,1,2],0,(1300,1000,250)),
    (8,'window_glass','BedroomWindowPane',[0,3,4],[1,1,2],0,(980,1500,470)),
]
assert all(name in by_name and by_name[name].get_component_by_class(u.StaticMeshComponent).static_mesh for _,name,*_ in specs)

manifest=[]
for item,label,name,faces,goals,axis,target in specs:
    source=by_name[label]
    component=source.get_component_by_class(u.StaticMeshComponent)
    mesh=component.static_mesh
    bounds=mesh.get_bounding_box()
    lo=[bounds.min.x,bounds.min.y,bounds.min.z]
    hi=[bounds.max.x,bounds.max.y,bounds.max.z]
    count=len(faces)
    cuts=[lo[axis]+(hi[axis]-lo[axis])*i/count for i in range(1,count)]
    original_location=source.get_actor_location()
    original_center,_=source.get_actor_bounds(False)
    offset=(original_center.x-original_location.x,original_center.y-original_location.y,original_center.z-original_location.z)
    actor_location=(target[0]-offset[0],target[1]-offset[1],target[2]-offset[2])
    rotation=source.get_actor_rotation()
    scale=source.get_actor_scale3d()
    tags(source,'RH_SOURCE_'+str(item),'RH_STAGE_1')
    for index,(face,goal) in enumerate(zip(faces,goals)):
        path=PUZZLE_ROOT+'/'+name+'_'+str(index)
        if u.EditorAssetLibrary.does_asset_exist(path):
            part_mesh=u.load_asset(path)
        else:
            part_mesh=fragment(mesh,name+'_'+str(index),axis,None if index==0 else cuts[index-1],None if index==count-1 else cuts[index])
        part=spawn_mesh(name+'_Part_'+str(index),part_mesh,actor_location,(scale.x,scale.y,scale.z),rotation)
        for slot in range(component.get_num_materials()):
            part.static_mesh_component.set_material(slot,component.get_material(slot))
        tags(part,'RH_STAGE_1','RH_ITEM_'+str(item),'RH_FACE_'+str(face),'RH_GOAL_'+str(goal))
        part.set_is_temporarily_hidden_in_editor(True)
        part.set_actor_hidden_in_game(True)
        manifest.append({'item':item,'name':name,'source':label,'part':part.get_actor_label(),'mesh':part_mesh.get_path_name(),'face':face,'goal_degrees':goal*90,'cut_axis':'XYZ'[axis],'target_bounds_center':target,'source_scale':[scale.x,scale.y,scale.z]})

# Tag the actual room shell so the moving full-wall surfaces carry trim and frames.
wall_map={
    '卧室侧墙':1,'卧室侧墙2':2,
    'Cube7':0,'Cube8':0,
    'Cube9':3,'Cube10':3,'Cube11':3,'Cube12':3,'Cube13':3,'Cube14':3,'Cube15':3,'Cube16':3,
    '卧室天花板':4,
}
for label,face in wall_map.items():
    actor=by_name[label]
    tags(actor,'RH_PANEL','RH_STAGE_1','RH_FACE_'+str(face),'RH_WALL_DECOR')
    component=actor.get_component_by_class(u.StaticMeshComponent)
    if component:component.set_mobility(u.ComponentMobility.MOVABLE)

director.set_editor_property('start_in_bedroom',True)
director.set_editor_property('bedroom_camera',u.Vector(980,920,145))
director.set_editor_property('field_of_view',120.)
tags(director,'RH_BEDROOM_PUZZLE_V1')
u.EditorAssetLibrary.save_directory('/Game/RoomShift/Hallway',only_if_is_dirty=True,recursive=True)
assert ls.save_current_level()
(PROJECT/'Documentation'/'Hallyway1BedroomPuzzle.json').write_text(json.dumps({'level':LEVEL,'camera':[980,920,145],'fov':120,'items':manifest,'rotating_walls':wall_map},indent=2,ensure_ascii=False),encoding='utf-8')
u.log('HALLYWAY1_BEDROOM_PUZZLE_COMPLETE items=5 fragments=%d backup=%s'%(len(manifest),backup))
