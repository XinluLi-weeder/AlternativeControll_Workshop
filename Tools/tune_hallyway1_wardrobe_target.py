"""Bring the wardrobe puzzle pieces closer to their right-wall rotation axis."""
import json, shutil
from datetime import datetime
from pathlib import Path
import unreal as u

project=Path(u.Paths.project_dir())
level=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert level.load_level('/Game/Levels/Hallyway1')
es=u.get_editor_subsystem(u.EditorActorSubsystem)
backup=project/'Documentation'/('Hallyway1BeforeWardrobeTune-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
backup.mkdir(parents=True,exist_ok=False)
shutil.copy2(project/'Content/Levels/Hallyway1.umap',backup/'Hallyway1.umap')
parts=[]
for actor in es.get_all_level_actors():
    if any(str(tag)=='RH_ITEM_9' for tag in actor.tags):
        p=actor.get_actor_location()
        actor.set_actor_location(u.Vector(p.x,p.y+110,p.z-70),False,True)
        parts.append(actor)
assert len(parts)==3,len(parts)
assert level.save_current_level()
manifest_path=project/'Documentation'/'Hallyway1BedroomPuzzle.json'
manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
for item in manifest['items']:
    if item['item']==9:
        item['target_bounds_center'][1]+=110
        item['target_bounds_center'][2]-=70
manifest_path.write_text(json.dumps(manifest,indent=2,ensure_ascii=False),encoding='utf-8')
u.log('HALLYWAY1_WARDROBE_TARGET_TUNED dy=110 dz=-70 backup='+str(backup))
