"""Bind the user's saved Hallyway scene. Refuses to replace an already authored setup."""
import json
from pathlib import Path
import unreal as u

LEVEL='/Game/Levels/Hallyway'
ROOT='/Game/RoomShift/Hallway'
es=u.get_editor_subsystem(u.EditorActorSubsystem)
ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert ls.load_level(LEVEL)
original=list(es.get_all_level_actors())
if any(a.get_class().get_name()=='HallwayPuzzle' for a in original):
    raise RuntimeError('Hallway is already configured; do not overwrite artistic edits.')
by_name={a.get_name():a for a in original}
assets=u.AssetToolsHelpers.get_asset_tools()
ml=u.MaterialEditingLibrary
u.EditorAssetLibrary.make_directory(ROOT+'/Fragments')

def tags(actor,*values): actor.tags=list(actor.tags)+[v for v in values if v not in [str(t) for t in actor.tags]]
def material(name,color,overlay=False):
    path=ROOT+'/'+name
    if u.EditorAssetLibrary.does_asset_exist(path): return u.load_asset(path)
    m=assets.create_asset(name,ROOT,u.Material,u.MaterialFactoryNew())
    m.set_editor_property('two_sided',True)
    m.set_editor_property('shading_model',u.MaterialShadingModel.MSM_UNLIT)
    if overlay:m.set_editor_property('blend_mode',u.BlendMode.BLEND_TRANSLUCENT)
    c=ml.create_material_expression(m,u.MaterialExpressionVectorParameter,-400,0)
    c.set_editor_property('parameter_name','Color');c.set_editor_property('default_value',u.LinearColor(*color,1))
    strength=ml.create_material_expression(m,u.MaterialExpressionScalarParameter,-400,150)
    strength.set_editor_property('parameter_name','Strength');strength.set_editor_property('default_value',.6)
    if overlay:
        mult=ml.create_material_expression(m,u.MaterialExpressionMultiply,-150,0)
        ml.connect_material_expressions(c,'',mult,'A');ml.connect_material_expressions(strength,'',mult,'B')
        ml.connect_material_property(mult,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
        fresnel=ml.create_material_expression(m,u.MaterialExpressionFresnel,-400,300)
        fresnel.set_editor_property('exponent',2.0);fresnel.set_editor_property('base_reflect_fraction',.15)
        opacity=ml.create_material_expression(m,u.MaterialExpressionMultiply,-150,220)
        ml.connect_material_expressions(fresnel,'',opacity,'A');ml.connect_material_expressions(strength,'',opacity,'B')
        ml.connect_material_property(opacity,'',u.MaterialProperty.MP_OPACITY)
    else:ml.connect_material_property(c,'',u.MaterialProperty.MP_EMISSIVE_COLOR)
    ml.recompile_material(m);u.EditorAssetLibrary.save_loaded_asset(m);return m

overlay=material('M_HallwayOverlay',(.1,.5,1),True)
black=material('M_DoorwayBlack',(0,0,0))

def spawn_mesh(label,mesh,location,scale=(1,1,1),rotation=None):
    a=es.spawn_actor_from_class(u.StaticMeshActor,u.Vector(*location),rotation or u.Rotator())
    a.set_actor_label(label);a.set_folder_path('Hallway/Gameplay')
    c=a.static_mesh_component;c.set_mobility(u.ComponentMobility.MOVABLE);c.set_static_mesh(mesh)
    a.set_actor_scale3d(u.Vector(*scale));c.set_collision_enabled(u.CollisionEnabled.QUERY_ONLY)
    c.set_collision_response_to_all_channels(u.CollisionResponseType.ECR_IGNORE)
    c.set_collision_response_to_channel(u.CollisionChannel.ECC_VISIBILITY,u.CollisionResponseType.ECR_BLOCK)
    return a

def fragment(source,name,axis,low,high):
    path=ROOT+'/Fragments/'+name
    if u.EditorAssetLibrary.does_asset_exist(path):return u.load_asset(path)
    dm=u.DynamicMesh()
    dm,outcome=u.GeometryScript_AssetUtils.copy_mesh_from_static_mesh(source,dm,u.GeometryScriptCopyMeshFromAssetOptions(),u.GeometryScriptMeshReadLOD())
    if outcome!=u.GeometryScriptOutcomePins.SUCCESS:raise RuntimeError('Mesh read failed: '+source.get_name())
    rot=u.Rotator() if axis==2 else u.Rotator(roll=90) if axis==1 else u.Rotator(pitch=90)
    normal=rot.quaternion().rotate_vector(u.Vector(0,0,1));sign=(normal.x,normal.y,normal.z)[axis]
    for value,above in ((low,True),(high,False)):
        if value is None:continue
        p=[0.,0.,0.];p[axis]=value
        options=u.GeometryScriptMeshPlaneCutOptions();options.fill_holes=True;options.fill_spans=True;options.hole_fill_material_id=0
        options.flip_cut_side=above if sign>0 else not above
        u.GeometryScript_MeshBooleans.apply_mesh_plane_cut(dm,u.Transform(location=u.Vector(*p),rotation=rot),options)
    opts=u.GeometryScriptCreateNewStaticMeshAssetOptions();opts.enable_collision=True;opts.collision_mode=u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE;opts.enable_recompute_tangents=True
    mesh,outcome=u.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm,path,opts)
    if not mesh or outcome!=u.GeometryScriptOutcomePins.SUCCESS:raise RuntimeError('Slice failed: '+name)
    mesh.set_editor_property('static_materials',source.static_materials);u.EditorAssetLibrary.save_loaded_asset(mesh);return mesh

# Inspect every source before making scene changes. Original models remain intact.
specs=[
    ('StaticMeshActor_9','EntryChair',0,[1,2],[1,3],(925,-330,230),150),
    ('StaticMeshActor_14','EntryTable',0,[1,4],[2,1],(925,-150,230),150),
    ('StaticMeshActor_13','CoatStand',0,[2,4],[2,3],(925,30,230),150),
    ('StaticMeshActor_34','Bed',1,[0,2,4],[1,2,3],(720,1090,380),260),
    ('StaticMeshActor_70','Desk',1,[0,1,3],[2,3,1],(980,1090,380),260),
    ('StaticMeshActor_54','BedroomChair',1,[1,3],[1,2],(1240,1090,380),220),
]
for name,*_ in specs:assert name in by_name and by_name[name].static_mesh_component.static_mesh
manifest=[]
for item,(name,label,stage,faces,goals,center,size) in enumerate(specs):
    source_actor=by_name[name];comp=source_actor.static_mesh_component;source=comp.static_mesh
    bounds=source.get_bounding_box();lo=[bounds.min.x,bounds.min.y,bounds.min.z];hi=[bounds.max.x,bounds.max.y,bounds.max.z]
    axis=max(range(3),key=lambda i:hi[i]-lo[i]);cuts=[lo[axis]+(hi[axis]-lo[axis])*i/len(faces) for i in range(1,len(faces))]
    world_center,extent=source_actor.get_actor_bounds(False);ratio=size/(2*max(extent.x,extent.y,extent.z))
    loc=source_actor.get_actor_location();sc=source_actor.get_actor_scale3d();q=source_actor.get_actor_rotation()
    pos=[center[0]-(world_center.x-loc.x)*ratio,center[1]-(world_center.y-loc.y)*ratio,center[2]-(world_center.z-loc.z)*ratio]
    scale=[sc.x*ratio,sc.y*ratio,sc.z*ratio]
    tags(source_actor,'RH_SOURCE_'+str(item),'RH_STAGE_'+str(stage))
    for p,(face,goal) in enumerate(zip(faces,goals)):
        mesh=fragment(source,label+'_'+str(p),axis,None if p==0 else cuts[p-1],None if p==len(faces)-1 else cuts[p])
        a=spawn_mesh(label+'_Part_'+str(p),mesh,pos,scale,q)
        for slot in range(comp.get_num_materials()):a.static_mesh_component.set_material(slot,comp.get_material(slot))
        tags(a,'RH_STAGE_'+str(stage),'RH_ITEM_'+str(item),'RH_FACE_'+str(face),'RH_GOAL_'+str(goal))
        a.set_is_temporarily_hidden_in_editor(True)
        a.set_actor_hidden_in_game(True)
        manifest.append({'item':item,'stage':stage,'source':name,'fragment':mesh.get_path_name(),'face':face,'goal':goal,'staging_scale':ratio})

# Room membership is based on the existing doorway, not imported asset names.
for a in original:
    if a.get_component_by_class(u.StaticMeshComponent) and a.get_actor_location().y>405 and a.get_actor_location().x<2100:tags(a,'RH_STAGE_1')
    if a.get_component_by_class(u.LightComponent):
        a.get_component_by_class(u.LightComponent).set_intensity(0)
        a.set_folder_path('Hallway/OriginalLights_Disabled')
    if isinstance(a,u.ExponentialHeightFog):a.set_actor_hidden_in_game(True)
curtain=by_name['StaticMeshActor_33'];curtain.static_mesh_component.set_mobility(u.ComponentMobility.MOVABLE);tags(curtain,'RH_CURTAIN')
cube=u.load_asset('/Engine/BasicShapes/Cube')
mask=spawn_mesh('Bedroom_Doorway_Blackout',cube,(925,398,180),(2.65,.06,5.2));mask.static_mesh_component.set_material(0,black);tags(mask,'RH_BLACKOUT')
mask.set_is_temporarily_hidden_in_editor(True)

# Independent inset rotation panels preserve the existing structural shell and doorway.
panel_specs=[
    (0,1,(1239,-150,230),(.04,5.15,5.15),'StaticMeshActor_7'),
    (0,2,(613,-150,230),(.04,5.15,5.15),'StaticMeshActor_8'),
    (0,4,(925,-150,536),(5.15,5.15,.04),'StaticMeshActor_4'),
    (1,0,(980,1790,380),(8.5,.04,8.5),'StaticMeshActor_23'),
    (1,1,(1804,1090,380),(.04,8.5,8.5),'StaticMeshActor_18'),
    (1,2,(158,1090,380),(.04,8.5,8.5),'StaticMeshActor_16'),
    (1,3,(980,401,380),(8.5,.04,8.5),'StaticMeshActor_3'),
    (1,4,(980,1090,906),(8.5,8.5,.04),'StaticMeshActor_22'),
]
for stage,face,pos,scale,ref in panel_specs:
    a=spawn_mesh('Room%d_%s_RotationPanel'%(stage,chr(65+face)),cube,pos,scale)
    a.static_mesh_component.set_material(0,by_name[ref].static_mesh_component.get_material(0))
    tags(a,'RH_PANEL','RH_STAGE_'+str(stage),'RH_FACE_'+str(face))

def rect(label,stage,pos,power,temp,width,height,rotation=u.Rotator(pitch=-90)):
    a=es.spawn_actor_from_class(u.RectLight,u.Vector(*pos),rotation);a.set_actor_label(label);a.set_folder_path('Hallway/Lighting')
    c=a.get_component_by_class(u.RectLightComponent);c.set_mobility(u.ComponentMobility.MOVABLE)
    c.set_editor_property('intensity_units',u.LightUnits.LUMENS);c.set_intensity(power);c.set_editor_property('use_temperature',True);c.set_temperature(temp)
    c.set_source_width(width);c.set_source_height(height);c.set_attenuation_radius(1600 if stage else 900)
    tags(a,'RH_STAGE_'+str(stage));return a

rect('Entry_CoolCeiling_4800K',0,(925,-220,520),5000,4800,160,260)
rect('Entry_SoftBounce_5000K',0,(930,320,360),800,5000,180,180,u.Rotator(pitch=-25,yaw=-90))
rect('Bedroom_WarmCeiling_3000K',1,(980,1100,875),9500,3000,300,400)
rect('Bedroom_BedsideBounce_2800K',1,(360,850,450),1100,2800,110,160,u.Rotator(pitch=-35,yaw=35))
rect('Bedroom_WallWash_3200K',1,(1660,1600,600),1500,3200,180,220,u.Rotator(pitch=-30,yaw=-135))
lamp=es.spawn_actor_from_class(u.PointLight,u.Vector(1614,1435,222));lamp.set_actor_label('Bedroom_DeskLamp_2700K');lamp.set_folder_path('Hallway/Lighting')
lc=lamp.get_component_by_class(u.PointLightComponent);lc.set_mobility(u.ComponentMobility.MOVABLE);lc.set_editor_property('intensity_units',u.LightUnits.LUMENS);lc.set_intensity(350);lc.set_editor_property('use_temperature',True);lc.set_temperature(2700);lc.set_source_radius(8);lc.set_attenuation_radius(500);tags(lamp,'RH_STAGE_1')

director=es.spawn_actor_from_class(u.load_class(None,'/Script/RoomShift.HallwayPuzzle'),u.Vector())
director.set_actor_label('HallwayPuzzle_TwoRooms');director.set_folder_path('Hallway/Gameplay')
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
world.get_world_settings().set_editor_property('default_game_mode',u.load_class(None,'/Script/RoomShift.HallwayMode'))
camera=es.spawn_actor_from_class(u.CameraActor,u.Vector(925,-510,110),u.Rotator(pitch=10,yaw=90))
camera.set_actor_label('Hallway_EntryPreview');camera.set_folder_path('Hallway/Preview');camera.camera_component.set_field_of_view(110)
u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(u.Vector(925,-510,110),u.Rotator(pitch=10,yaw=90))
u.EditorAssetLibrary.save_directory(ROOT,only_if_is_dirty=True,recursive=True)
assert ls.save_current_level()
Path(u.Paths.project_dir(),'Documentation/HallwayManifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
u.log('HALLWAY_SETUP_COMPLETE fragments='+str(len(manifest)))
