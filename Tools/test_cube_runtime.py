"""Run only against a disposable Hallyway Play session: resets gameplay state."""
import time
from device_bridge import DeviceClient
from test_hallway_runtime import GOALS, ROOM_ITEMS, wait_idle


def main():
    c = DeviceClient()
    s = c.send({'cmd': 'status'})
    assert s['mode'] == 'hallway' and s['stage'] == 0
    c.send({'cmd': 'reset'})
    s = c.send({'cmd': 'angles', 'degrees': [0, 33.75, 0, 0, 0]})
    assert abs(s['angles'][1]-33.75) < .001
    assert s['ready_item'] == -1
    # Correct nearest detent is not enough: outside the five-degree tolerance fails.
    c.send({'cmd': 'angles', 'degrees': [0, 83, 270, 0, 0]})
    time.sleep(.25)
    try:
        c.send({'cmd': 'assemble', '_gesture': True})
    except ValueError:
        pass
    else:
        raise AssertionError('Outside-tolerance shake accepted')
    s = c.send({'cmd': 'angles', 'degrees': [0, 86, 273, 0, 0]})
    assert s['ready_item'] == -1, 'Must hold still before assembling'
    time.sleep(.25)
    s = c.send({'cmd': 'status'})
    assert s['ready_item'] == 0
    c.send({'cmd': 'assemble', '_gesture': True})
    try:
        c.send({'cmd': 'assemble', '_gesture': True})
    except ValueError:
        pass
    else:
        raise AssertionError('Busy shake was queued')
    wait_idle(c)
    c.send({'cmd': 'reset'})
    # Exercise every target through the new continuous interface and room transition.
    for stage in range(2):
        for item in ROOM_ITEMS[stage]:
            s = wait_idle(c)
            angles = s['angles'][:]
            for face, goal in GOALS[item].items():
                angles[face] = goal*90
            c.send({'cmd': 'angles', 'degrees': angles})
            time.sleep(.25)
            c.send({'cmd': 'assemble', '_gesture': True})
            s = wait_idle(c)
            assert s['joined'][item]
            print('CONTINUOUS_ASSEMBLED', stage, item, flush=True)
        if stage == 0:
            deadline = time.monotonic()+20
            while time.monotonic() < deadline:
                s = c.send({'cmd': 'status'})
                if s['stage'] == 1 and not s['busy']:
                    break
                time.sleep(.1)
            assert s['stage'] == 1
    assert all(s['joined'])
    print('CUBE_RUNTIME_PASS: fractional angles, tolerance, hold, busy shake, 10 objects, transition', flush=True)


if __name__ == '__main__':
    main()
