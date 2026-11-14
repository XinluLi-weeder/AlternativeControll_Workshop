"""Non-destructive finish pass; existing furniture and authoring layout are preserved."""
import unreal as u
es=u.get_editor_subsystem(u.EditorActorSubsystem)
ls=u.get_editor_subsystem(u.LevelEditorSubsystem)
assert ls.load_level('/Game/Levels/Hallyway')
actors=es.get_all_level_actors();by_name={a.get_name():a for a in actors}
assert not any('RH_EXPANDED_V2' in [str(t) for t in a.tags] for a in actors), 'This legacy tuning script must not overwrite the expanded ten-object layout.'
centers={0:(755,-120,190),1:(925,-120,240),2:(1095,-120,220)}
old_centers={0:(925,-330,230),1:(925,-150,230),2:(925,30,230)}
def tag(a,value):
    if value not in [str(t) for t in a.tags]:a.tags=list(a.tags)+[value]
tag(by_name['StaticMeshActor_12'],'RH_PROP_1')
tag(by_name['StaticMeshActor_72'],'RH_PROP_4')
for a in actors:
    if a.get_actor_label()=='Bedroom_DeskLamp_2700K':tag(a,'RH_PROP_4')
    if any(str(t).startswith('RH_ITEM_') for t in a.tags) or 'RH_BLACKOUT' in [str(t) for t in a.tags]:a.set_is_temporarily_hidden_in_editor(True)
    if 'RH_PANEL' in [str(t) for t in a.tags]:
        stage=1 if 'RH_STAGE_1' in [str(t) for t in a.tags] else 0
        side=6.0 if stage else 4.2
        s=a.get_actor_scale3d()
        a.set_actor_scale3d(u.Vector(*[.04 if v<.1 else side for v in (s.x,s.y,s.z)]))
        face=next(int(str(t).split('_')[-1]) for t in a.tags if str(t).startswith('RH_FACE_'))
        if face in (1,2):
            newface=1 if a.get_actor_location().x<(925 if stage==0 else 980) else 2
            a.tags=[t for t in a.tags if not str(t).startswith('RH_FACE_')]+['RH_FACE_'+str(newface)]
            a.set_actor_label('Room%d_%s_RotationPanel'%(stage,chr(65+newface)))
    item=next((int(str(t).split('_')[-1]) for t in a.tags if str(t).startswith('RH_ITEM_')),-1)
    if item in centers and 'RH_SPREAD' not in [str(t) for t in a.tags]:
        delta=u.Vector(*[centers[item][i]-old_centers[item][i] for i in range(3)])
        a.set_actor_location(a.get_actor_location()+delta,False,True);tag(a,'RH_SPREAD')
    c=a.get_component_by_class(u.LightComponent)
    if c and a.get_actor_label().startswith('Entry_'):c.set_temperature(5800)
    if c and a.get_actor_label()=='Bedroom_WarmCeiling_3000K':c.set_temperature(3500)
    if c and a.get_actor_label()=='Bedroom_WallWash_3200K':c.set_temperature(3800)
    if a.get_class().get_name()=='HallwayPuzzle':a.set_editor_property('exposure_compensation',6.0)
labels={a.get_actor_label() for a in actors}
for label,stage,pos,power,temp in [('Entry_FrontBounce',0,(925,-600,290),700,5800),('Bedroom_FrontBounce',1,(980,500,340),1000,4000)]:
    if label not in labels:
        a=es.spawn_actor_from_class(u.RectLight,u.Vector(*pos),u.Rotator(pitch=-5,yaw=90));a.set_actor_label(label);a.set_folder_path('Hallway/Lighting');tag(a,'RH_STAGE_'+str(stage))
        c=a.get_component_by_class(u.RectLightComponent);c.set_mobility(u.ComponentMobility.MOVABLE);c.set_editor_property('intensity_units',u.LightUnits.LUMENS);c.set_intensity(power);c.set_editor_property('use_temperature',True);c.set_temperature(temp);c.set_source_width(250);c.set_source_height(250);c.set_attenuation_radius(1200)
# Use the project's existing matte wood floor, leaving the imported master unchanged.
floor=u.load_asset('/Game/RoomShift/Studio/M_StudioFloor')
if floor:by_name['StaticMeshActor_0'].static_mesh_component.set_material(0,floor)
u.get_editor_subsystem(u.UnrealEditorSubsystem).set_level_viewport_camera_info(u.Vector(925,-510,110),u.Rotator(pitch=10,yaw=90))
assert ls.save_current_level()
u.log('HALLWAY_TUNED')
