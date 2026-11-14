import unreal as u
import json
from pathlib import Path
es=u.get_editor_subsystem(u.EditorActorSubsystem)
u.get_editor_subsystem(u.LevelEditorSubsystem).load_level('/Game/Levels/Hallyway1')
rows=[]
for a in es.get_all_level_actors():
    p=a.get_actor_location(); origin,extent=a.get_actor_bounds(False)
    row={'label':a.get_actor_label(),'class':a.get_class().get_name(),'pos':[p.x,p.y,p.z],
         'bounds_center':[origin.x,origin.y,origin.z],'extent':[extent.x,extent.y,extent.z],
         'tags':[str(t) for t in a.tags]}
    if a.get_class().get_name()=='HallwayPuzzle':
        for key in ['entrance_camera','bedroom_camera','entrance_center','bedroom_center']:
            value=a.get_editor_property(key);row[key]=[value.x,value.y,value.z]
    rows.append(row)
w=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
result={'game_mode':str(w.get_world_settings().get_editor_property('default_game_mode')),'actors':rows}
Path(u.Paths.project_dir(),'Documentation','Hallyway1Inspection.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
