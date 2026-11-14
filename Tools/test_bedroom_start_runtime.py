"""Use a disposable Hallyway1 Play session; completes all five new room puzzles."""
import json
import time
from pathlib import Path
from device_bridge import DeviceClient

c=DeviceClient()
s=c.send({'cmd':'status'})
assert s['stage']==1, s
manifest=json.loads((Path(__file__).resolve().parents[1]/'Documentation/Hallyway1BedroomPuzzle.json').read_text(encoding='utf-8'))
assert s['camera']==dict(zip(('x','y','z'),manifest['camera'])), s['camera']
assert not s['transitioning'] and not s['blackout_visible']
position=s['camera']
assert s['layout_valid'], 'Puzzle fragments exceed bedroom bounds during a full turn'
c.send({'cmd':'reset'})
angles=[33.75,67.5,101.25,135,168.75]
s=c.send({'cmd':'angles','degrees':angles})
assert s['angles']==angles,s['angles']
time.sleep(.25)
s=c.send({'cmd':'status'})
assert s['angles']==angles
s=c.send({'cmd':'view','yaw':42,'pitch':25})
assert s['view_yaw']==42 and s['view_pitch']==25 and s['camera']==position
changed=angles[:];changed[1]=90
s=c.send({'cmd':'angles','degrees':changed})
assert s['angles']==changed and s['view_yaw']==42
c.send({'cmd':'view','yaw':0,'pitch':10})
manifest=json.loads((Path(__file__).resolve().parents[1]/'Documentation/Hallyway1BedroomPuzzle.json').read_text(encoding='utf-8'))
for item in [8,4,3,5]:
    s=c.send({'cmd':'status'})
    goals=[None]*5
    for part in manifest['items']:
        if part['item']==item:goals[part['face']]=part['goal_degrees']
    degrees=s['angles'][:]
    for face,goal in enumerate(goals):
        if goal is not None:degrees[face]=goal
    c.send({'cmd':'angles','degrees':degrees})
    time.sleep(.25)
    s=c.send({'cmd':'status'})
    assert s['ready_item']==item,(item,s)
    c.send({'cmd':'assemble'})
    deadline=time.monotonic()+5
    while time.monotonic()<deadline:
        s=c.send({'cmd':'status'})
        if not s['busy']:break
        time.sleep(.05)
    assert s['joined'][item],(item,s['joined'])
print('BEDROOM_PUZZLE_PASS: wider fixed camera, five simultaneous inputs, four full-scale objects assembled one at a time')
