"""Expand the existing level to ten puzzles and full-surface rotation. Run once after backup."""
import unreal as u
import ast, json, math, random
from pathlib import Path
es=u.get_editor_subsystem(u.EditorActorSubsystem)
ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert ls.load_level('/Game/Levels/Hallyway')
actors=es.get_all_level_actors(); by_name={a.get_name():a for a in actors}
director=next(a for a in actors if a.get_class().get_name()=='HallwayPuzzle')
assert 'RH_EXPANDED_V2' not in [str(t) for t in director.tags], 'Already expanded; use a dedicated tuning pass.'
ROOT='/Game/RoomShift/Hallway'
assets=u.AssetToolsHelpers.get_asset_tools();ml=u.MaterialEditingLibrary
# Reuse mesh-cut and spawn helpers without executing the original scene builder.
tree=ast.parse(Path(u.Paths.project_dir(),'Tools/setup_hallway.py').read_text(encoding='utf-8'))
exec(compile(ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef)],type_ignores=[]),'hallway_helpers','exec'))
sources={int(str(t)[10:]):a for a in actors for t in a.tags if str(t).startswith('RH_SOURCE_')}
new=[(6,'StaticMeshActor_26','EntryWardrobe',0,[1,2],[3,1]),(7,'StaticMeshActor_32','EntryStorage',0,[1,4],[1,2]),(8,'StaticMeshActor_62','BedroomWardrobe',1,[0,2,4],[3,1,2]),(9,'StaticMeshActor_66','DisplayCabinet',1,[1,3,4],[2,3,1])]
for item,name,label,stage,faces,goals in new:
    a=by_name[name];sources[item]=a;comp=a.static_mesh_component;mesh=comp.static_mesh
    tags(a,'RH_SOURCE_'+str(item),'RH_STAGE_'+str(stage))
    box=mesh.get_bounding_box();lo=[box.min.x,box.min.y,box.min.z];hi=[box.max.x,box.max.y,box.max.z]
    axis=max(range(3),key=lambda i:hi[i]-lo[i]);cuts=[lo[axis]+(hi[axis]-lo[axis])*i/len(faces) for i in range(1,len(faces))]
    for i,(face,goal) in enumerate(zip(faces,goals)):
        part=fragment(mesh,label+'_'+str(i),axis,None if i==0 else cuts[i-1],None if i==len(faces)-1 else cuts[i])
        p=spawn_mesh(label+'_Part_'+str(i),part,(0,0,0))
        for slot in range(comp.get_num_materials()):p.static_mesh_component.set_material(slot,comp.get_material(slot))
        tags(p,'RH_STAGE_'+str(stage),'RH_ITEM_'+str(item),'RH_FACE_'+str(face),'RH_GOAL_'+str(goal))
        p.set_actor_hidden_in_game(True);p.set_is_temporarily_hidden_in_editor(True)

def number(a,prefix):return next((int(str(t)[len(prefix):]) for t in a.tags if str(t).startswith(prefix)),-1)
actors=es.get_all_level_actors()
parts={i:[a for a in actors if number(a,'RH_ITEM_')==i] for i in range(10)}
centers=[(925,-145,230),(980,1090,415)]
cameras=[(925,-145,90),(980,1090,90)]
rooms=[((619,-655,-65),(1233,370,529)),((165,405,-65),(1798,1780,901))]
sizes=[115,100,125,230,180,145,145,145,230,230]
stages=[0,0,0,1,1,1,0,0,1,1]
def add(a,b):return tuple(x+y for x,y in zip(a,b))
def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def rot(v,face,deg):
    a=math.radians(deg);c=math.cos(a);s=math.sin(a);x,y,z=v
    if face==0:return (c*x+s*z,y,-s*x+c*z)
    if face==3:return (c*x-s*z,y,s*x+c*z)
    if face==1:return (x,c*y+s*z,-s*y+c*z)
    if face==2:return (x,c*y-s*z,s*y+c*z)
    return (c*x-s*y,s*x+c*y,z)
def bbox(points):return (tuple(min(p[k] for p in points) for k in range(3)),tuple(max(p[k] for p in points) for k in range(3)))
def midpoint(b):return tuple((b[0][k]+b[1][k])/2 for k in range(3))
def distance(a,b):return math.sqrt(sum((a[k]-b[k])**2 for k in range(3)))
def gap(b,p):return math.sqrt(sum(max(b[0][k]-p[k],0,p[k]-b[1][k])**2 for k in range(3)))
data={}
for item in range(10):
    a=sources[item];world,extent=a.get_actor_bounds(False);origin=a.get_actor_location();sc=a.get_actor_scale3d();q=a.get_actor_rotation().quaternion()
    ratio=sizes[item]/(2*max(extent.x,extent.y,extent.z));offset=tuple((getattr(world,k)-getattr(origin,k))*ratio for k in ('x','y','z'))
    scale=tuple(getattr(sc,k)*ratio for k in ('x','y','z'))
    entries=[]
    for p in parts[item]:
        b=p.static_mesh_component.static_mesh.get_bounding_box();corners=[]
        for x in (b.min.x,b.max.x):
            for y in (b.min.y,b.max.y):
                for z in (b.min.z,b.max.z):
                    v=q.rotate_vector(u.Vector(x*scale[0],y*scale[1],z*scale[2]));corners.append(sub((v.x,v.y,v.z),offset))
        entries.append((p,number(p,'RH_FACE_'),number(p,'RH_GOAL_'),corners))
    data[item]=(ratio,offset,scale,entries)

# Find separated initial positions, while keeping all sampled rotations inside the room
# and away from the fixed camera. Pieces of one furniture share one solved transform.
rng=random.Random(731)
manifest=[]
for stage,order in [(0,[6,7,0,2,1]),(1,[8,9,3,4,5])]:
    occupied=[];targets=[];center=centers[stage];room=rooms[stage];camera=cameras[stage]
    for item in order:
        ratio,offset,scale,entries=data[item];best=None
        for attempt in range(1800):
            if stage==0:c=(rng.uniform(715,1135),rng.uniform(-345,55),rng.uniform(95,385))
            else:c=(rng.uniform(450,1510),rng.uniform(670,1510),rng.uniform(145,685))
            initial=[];valid=True;minclear=1e9
            for p,face,goal,corners in entries:
                for angle in range(0,360,15):
                    b=bbox([add(center,rot(sub(add(c,v),center),face,angle-goal*90)) for v in corners])
                    if any(b[0][k]<room[0][k] or b[1][k]>room[1][k] for k in range(3)) or gap(b,camera)<70:
                        valid=False;break
                    if angle==0:initial.append(b)
                if not valid:break
            if not valid:continue
            # Penalize projected angular overlap and physical interpenetration at spawn.
            score=0.
            for b in initial:
                m=midpoint(b);v=sub(m,camera);n=math.sqrt(sum(x*x for x in v));direction=tuple(x/n for x in v)
                if occupied:
                    sep=[]
                    for other in occupied:
                        ov=sub(midpoint(other),camera);on=math.sqrt(sum(x*x for x in ov))
                        angular=math.acos(max(-1,min(1,sum(direction[k]*ov[k]/on for k in range(3)))))
                        overlap=all(b[0][k]<other[1][k]+12 and b[1][k]>other[0][k]-12 for k in range(3))
                        sep.append(angular*100- (350 if overlap else 0))
                    score+=min(sep)
                score+=min(abs(v[0]),abs(v[1]))*.04
            if targets:score+=min(distance(c,t) for t in targets)*.2
            if best is None or score>best[0]:best=(score,c,initial)
        assert best is not None, 'No collision-free room orbit for item '+str(item)
        _,c,initial=best;occupied+=initial;targets.append(c)
        a=sources[item]
        for p,face,goal,corners in entries:
            p.set_actor_location(u.Vector(*sub(c,offset)),False,True);p.set_actor_rotation(a.get_actor_rotation(),True);p.set_actor_scale3d(u.Vector(*scale))
            p.set_is_temporarily_hidden_in_editor(True)
        manifest.append({'item':item,'stage':stage,'target_center':c,'size':sizes[item],'initial_bounds':initial})
        u.log('HALLWAY_DISTRIBUTED item=%d center=%s score=%.1f'%(item,c,best[0]))

director.set_editor_property('entrance_center',u.Vector(*centers[0]));director.set_editor_property('bedroom_center',u.Vector(*centers[1]))
director.set_editor_property('entrance_camera',u.Vector(*cameras[0]));director.set_editor_property('bedroom_camera',u.Vector(*cameras[1]))

# Full wall finish panels extend past the room boundary so a 45-degree turn never
# opens a hole to the outside. Fixed adjacent surfaces mask the excess perimeter.
cube=u.load_asset('/Engine/BasicShapes/Cube')
wood=u.load_asset('/Game/RoomShift/Studio/M_StudioFloor')
for a in actors:
    if 'RH_PANEL' not in [str(t) for t in a.tags]:continue
    stage=number(a,'RH_STAGE_');face=number(a,'RH_FACE_');center=centers[stage]
    a.set_actor_rotation(u.Rotator(),True)
    pos=list(center)
    if face==0:pos[1]=1788
    elif face==3:pos[1]=403
    elif face==1:pos[0]=615 if stage==0 else 161
    elif face==2:pos[0]=1237 if stage==0 else 1802
    else:pos[2]=533 if stage==0 else 905
    side=15 if stage==0 else 25
    normal=1 if face in (0,3) else 0 if face in (1,2) else 2
    scale=[side]*3;scale[normal]=.06
    a.set_actor_location(u.Vector(*pos),False,True);a.set_actor_scale3d(u.Vector(*scale));a.set_actor_label('Room%d_%s_WholeWall'%(stage,chr(65+face)))
    # Offset wooden inlays give a visibly asymmetric reference for every turn.
    tangent=[i for i in range(3) if i!=normal];inward=-1 if face in (0,2,4) else 1
    for j,offset in enumerate((-180,-60,90,220) if stage==0 else (-350,-130,170,390)):
        pp=list(pos);pp[normal]+=inward*4;pp[tangent[0]]+=offset
        ss=[side]*3;ss[normal]=.018;ss[tangent[0]]=.024 if j<3 else .12
        trim=spawn_mesh('Room%d_%s_Inlay%d'%(stage,chr(65+face),j),cube,pp,ss)
        if wood:trim.static_mesh_component.set_material(0,wood)
        tags(trim,'RH_PANEL','RH_STAGE_'+str(stage),'RH_FACE_'+str(face))
        trim.set_folder_path('Hallway/RotatingWalls')
    a.set_folder_path('Hallway/RotatingWalls')

# These existing decorative pieces now turn with their wall, reinforcing the linkage.
for name,stage,face in [('StaticMeshActor_21',0,2),('StaticMeshActor_80',1,3)]:
    a=by_name[name];tags(a,'RH_PANEL','RH_STAGE_'+str(stage),'RH_FACE_'+str(face));a.static_mesh_component.set_mobility(u.ComponentMobility.MOVABLE)
for name,item in [('StaticMeshActor_36',6),('StaticMeshActor_40',6),('StaticMeshActor_60',6),('StaticMeshActor_30',7)]:tags(by_name[name],'RH_PROP_'+str(item))
tags(director,'RH_EXPANDED_V2')
u.EditorAssetLibrary.save_directory(ROOT,only_if_is_dirty=True,recursive=True)
u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(u.Vector(*cameras[0]),u.Rotator(pitch=10,yaw=90))
assert ls.save_current_level()
Path(u.Paths.project_dir(),'Documentation/HallwayExpandedLayout.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
u.log('HALLWAY_EXPANSION_COMPLETE objects=10')
