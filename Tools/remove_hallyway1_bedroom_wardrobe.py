"""Remove the wardrobe source and its three puzzle fragments from Hallyway1."""
import json, shutil
from datetime import datetime
from pathlib import Path
import unreal as u

project=Path(u.Paths.project_dir())
level=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
assert level.load_level('/Game/Levels/Hallyway1')

backup=project/'Documentation'/('Hallyway1BeforeWardrobeRemoval-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
backup.mkdir(parents=True,exist_ok=False)
shutil.copy2(project/'Content/Levels/Hallyway1.umap',backup/'Hallyway1.umap')

removed=[]
for actor in actors.get_all_level_actors():
    tags={str(tag) for tag in actor.tags}
    label=actor.get_actor_label()
    if 'RH_ITEM_9' in tags or 'RH_SOURCE_9' in tags or label=='SM_Wardrobe_013':
        removed.append(label)
        actors.destroy_actor(actor)
assert len(removed)>=4,removed
assert level.save_current_level()

manifest_path=project/'Documentation'/'Hallyway1BedroomPuzzle.json'
manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
manifest['items']=[item for item in manifest['items'] if item['item']!=9]
manifest['bedroom_puzzle_count']=4
manifest['removed_item']='BedroomWardrobe'
manifest_path.write_text(json.dumps(manifest,indent=2,ensure_ascii=False),encoding='utf-8')
u.log('HALLYWAY1_WARDROBE_REMOVED actors=%s backup=%s'%(','.join(removed),backup))
