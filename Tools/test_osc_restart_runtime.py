"""Exercise real PIE restarts while one OSC bridge process stays alive.

Run with the editor open on Hallyway1 and the production bridge stopped.
Uses a separate loopback OSC port and leaves PIE running with a fresh puzzle.
"""
import json
from pathlib import Path
import socket
import subprocess
import sys
import time

from device_bridge import DeviceClient
from osc_bridge import _osc_packet

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, 'D:/DKU_Academic/UE_5.7/Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python')
import remote_execution


def editor(code):
    remote = remote_execution.RemoteExecution()
    remote.start()
    try:
        time.sleep(2)
        nodes = [n for n in remote.remote_nodes if n.get('project_name') == 'RoomShiftDemo']
        assert len(nodes) == 1, nodes
        remote.open_command_connection(nodes[0]['node_id'])
        result = remote.run_command('import unreal as u\n' + code)
        assert result['success'], result
    finally:
        remote.stop()


def main():
    client = DeviceClient()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    target = ('127.0.0.1', 8002)
    log_path = ROOT / 'Saved/Logs/OSCPlayRestartTest.log'
    results = []

    def stream(angles, yaw, pitch, duration=1.0):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            sock.sendto(_osc_packet('/rotary', angles), target)
            sock.sendto(_osc_packet('/imu', [yaw, 0, pitch, 0]), target)
            time.sleep(.025)

    editor("ls=u.get_editor_subsystem(u.LevelEditorSubsystem)\n"
           "if not ls.is_in_play_in_editor(): ls.editor_play_simulate()")
    initial = client.send({'cmd':'status'})
    client.send({'cmd':'reset'})
    with log_path.open('w', encoding='utf-8') as log:
        bridge = subprocess.Popen([sys.executable, str(ROOT/'Tools/osc_bridge.py'),
                                   '--bind','127.0.0.1','--port','8002','--allow-loopback'],
                                  stdout=log, stderr=subprocess.STDOUT)
        try:
            time.sleep(1)
            last_run = initial['run_id']
            for cycle in range(3):
                if cycle:
                    editor('u.get_editor_subsystem(u.LevelEditorSubsystem).editor_request_end_play()')
                    time.sleep(1.5)
                    assert bridge.poll() is None, 'Bridge exited when PIE stopped'
                    editor('u.get_editor_subsystem(u.LevelEditorSubsystem).editor_play_simulate()')
                    state = client.send({'cmd':'status'})
                    assert state['run_id'] != last_run, 'PIE reused the run identity'
                    last_run = state['run_id']
                physical = [45.0 * cycle] * 5
                stream(physical, 10.0 * cycle, 5.0 * cycle, 1.5)
                before = client.send({'cmd':'status'})
                moved = [(x + 90) % 360 for x in physical]
                stream(moved, 10.0 * cycle + 20, 5.0 * cycle + 10)
                after = client.send({'cmd':'status'})
                deltas = [(a-b+180)%360-180 for a,b in zip(after['angles'],before['angles'])]
                yaw = (after['view_yaw']-before['view_yaw']+180)%360-180
                pitch = after['view_pitch']-before['view_pitch']
                assert all(abs(d-90)<.1 for d in deltas), (cycle,before,after)
                assert abs(yaw-50)<1 and abs(pitch-20)<1, (cycle,yaw,pitch)
                results.append({'cycle':cycle,'run_id':last_run,'wall_deltas':deltas,
                                'yaw_delta':yaw,'pitch_delta':pitch})
                print('PLAY_CYCLE_PASS', results[-1], flush=True)
            assert bridge.poll() is None
        finally:
            bridge.terminate()
            bridge.wait(timeout=5)
            sock.close()
            client.send({'cmd':'reset'})
            client.send({'cmd':'view','yaw':0,'pitch':-18})
    (ROOT/'Documentation/OSCPlayRestartVerification.json').write_text(json.dumps(results,indent=2), encoding='utf-8')
    print('OSC_RESTART_PASS: same bridge survived two PIE restarts; five walls and amplified view verified')


if __name__ == '__main__':
    main()
