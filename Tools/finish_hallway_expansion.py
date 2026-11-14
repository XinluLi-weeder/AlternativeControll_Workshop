import unreal as u
es=u.get_editor_subsystem(u.EditorActorSubsystem);ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert ls.load_level('/Game/Levels/Hallyway')
for a in es.get_all_level_actors():
    tags=[str(t) for t in a.tags]
    if a.get_name() in ('StaticMeshActor_21','StaticMeshActor_80') and 'RH_WALL_DECOR' not in tags:
        a.tags=list(a.tags)+['RH_WALL_DECOR']
    if 'RH_PANEL' in tags or 'RH_BLACKOUT' in tags or any(t.startswith('RH_ITEM_') for t in tags):
        a.set_is_temporarily_hidden_in_editor('RH_WALL_DECOR' not in [str(t) for t in a.tags])
    if a.get_actor_label()=='Hallway_EntryPreview':
        a.set_actor_location(u.Vector(925,-145,90),False,True)
    if a.get_actor_label()=='Bedroom_WarmCeiling_3000K':
        c=a.get_component_by_class(u.RectLightComponent);c.set_source_width(450);c.set_source_height(550)
    if a.get_actor_label()=='Bedroom_FrontBounce':
        c=a.get_component_by_class(u.LightComponent);c.set_intensity(1400);c.set_cast_shadows(False)
u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(u.Vector(925,-145,90),u.Rotator(pitch=10,yaw=90))
assert ls.save_current_level()
u.log('HALLWAY_EXPANSION_FINISHED')
