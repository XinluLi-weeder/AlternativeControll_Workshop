"""Create a separate editable five-face level, preserving the original map and imported source assets.

Run once with UnrealEditor-Cmd -run=pythonscript -script=... .
The refusal to overwrite is intentional: subsequent artistic edits belong in the editor.
"""
import math
import json
from pathlib import Path
import unreal as ue

ROOT='/Game/RoomShift/Studio'
LEVEL='/Game/RoomShift/RoomShiftStudio'
levels=ue.get_editor_subsystem(ue.LevelEditorSubsystem)
actors=ue.get_editor_subsystem(ue.EditorActorSubsystem)
assets=ue.AssetToolsHelpers.get_asset_tools()
lib=ue.MaterialEditingLibrary
if ue.EditorAssetLibrary.does_asset_exist(LEVEL):
    levels.load_level(LEVEL)
    if any('RS_AUTHORED' in [str(t) for t in a.tags] for a in actors.get_all_level_actors()):
        raise RuntimeError('Studio map already exists; preserve editor edits instead of regenerating it.')
ue.EditorAssetLibrary.make_directory(ROOT)
if not ue.EditorAssetLibrary.does_asset_exist(LEVEL) and not levels.new_level(LEVEL):
    raise RuntimeError('Cannot create Studio map')

def instance(name,parent,tint=None,rough=None):
    path=ROOT+'/'+name
    m=ue.load_asset(path) if ue.EditorAssetLibrary.does_asset_exist(path) else assets.create_asset(name,ROOT,ue.MaterialInstanceConstant,ue.MaterialInstanceConstantFactoryNew())
    m.set_editor_property('parent',ue.load_asset(parent) if isinstance(parent,str) else parent)
    if tint is not None: lib.set_material_instance_vector_parameter_value(m,'Tint',ue.LinearColor(*tint,1))
    if rough is not None: lib.set_material_instance_scalar_parameter_value(m,'Roughness',rough)
    ue.EditorAssetLibrary.save_loaded_asset(m)
    return m

plaster=instance('MI_WarmPlaster','/Game/RoomShift/M_Plaster',(.72,.68,.60),.87)
ceiling=instance('MI_Ceiling','/Game/RoomShift/M_Plaster',(.82,.79,.71),.9)
oak=instance('MI_Oak','/Game/RoomShift/M_Wood',(.32,.23,.14),.48)
brass=instance('MI_Brass','/Game/RoomShift/M_Surface',(.25,.19,.10),.35)
lib.set_material_instance_scalar_parameter_value(brass,'Metallic',.65)
ue.EditorAssetLibrary.save_loaded_asset(brass)

# A subtle silhouette overlay works with imported materials without changing their shaders.
selection=ue.load_asset(ROOT+'/M_Selection') if ue.EditorAssetLibrary.does_asset_exist(ROOT+'/M_Selection') else assets.create_asset('M_Selection',ROOT,ue.Material,ue.MaterialFactoryNew())
lib.delete_all_material_expressions(selection)
selection.set_editor_property('blend_mode',ue.BlendMode.BLEND_TRANSLUCENT)
selection.set_editor_property('two_sided',True)
selection.set_editor_property('shading_model',ue.MaterialShadingModel.MSM_UNLIT)
color=lib.create_material_expression(selection,ue.MaterialExpressionConstant3Vector,-300,0)
color.set_editor_property('constant',ue.LinearColor(.65,.52,.23,1))
alpha=lib.create_material_expression(selection,ue.MaterialExpressionFresnel,-300,150)
alpha.set_editor_property('exponent',3.0)
alpha.set_editor_property('base_reflect_fraction',.035)
lib.connect_material_property(color,'',ue.MaterialProperty.MP_EMISSIVE_COLOR)
lib.connect_material_property(alpha,'',ue.MaterialProperty.MP_OPACITY)
lib.recompile_material(selection); ue.EditorAssetLibrary.save_loaded_asset(selection)

puzzle=actors.spawn_actor_from_class(ue.load_class(None,'/Script/RoomShift.RoomShiftPuzzle'),ue.Vector())
puzzle.set_actor_label('RoomShiftPuzzle — Five rotation axes')
puzzle.bake_editable_scene()

# Convert temporary generated materials into saved material instances.
material_cache={}
for actor in list(actors.get_all_level_actors()):
    tags=[str(t) for t in actor.tags]
    label=actor.get_actor_label()
    if any(t.startswith('RS_FURNITURE_') for t in tags):
        actors.destroy_actor(actor)  # Only generated fallback objects in this new level.
        continue
    comp=actor.get_component_by_class(ue.StaticMeshComponent)
    if comp and 'RS_AUTHORED' in tags:
        if label.startswith('FloorPlank'):
            comp.set_material(0,ue.load_asset('/Game/ArchVis/Materials/M_WoodFloor_MAT_Inst'))
        elif label.endswith('Shell'): comp.set_material(0,plaster)
        elif label=='Ceiling': comp.set_material(0,ceiling)
        elif 'Skirting' in label: comp.set_material(0,oak)
        elif label=='Rug': comp.set_material(0,ue.load_asset('/Game/ArchVis/Materials/MI_Carptet_Mat_Inst'))
        else:
            m=comp.get_material(0)
            if isinstance(m,ue.MaterialInstanceDynamic):
                parent=m.get_editor_property('parent')
                tint=m.get_vector_parameter_value('Tint')
                rough=m.get_scalar_parameter_value('Roughness')
                key=(parent.get_path_name(),tint.r,tint.g,tint.b,rough)
                if key not in material_cache:
                    mat=instance('MI_Scene_%03d'%len(material_cache),parent,(tint.r,tint.g,tint.b),rough)
                    material_cache[key]=mat
                comp.set_material(0,material_cache[key])
    if isinstance(actor,ue.PointLight):
        light=actor.get_component_by_class(ue.PointLightComponent)
        light.set_mobility(ue.ComponentMobility.MOVABLE)
        light.set_light_color(ue.LinearColor(1,.90,.77,1))

CENTERS=[(530,0,320),(60,530,320),(60,-530,320),(-600,0,320),(-30,0,630)]
GOALS=[[1,3,-1,-1,-1],[2,-1,2,-1,-1],[-1,1,0,-1,-1],[-1,-1,1,3,-1],[3,-1,-1,1,2],[-1,2,3,-1,1]]

def mul(a,b):
    x,y,z,w=a; X,Y,Z,W=b
    return (w*X+x*W+y*Z-z*Y,w*Y-x*Z+y*W+z*X,w*Z+x*Y-y*X+z*W,w*W-x*X-y*Y-z*Z)
def rotate(q,v):
    return mul(mul(q,(*v,0)),(-q[0],-q[1],-q[2],q[3]))[:3]
def inv_turn(face,turn):
    a=-turn*math.pi/4
    axis=(1,0,0) if face in (0,3) else (0,0,1) if face==4 else (0,1,0)
    return tuple(v*math.sin(a) for v in axis)+(math.cos(a),)

def mesh_actor(name,mesh,location,rotation=(0,0,0,1),scale=1.,tags=(),folder='RoomShift/Decor'):
    actor=actors.spawn_actor_from_class(ue.StaticMeshActor,ue.Vector(*location),ue.Quat(*rotation).rotator())
    actor.set_actor_label(name); actor.set_folder_path(folder); actor.tags=list(tags)
    comp=actor.static_mesh_component
    comp.set_mobility(ue.ComponentMobility.MOVABLE)
    comp.set_static_mesh(mesh)
    comp.set_world_scale3d(ue.Vector(scale,scale,scale))
    comp.set_collision_enabled(ue.CollisionEnabled.QUERY_ONLY)
    comp.set_collision_response_to_all_channels(ue.CollisionResponseType.ECR_IGNORE)
    comp.set_collision_response_to_channel(ue.CollisionChannel.ECC_VISIBILITY,ue.CollisionResponseType.ECR_BLOCK)
    return actor

def make_slice(source,name,axis,low,high):
    path=ROOT+'/Fragments/'+name
    if ue.EditorAssetLibrary.does_asset_exist(path): return ue.load_asset(path)
    dm=ue.DynamicMesh()
    dm,outcome=ue.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(source,dm,ue.GeometryScriptCopyMeshFromAssetOptions(),ue.GeometryScriptMeshReadLOD())
    if outcome!=ue.GeometryScriptOutcomePins.SUCCESS: raise RuntimeError('Cannot copy '+source.get_name())
    # Plane cut uses local +Z as the cut normal.
    rotation=ue.Rotator() if axis==2 else ue.Rotator(roll=90) if axis==1 else ue.Rotator(pitch=90)
    # The sign of the rotated normal is resolved explicitly for each axis.
    normal=rotation.quaternion().rotate_vector(ue.Vector(0,0,1))
    sign=(normal.x,normal.y,normal.z)[axis]
    for value,keep_above in [(low,True),(high,False)]:
        if value is None: continue
        pos=[0.,0.,0.]; pos[axis]=value
        options=ue.GeometryScriptMeshPlaneCutOptions()
        options.fill_holes=True; options.fill_spans=True; options.hole_fill_material_id=0
        # Default cut discards the positive half-space. Flip to preserve above.
        options.flip_cut_side=(keep_above if sign>0 else not keep_above)
        ue.GeometryScript_MeshBooleans.apply_mesh_plane_cut(dm,ue.Transform(location=ue.Vector(*pos),rotation=rotation),options)
    opts=ue.GeometryScriptCreateNewStaticMeshAssetOptions()
    opts.enable_collision=True
    opts.collision_mode=ue.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
    opts.enable_recompute_tangents=True
    result,outcome=ue.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm,path,opts)
    if not result or outcome!=ue.GeometryScriptOutcomePins.SUCCESS: raise RuntimeError('Cannot create '+name)
    result.set_editor_property('static_materials',source.static_materials)
    ue.EditorAssetLibrary.save_loaded_asset(result)
    return result

# Source assets remain intact. All slices share the original coordinate system and UVs.
SPECS=[
    ('Sofa','/Game/ArchVis/Mesh/SM_Couch',(260,-120,0),0,1.25,1,[0,1]),
    ('CoffeeTable','/Game/ArchVis/Mesh/SM_CoffeeTable',(90,-120,0),0,1.0,2,[0,2]),
    ('Door','/Game/ArchVis/Mesh/SM_Door',(-135,480,0),0,1.0,2,[1,2]),
    ('Armchair','/Game/ArchVis/Mesh/SM_Couch_1seat',(-150,-195,0),-90,1.25,0,[2,3]),
    ('Bookcase','/Game/OldFurniturePack_01/Meshes/SM_WoodenShelf_03',(320,200,0),90,.9,2,[0,3,4]),
    ('WritingDesk','/Game/OldFurniturePack_01/Meshes/SM_Table_01',(-115,160,0),0,.78,0,[1,2,4]),
]
manifest=[]
for f,(name,path,center,yaw,scale,axis,faces) in enumerate(SPECS):
    source=ue.load_asset(path)
    if not source: raise RuntimeError('Missing '+path)
    b=source.get_bounding_box(); lo=[b.min.x,b.min.y,b.min.z]; hi=[b.max.x,b.max.y,b.max.z]
    q=(0,0,math.sin(math.radians(yaw)/2),math.cos(math.radians(yaw)/2))
    offset=rotate(q,(-(lo[0]+hi[0])*.5*scale,-(lo[1]+hi[1])*.5*scale,-lo[2]*scale))
    goal=[center[i]+offset[i]+(180 if i==2 else 0) for i in range(3)]
    cuts=[lo[axis]+(hi[axis]-lo[axis])*i/len(faces) for i in range(1,len(faces))]
    for part,face in enumerate(faces):
        mesh=make_slice(source,name+'_%d'%part,axis,None if part==0 else cuts[part-1],None if part==len(faces)-1 else cuts[part])
        inverse=inv_turn(face,GOALS[f][face])
        local=rotate(inverse,[goal[i]-CENTERS[face][i] for i in range(3)])
        initial=[CENTERS[face][i]+local[i] for i in range(3)]
        actor=mesh_actor(name+'_Part_%d_Face_%s'%(part,chr(65+face)),mesh,initial,mul(inverse,q),scale,
            ['RS_AUTHORED','RS_FACE_%d'%face,'RS_FURNITURE_%d'%f], 'RoomShift/Furniture/'+name)
        manifest.append({'actor':actor.get_actor_label(),'source':path,'fragment':mesh.get_path_name(),'face':face,'furniture':f,'goal':GOALS[f]})

def decor(name,path,pos,yaw=0,scale=1.):
    q=(0,0,math.sin(math.radians(yaw)/2),math.cos(math.radians(yaw)/2))
    return mesh_actor(name,ue.load_asset(path),pos,q,scale)

decor('Plant_NorthEast','/Game/ArchVis/Mesh/SM_Plant',(470,450,0),10,1.5)
decor('Plant_Entry','/Game/ArchVis/Mesh/SM_Plant',(-480,-430,0),75,1.3)
decor('FloorLamp_Lounge','/Game/ArchVis/Mesh/SM_FloorLamp',(390,-350,0),0,1.4)
decor('FloorLamp_Reading','/Game/ArchVis/Mesh/SM_FloorLamp',(-330,-350,0),0,1.2)
decor('CeilingLight_1','/Game/ArchVis/Mesh/SM_RoundCeilingLight',(360,-420,640),0,1.7)
decor('CeilingLight_2','/Game/ArchVis/Mesh/SM_RoundCeilingLight',(-400,420,640),0,1.7)

# Frame the rotation panels with warm architectural millwork.
cube=ue.load_asset('/Engine/BasicShapes/Cube')
for x in [-530,470]:
    for y in [-500,500]:
        a=mesh_actor('Oak_Pilaster_%s_%s'%(x,y),cube,(x,y,320))
        a.static_mesh_component.set_world_scale3d(ue.Vector(.14,.14,6.4))
        a.static_mesh_component.set_material(0,oak)
for y in [-535,535]:
    a=mesh_actor('CrownTrim_%s'%y,cube,(-30,y,625))
    a.static_mesh_component.set_world_scale3d(ue.Vector(11.5,.16,.24))
    a.static_mesh_component.set_material(0,oak)

# Persistent editor camera makes the room easy to frame without Play.
camera=actors.spawn_actor_from_class(ue.CameraActor,ue.Vector(-550,0,260),ue.Rotator(pitch=-12))
camera.set_actor_label('PreviewCamera — select and pilot to view the room')
camera.set_folder_path('RoomShift/Preview')
camera.camera_component.set_field_of_view(100)
ue.get_editor_subsystem(ue.UnrealEditorSubsystem).set_level_viewport_camera_info(ue.Vector(-550,0,260),ue.Rotator(pitch=-12))
if not levels.save_current_level(): raise RuntimeError('Studio map save failed')
ue.EditorAssetLibrary.save_directory(ROOT,only_if_is_dirty=True,recursive=True)
Path(ue.Paths.project_dir(),'Documentation/StudioManifest.json').write_text(json.dumps(manifest,indent=2))
ue.log('ROOMSHIFT_STUDIO_READY fragments=%d faces=5 furniture=6'%len(manifest))
