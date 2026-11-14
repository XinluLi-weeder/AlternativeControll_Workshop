"""Save a higher, wider bedroom viewpoint with full-turn clearance sampling."""
import unreal as u
import math,json,shutil
from datetime import datetime
from pathlib import Path
ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not ls.is_in_play_in_editor()
root=Path(u.Paths.project_dir())
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith('/Game/Levels/Hallyway1.')
assert ls.save_current_level()
backup=root/'Documentation'/('CameraBackup-'+datetime.now().strftime('%Y%m%d-%H%M%S')+'.umap')
shutil.copy2(root/'Content/Levels/Hallyway1.umap',backup)
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
p=next(a for a in actors if a.get_class().get_name()=='HallwayPuzzle')
pivot=p.get_editor_property('bedroom_center')
axes=[u.Vector(0,1,0),u.Vector(-1,0,0),u.Vector(1,0,0),u.Vector(0,-1,0),u.Vector(0,0,1)]
boxes=[]
for a in actors:
    tags=[str(t) for t in a.tags]
    if 'RH_STAGE_1' not in tags:continue
    c,e=a.get_actor_bounds(False)
    if any(t.startswith('RH_ITEM_') for t in tags):
        face=int(next(t[8:] for t in tags if t.startswith('RH_FACE_')))
        corners=[u.Vector(c.x+x*e.x,c.y+y*e.y,c.z+z*e.z) for x in (-1,1) for y in (-1,1) for z in (-1,1)]
        for deg in range(0,360,5):
            rotated=[u.MathLibrary.rotate_angle_axis(v-pivot,deg,axes[face])+pivot for v in corners]
            boxes.append(([min(v.to_tuple()[i] for v in rotated) for i in range(3)],[max(v.to_tuple()[i] for v in rotated) for i in range(3)]))
    elif any(t.startswith('RH_SOURCE_') for t in tags):
        boxes.append(([c.to_tuple()[i]-e.to_tuple()[i] for i in range(3)],[c.to_tuple()[i]+e.to_tuple()[i] for i in range(3)]))
best=None
for x in range(715,916,50):
    for y in range(970,1171,50):
        for z in range(540,691,25):
            point=[x,y,z]
            clearance=min(math.sqrt(sum(max(lo[i]-point[i],point[i]-hi[i],0)**2 for i in range(3))) for lo,hi in boxes)
            score=clearance-math.dist(point[:2],[815,1070])*.2-abs(z-640)*.15
            if best is None or score>best[0]:best=(score,point,clearance)
point=best[1]
p.set_editor_property('bedroom_camera',u.Vector(*point))
p.set_editor_property('field_of_view',126.)
p.tags=[t for t in p.tags if str(t)!='RH_HIGH_VIEW']+[u.Name('RH_HIGH_VIEW')]
assert ls.save_current_level()
u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(u.Vector(*point),u.Rotator(pitch=-18,yaw=90))
report=dict(camera=point,fov=126,pitch=-18,sampled_min_clearance=best[2],backup=str(backup))
(root/'Documentation/BedroomCameraRevision.json').write_text(json.dumps(report,indent=2))
manifest=json.loads((root/'Documentation/Hallyway1BedroomPuzzle.json').read_text())
manifest.update(camera=point,fov=126)
(root/'Documentation/Hallyway1BedroomPuzzle.json').write_text(json.dumps(manifest,indent=2))
print(report)
