"""Disposable PIE test: current firmware OSC through a separate local bridge port."""
import socket,struct,subprocess,sys,time
from pathlib import Path
from device_bridge import DeviceClient
from osc_bridge import _osc_packet

def string(s):
    b=s.encode()+b'\0'
    return b+b'\0'*((-len(b))%4)
c=DeviceClient()
c.send({'cmd':'reset'})
p=subprocess.Popen([sys.executable,str(Path(__file__).with_name('osc_bridge.py')),'--port','8001','--allow-loopback'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
try:
    time.sleep(2)
    for face in ('B','C'):
        sock.sendto(string('/rotate')+string(',si')+string(face)+struct.pack('>i',1),('127.0.0.1',8001))
    deadline=time.monotonic()+7
    while time.monotonic()<deadline:
        sock.sendto(_osc_packet('/view',[35,12,0]),('127.0.0.1',8001))
        time.sleep(.1)
    state=c.send({'cmd':'status'})
    assert state['angles'][1:3]==[90,90],state
    assert abs(state['view_yaw']-35)<.1 and abs(state['view_pitch']-12)<.1,state
    print('CURRENT_OSC_PASS: /rotate string+int, two queued faces, /view absolute camera')
finally:
    p.terminate();p.wait(timeout=5);sock.close()
    c.send({'cmd':'reset'})
    c.send({'cmd':'view','yaw':0,'pitch':8})
