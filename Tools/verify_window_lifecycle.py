"""Run inside disposable PIE: test animation shape and immutable completed window."""
import unreal as u
import time,json,math
from pathlib import Path
subsystem=u.get_editor_subsystem(u.UnrealEditorSubsystem)
world=subsystem.get_game_world() if subsystem else u.find_object(None,'/Game/Levels/Hallyway1.Hallyway1')
actors=u.GameplayStatics.get_all_actors_of_class(world,u.Actor)
puzzle=next(a for a in actors if a.get_class().get_name()=='HallwayPuzzle')
parts=[a for a in actors if a.actor_has_tag('RH_ITEM_8')]
completed=[a for a in actors if a.actor_has_tag('RH_SOURCE_8') or a.actor_has_tag('RH_PROP_8')]
shell=[a for a in actors if a.actor_has_tag('RH_ENCLOSURE')]
tethers=puzzle.get_components_by_class(u.StaticMeshComponent)
assert len(parts)==7 and len(completed)==5 and len(shell)==7
def transform(a):
    r=a.get_actor_rotation()
    return [*a.get_actor_location().to_tuple(),*a.get_actor_scale3d().to_tuple(),r.pitch,r.yaw,r.roll]
homes={a.get_name():transform(a) for a in completed}
scales={a.get_name():list(a.get_actor_scale3d().to_tuple()) for a in parts}
report={'phase':'started','samples':0,'checks':[]}
state={'phase':0,'time':time.monotonic(),'shape':None}
assert puzzle.reset_stage()
assert puzzle.set_face_angles([270,0,0,180,90])
def finish(error=None):
    report['phase']='FAIL' if error else 'PASS'
    if error:report['error']=str(error)
    Path(u.Paths.project_dir(),'Documentation/WindowLifecycleTest.json').write_text(json.dumps(report,indent=2))
    u.unregister_slate_post_tick_callback(window_test_handle)
    u.log('WINDOW_LIFECYCLE_'+report['phase'])
def check_home():
    for a in completed:
        assert not a.get_editor_property('hidden'),a.get_name()+' hidden'
        assert max(abs(x-y) for x,y in zip(transform(a),homes[a.get_name()]))<.01,a.get_name()+' moved after assembly'
def step(dt):
    try:
        elapsed=time.monotonic()-state['time']
        for a in shell:assert not a.get_editor_property('hidden')
        if state['phase']==0 and elapsed>.6:
            assert puzzle.assemble()
            state.update(phase=1,time=time.monotonic())
        elif state['phase']==1:
            for a in parts:assert max(abs(x-y) for x,y in zip(a.get_actor_scale3d().to_tuple(),scales[a.get_name()]))<.001
            if .35<elapsed<1.1:
                shape=[(a.get_actor_location()-parts[0].get_actor_location()).length() for a in parts]
                if state['shape'] is not None:assert max(abs(x-y) for x,y in zip(shape,state['shape']))<.05,'Assembly broke rigid shape'
                state['shape']=shape;report['samples']+=1
            if elapsed>1.8:
                check_home()
                assert all(a.get_editor_property('hidden') for a in parts)
                assert sum(bool(t.get_editor_property('visible')) for t in tethers)==8,'Window tethers still visible'
                report['checks'].append('rigid landing, constant scale, original upright transforms')
                assert puzzle.set_face_angles([13,47,92,271,150])
                state.update(phase=2,time=time.monotonic())
        elif state['phase']==2:
            check_home()
            if elapsed>.5:
                assert puzzle.rotate_face(3,1)
                state.update(phase=3,time=time.monotonic())
        elif state['phase']==3:
            check_home()
            if elapsed>1.3:
                report['checks'].append('window fixed during analog and animated wall rotation')
                assert puzzle.reset_stage()
                state.update(phase=4,time=time.monotonic())
        elif state['phase']==4 and elapsed>.3:
            assert all(a.get_editor_property('hidden') for a in completed)
            assert all(not a.get_editor_property('hidden') for a in parts)
            assert sum(bool(t.get_editor_property('visible')) for t in tethers)==15,'Reset tether count incorrect'
            report['checks'].append('reset restores fragments and keeps enclosure visible')
            assert report['samples']>0,'No animation frames sampled'
            finish()
    except Exception as exc:finish(exc)
window_test_handle=u.register_slate_post_tick_callback(step)
