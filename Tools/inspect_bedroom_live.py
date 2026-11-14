import unreal as u
import json
from pathlib import Path
ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
print('PIE', ls.is_in_play_in_editor())
if ls.is_in_play_in_editor():
    ls.editor_request_end_play()
else:
    world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
    print('WORLD',world.get_path_name())
    rows=[]
    for a in u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors():
        c,e=a.get_actor_bounds(False)
        m=a.get_component_by_class(u.StaticMeshComponent)
        row=dict(label=a.get_actor_label(),name=a.get_name(),cls=a.get_class().get_name(),tags=[str(t) for t in a.tags],location=list(a.get_actor_location().to_tuple()),center=list(c.to_tuple()),extent=list(e.to_tuple()),scale=list(a.get_actor_scale3d().to_tuple()),rotation=str(a.get_actor_rotation()),mesh=m.static_mesh.get_path_name() if m and m.static_mesh else None)
        if row['cls']=='HallwayPuzzle':
            row['camera']=list(a.get_editor_property('bedroom_camera').to_tuple())
            row['pivot']=list(a.get_editor_property('bedroom_center').to_tuple())
        rows.append(row)
    Path(u.Paths.project_dir(),'Documentation/BedroomLiveInspection.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
    print('ACTORS',len(rows))
