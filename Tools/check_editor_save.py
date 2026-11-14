import unreal as u
world=u.get_editor_subsystem(u.UnrealEditorSubsystem).get_editor_world()
dirty=list(u.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(u.EditorLoadingAndSavingUtils.get_dirty_content_packages())
print('EDITOR_WORLD',world.get_path_name(),'DIRTY',[p.get_name() for p in dirty])
if dirty:
    assert u.EditorLoadingAndSavingUtils.save_packages(dirty,True), 'Could not save dirty packages'
assert not u.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert not u.EditorLoadingAndSavingUtils.get_dirty_content_packages()
u.SystemLibrary.execute_console_command(world,'QUIT_EDITOR')
