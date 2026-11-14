import unreal as u
ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not ls.is_in_play_in_editor()
ls.editor_play_simulate()
