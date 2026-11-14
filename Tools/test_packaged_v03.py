"""Black-box checks against the packaged Windows game's loopback diagnostics.

Launch the build at 1280x720, with no bridge or hardware sender active.
Exercises actual menu hit boxes and the native OSC receiver, then quits via the menu.
"""
import json
import socket
import time
from device_bridge import DeviceClient
from osc_bridge import _osc_packet


def main():
    client=DeviceClient()
    sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    def state(): return client.send({'cmd':'status'},timeout=3)
    def click(x,y):
        client.send({'cmd':'ui','action':'click','x':x,'y':y},timeout=3)
        time.sleep(.15)
        return state()
    def capture():
        client.send({'cmd':'ui','action':'screenshot'},timeout=3)
        time.sleep(.4)
    def stream(angles,yaw=0,pitch=0,acc=0,duration=.4):
        end=time.monotonic()+duration
        while time.monotonic()<end:
            sock.sendto(_osc_packet('/rotary',angles),('127.0.0.1',8000))
            sock.sendto(_osc_packet('/imu',[yaw,0,pitch,acc]),('127.0.0.1',8000))
            time.sleep(.02)
    first=state()
    assert first['menu_open'] and not first['game_started'] and first['control_mode']=='mouse',first
    capture()
    started=click(640,308)
    assert started['game_started'] and not started['menu_open']
    client.send({'cmd':'ui','action':'menu'})
    settings=click(640,365)
    assert settings['settings_open']
    selected=click(640,365)
    assert selected['control_mode']=='controller'
    time.sleep(2.2)
    assert not state()['controller_online'], 'Stop the real hardware sender during this automated test'
    stream([0]*5,duration=.5)
    connected=state()
    assert connected['controller_online'],connected
    capture()
    click(640,424) # back
    click(640,308) # resume
    stream([0]*5,duration=.4)
    base=state()
    stream([90,180,270,45,135],20,10,duration=.7)
    moved=state()
    assert all(abs(a-b)<.1 for a,b in zip(moved['angles'],[90,180,270,45,135])),moved
    assert abs((moved['view_yaw']-base['view_yaw']+180)%360-180-50)<1,moved
    assert abs(moved['view_pitch']-base['view_pitch']-20)<1,moved
    capture()
    # Bed target: B=90, C=270, E=180. Two separated Z acceleration peaks confirm it.
    target=[0,90,270,0,180]
    stream(target,20,10,duration=.5)
    assert state()['ready_item']==3
    stream(target,20,10,8,.08)
    stream(target,20,10,0,.1)
    stream(target,20,10,-8,.08)
    time.sleep(1.8)
    assembled=state()
    assert assembled['joined'][3] and assembled['completion_serial']>0,assembled
    # A stale connection must not stay green; reconnect establishes a neutral pose.
    time.sleep(2.2)
    assert not state()['controller_online']
    stream(target,20,10,duration=.5)
    assert state()['controller_online']
    client.send({'cmd':'ui','action':'menu'})
    click(640,365)
    mouse=click(640,308)
    assert mouse['control_mode']=='mouse'
    click(640,424)
    click(640,308)
    before=state()
    stream([0]*5,90,50,duration=.4)
    after=state()
    assert before['angles']==after['angles'] and before['view_yaw']==after['view_yaw']
    capture()
    print('PACKAGED_V03_PASS',json.dumps({'menu':True,'settings':True,'five_faces':moved['angles'],
          'camera_gain':True,'shake_bed_restored':True,'disconnect_reconnect':True,'mouse_ignores_OSC':True}),flush=True)
    client.send({'cmd':'ui','action':'menu'})
    # Quit can close before acknowledgement; never replay this click into another run.
    client.sequence+=1
    payload={'cmd':'ui','action':'click','x':640,'y':424,'session':client.session,'seq':client.sequence}
    client.sock.sendto(json.dumps(payload).encode(),client.address)
    time.sleep(1)
    try: state()
    except TimeoutError: print('QUIT_BUTTON_PASS',flush=True)
    else: raise AssertionError('Quit button did not close game')
    sock.close()


if __name__=='__main__': main()
