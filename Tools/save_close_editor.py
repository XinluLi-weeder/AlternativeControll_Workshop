import unreal as u
ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert not ls.is_in_play_in_editor()
assert ls.save_current_level()
assert u.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
u.SystemLibrary.execute_console_command(u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world(),'QUIT_EDITOR')
