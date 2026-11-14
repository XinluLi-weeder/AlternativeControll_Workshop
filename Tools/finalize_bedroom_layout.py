import unreal as u
import json
from pathlib import Path
root=Path(u.Paths.project_dir())
ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not ls.is_in_play_in_editor()
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
parts=[a for a in actors if a.actor_has_tag('RH_ITEM_8')]
boxes=[a.get_actor_bounds(False) for a in parts]
center=u.Vector(*[(min(c.to_tuple()[i]-e.to_tuple()[i] for c,e in boxes)+max(c.to_tuple()[i]+e.to_tuple()[i] for c,e in boxes))/2 for i in range(3)])
target=u.Vector(800,1400,450)
for a in parts:a.set_actor_location(a.get_actor_location()+target-center,False,False)
bed=next(a for a in actors if a.actor_has_tag('RH_SOURCE_3'))
c,e=bed.get_actor_bounds(False)
bed.set_actor_location(bed.get_actor_location()+u.Vector(530-c.x,1350-c.y,0),False,False)
assert ls.save_current_level()
print('Window full sweep corrected; restored bed kept out of camera space.')
