"""One-time bedroom V2 authoring, preserving full-sized original models."""
import unreal as u
import json, shutil
from pathlib import Path
from datetime import datetime
root=Path(u.Paths.project_dir())
ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
es=u.get_editor_subsystem(u.EditorActorSubsystem)
assert not ls.is_in_play_in_editor()
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith('/Game/Levels/Hallyway1.')
assert ls.save_current_level()
backup=root/'Documentation'/('BedroomV2Backup-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
backup.mkdir()
shutil.copy2(root/'Content/Levels/Hallyway1.umap',backup/'Hallyway1.umap')
shutil.copytree(root/'Plugins/RoomShift/Source',backup/'Source')
actors=es.get_all_level_actors()
bylabel={a.get_actor_label():a for a in actors}
puzzle=next(a for a in actors if a.get_class().get_name()=='HallwayPuzzle')
assert not puzzle.actor_has_tag('RH_BEDROOM_V2'), 'Already authored'
plan=json.loads((root/'Documentation/BedroomLayoutPlan.json').read_text())
puzzle.set_editor_property('start_in_bedroom',True)
puzzle.set_editor_property('bedroom_camera',u.Vector(*plan['camera']))
puzzle.set_editor_property('bedroom_center',u.Vector(*plan['pivot']))
puzzle.set_editor_property('field_of_view',118.)
manifest=[]
def settag(a,prefix,value):
    a.tags=[t for t in a.tags if not str(t).startswith(prefix)]+[u.Name(prefix+str(value))]
for item,spec in plan['items'].items():
    parts=[a for a in actors if a.actor_has_tag('RH_ITEM_'+item)]
    parts.sort(key=lambda a:a.get_actor_label())
    boxes=[a.get_actor_bounds(False) for a in parts]
    center=[(min(c.to_tuple()[i]-e.to_tuple()[i] for c,e in boxes)+max(c.to_tuple()[i]+e.to_tuple()[i] for c,e in boxes))/2 for i in range(3)]
    delta=u.Vector(*[spec['target'][i]-center[i] for i in range(3)])
    for part,face,goal in zip(parts,spec['faces'],spec['goals']):
        part.set_actor_location(part.get_actor_location()+delta,False,False)
        settag(part,'RH_FACE_',face);settag(part,'RH_GOAL_',goal)
        manifest.append(dict(item=int(item),part=part.get_actor_label(),face=face,goal_degrees=goal*90))

# Window frame and sill are part of the same puzzle, with their own landing transforms.
glass=bylabel['window_glass']
target=u.Vector(*plan['items']['8']['target'])
delta=target-glass.get_actor_bounds(False)[0]
for index,label in enumerate(['Cube9','Cube14','Cube15','Cube16']):
    original=bylabel[label]
    original.tags=[u.Name('RH_STAGE_1'),u.Name('RH_PROP_8'),u.Name('RH_FOLLOW_FACE_3')]
    clone=es.duplicate_actor(original,world)
    assert clone
    clone.set_actor_label('BedroomWindowFrame_Part_'+str(index))
    face=[0,3,4,0][index];goal={0:3,3:2,4:1}[face]
    clone.tags=[u.Name('RH_STAGE_1'),u.Name('RH_ITEM_8'),u.Name('RH_FACE_'+str(face)),u.Name('RH_GOAL_'+str(goal)),u.Name('RH_HOME_'+original.get_name())]
    clone.set_actor_location(original.get_actor_location()+delta,False,False)
    clone.set_is_temporarily_hidden_in_editor(True)
    clone.set_actor_hidden_in_game(True)
    manifest.append(dict(item=8,part=clone.get_actor_label(),face=face,goal_degrees=goal*90))
settag(glass,'RH_FOLLOW_FACE_',3)
# Existing fill light was outside the actual right wall, and front fill pointed upward.
for label,position,rotation in [('Bedroom_PuzzleFill_Right',(1370,1070,500),u.Rotator(yaw=180)),('Bedroom_PuzzleFill_Front',(815,480,500),u.Rotator(pitch=-20,yaw=90))]:
    light=bylabel.get(label)
    if light:
        light.set_actor_location(u.Vector(*position),False,False)
        light.set_actor_rotation(rotation,False)
settag(puzzle,'RH_BEDROOM_V',2)
assert ls.save_current_level()
u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(u.Vector(*plan['camera']),u.Rotator(pitch=8,yaw=90))
(root/'Documentation/Hallyway1BedroomPuzzle.json').write_text(json.dumps(dict(level='/Game/Levels/Hallyway1',camera=plan['camera'],fov=118,items=manifest,bedroom_puzzle_count=4),indent=2),encoding='utf-8')
print('BEDROOM_V2_SAVED',backup)
