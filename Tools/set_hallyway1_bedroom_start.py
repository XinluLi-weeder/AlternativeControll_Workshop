"""Change only Hallyway1 startup settings; keep authored furniture and lighting."""
import unreal as u
import shutil
from pathlib import Path
from datetime import datetime

root=Path(u.Paths.project_dir())
backup=root/'Documentation'/('Hallyway1BeforeBedroomStart-'+datetime.now().strftime('%Y%m%d-%H%M%S'))
backup.mkdir(parents=True,exist_ok=False)
shutil.copy2(root/'Content/Levels/Hallyway1.umap',backup/'Hallyway1.umap')
level=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert level.load_level('/Game/Levels/Hallyway1')
actors=u.get_editor_subsystem(u.EditorActorSubsystem).get_all_level_actors()
puzzles=[a for a in actors if a.get_class().get_name()=='HallwayPuzzle']
assert len(puzzles)==1, 'Expected exactly one puzzle controller'
puzzle=puzzles[0]
puzzle.set_editor_property('start_in_bedroom',True)
puzzle.set_editor_property('bedroom_camera',u.Vector(980,1090,90))
puzzle.set_editor_property('field_of_view',110.)
assert level.save_current_level()
u.log('HALLYWAY1_BEDROOM_START_SAVED backup='+str(backup))
