"""Integration test against a running Hallyway game; uses the same UDP interface as Arduino."""
import argparse
import time
from device_bridge import DeviceClient

GOALS=[{1:1,2:3},{1:2,4:1},{2:2,4:3},{0:1,2:2,4:3},{0:2,1:3,3:1},{1:1,3:2},
       {1:3,2:1},{1:1,4:2},{0:3,2:1,4:2},{1:2,3:3,4:1}]
ROOM_ITEMS=[[0,1,2,6,7],[3,4,5,8,9]]

def wait_idle(c, timeout=20):
    deadline=time.monotonic()+timeout
    while time.monotonic()<deadline:
        s=c.send({'cmd':'status'})
        if not s['busy']:return s
        time.sleep(.1)
    raise AssertionError('Animation did not finish')

def solve(c,item):
    s=wait_idle(c)
    for face,target in GOALS[item].items():
        delta=(target-s['turns'][face])%4
        for direction in ([-1] if delta==3 else [1]*delta):
            c.send({'cmd':'rotate','face':face,'dir':direction})
            s=wait_idle(c)
    c.send({'cmd':'assemble'})
    s=wait_idle(c)
    assert s['joined'][item]
    print('ASSEMBLED',item,'counter',s['completion_serial'],flush=True)
    return s

def main():
    p=argparse.ArgumentParser();p.add_argument('--stage',type=int,choices=[0,1]);p.add_argument('--item',type=int,choices=range(10));args=p.parse_args()
    c=DeviceClient();s=c.send({'cmd':'status'});assert s.get('mode')=='hallway'
    assert s['layout_valid'], 'A fragment crosses the room shell during a rotation'
    if args.item is not None:solve(c,args.item);return
    stage=s['stage'] if args.stage is None else args.stage
    assert s['stage']==stage
    c.send({'cmd':'reset'});s=wait_idle(c)
    if stage==0:
        assert s['blackout_visible'] and s['enabled_faces']==[False,True,True,False,True]
        camera=s['camera']
        c.send({'cmd':'view','yaw':35,'pitch':15})
        assert c.send({'cmd':'status'})['camera']==camera
        c.send({'cmd':'view','yaw':0,'pitch':10})
        try:c.send({'cmd':'rotate','face':0,'dir':1})
        except ValueError:pass
        else:raise AssertionError('Locked face accepted')
    try:c.send({'cmd':'assemble'})
    except ValueError:pass
    else:raise AssertionError('Misaligned furniture assembled')
    initial=s['completion_serial']
    for item in ROOM_ITEMS[stage]:s=solve(c,item)
    assert s['completion_serial']==initial+5
    if stage==0:
        deadline=time.monotonic()+20
        transition_seen=False
        while time.monotonic()<deadline:
            s=c.send({'cmd':'status'});transition_seen |= s['transitioning']
            if s['stage']==1:break
            time.sleep(.05)
        assert transition_seen and s['stage']==1 and not s['blackout_visible']
        assert s['camera']['y']>600 and all(s['joined'][i] for i in ROOM_ITEMS[0]) and not any(s['joined'][i] for i in ROOM_ITEMS[1])
        assert all(s['enabled_faces'])
    else:
        assert all(s['joined'])
        c.send({'cmd':'undo'});s=wait_idle(c);assert not s['joined'][9] and all(s['joined'][:9])
        c.send({'cmd':'assemble'});s=wait_idle(c);assert all(s['joined'])
    print('HALLWAY_STAGE_TEST_PASS',stage,s,flush=True)

if __name__=='__main__':main()
