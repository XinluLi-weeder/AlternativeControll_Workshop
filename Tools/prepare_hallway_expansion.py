import unreal as u
import shutil
from pathlib import Path
from datetime import datetime
p=Path(u.Paths.project_dir())
w=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
assert w.get_path_name().startswith('/Game/Levels/Hallyway.'), w.get_path_name()
assert u.get_editor_subsystem(u.LevelEditorSubsystem).save_current_level()
b=p/'Documentation'/('HallwayBackup-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
b.mkdir()
shutil.copy2(p/'Content/Levels/Hallyway.umap',b/'Hallyway.umap')
shutil.copytree(p/'Plugins/RoomShift/Source',b/'Source')
shutil.copytree(p/'Tools',b/'Tools',ignore=shutil.ignore_patterns('__pycache__'))
exec((p/'Tools/inspect_hallway.py').read_text(encoding='utf-8'))
u.log('HALLWAY_EXPANSION_BACKUP '+str(b))
