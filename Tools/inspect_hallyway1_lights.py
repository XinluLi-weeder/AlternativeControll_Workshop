"""Print the existing Hallyway1 light layout without changing the level."""
import unreal as u

level=u.get_editor_subsystem(u.LevelEditorSubsystem)
actors=u.get_editor_subsystem(u.EditorActorSubsystem)
assert level.load_level('/Game/Levels/Hallyway1')
for actor in actors.get_all_level_actors():
    light=actor.get_component_by_class(u.LightComponent)
    if light:
        loc=actor.get_actor_location()
        u.log('H1_LIGHT label=%s class=%s loc=(%.1f,%.1f,%.1f) intensity=%.1f color=%s tags=%s' % (
            actor.get_actor_label(),actor.get_class().get_name(),loc.x,loc.y,loc.z,
            light.get_editor_property('intensity'),str(light.get_editor_property('light_color')),
            ','.join(str(t) for t in actor.tags)))
