import unreal as u
subsystem=u.get_editor_subsystem(u.UnrealEditorSubsystem)
world=subsystem.get_game_world() if subsystem else u.find_object(None,'/Game/Levels/Hallyway1.Hallyway1')
u.SystemLibrary.execute_console_command(world,'HighResShot 1280x720')
