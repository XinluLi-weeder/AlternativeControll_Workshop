import unreal as u
assert u.get_editor_subsystem(u.UnrealEditorSubsystem) is None, 'Only close the standalone QA process'
world=u.find_object(None,'/Game/Levels/Hallyway1.Hallyway1')
assert world
u.SystemLibrary.execute_console_command(world,'quit')
