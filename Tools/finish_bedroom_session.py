import unreal as u
performance=u.get_default_object(u.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
performance.set_editor_property('bThrottleCPUWhenNotForeground',False)
ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
if ls.is_in_play_in_editor():ls.editor_request_end_play()
print('Background CPU throttle disabled for this editor session; PIE ended.')
