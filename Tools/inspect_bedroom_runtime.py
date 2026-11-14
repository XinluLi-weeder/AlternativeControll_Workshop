import unreal as u
import json
from pathlib import Path
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_game_world()
actors=u.GameplayStatics.get_all_actors_of_class(world,u.Actor)
rows=[]
for a in actors:
    if a.actor_has_tag('RH_STAGE_1'):
        rows.append(dict(name=a.get_name(),tags=[str(t) for t in a.tags],hidden=a.get_editor_property('hidden'),location=list(a.get_actor_location().to_tuple()),scale=list(a.get_actor_scale3d().to_tuple())))
Path(u.Paths.project_dir(),'Documentation/BedroomRuntimeVerification.json').write_text(json.dumps(rows,indent=2))
camera=next(a for a in u.GameplayStatics.get_all_actors_of_class(world,u.CameraActor) if abs(a.get_actor_location().x-815)<1)
u.AutomationLibrary.take_high_res_screenshot(1280,720,str(Path(u.Paths.project_dir()).resolve()/'Documentation/BedroomV2Preview.png'),camera=camera)
print('CAMERA',camera.get_actor_location())
