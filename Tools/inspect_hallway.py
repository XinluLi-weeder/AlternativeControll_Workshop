import json
from pathlib import Path
import unreal as ue

def vec(v): return [v.x, v.y, v.z]
es=ue.get_editor_subsystem(ue.EditorActorSubsystem)
world=ue.get_editor_subsystem(ue.UnrealEditorSubsystem).get_editor_world()
result={'world':world.get_path_name(),'actors':[]}
for a in es.get_all_level_actors():
    origin, extent=a.get_actor_bounds(False)
    r=a.get_actor_rotation()
    row={'name':a.get_name(),'label':a.get_actor_label(),'class':a.get_class().get_name(),'location':vec(a.get_actor_location()),'rotation':[r.pitch,r.yaw,r.roll],'scale':vec(a.get_actor_scale3d()),'bounds':[vec(origin),vec(extent)],'tags':[str(t) for t in a.tags],'components':[]}
    for c in a.get_components_by_class(ue.StaticMeshComponent):
        row['components'].append({'name':c.get_name(),'mesh':c.static_mesh.get_path_name() if c.static_mesh else None,'materials':[c.get_material(i).get_path_name() if c.get_material(i) else None for i in range(c.get_num_materials())]})
    result['actors'].append(row)
Path(ue.Paths.project_dir(),'Documentation/HallwayInspection.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
ue.log('HALLWAY_INSPECTION '+result['world']+' actors='+str(len(result['actors'])))
