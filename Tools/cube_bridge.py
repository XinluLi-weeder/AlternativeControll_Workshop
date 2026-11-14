"""Latest-state bridge for the five-encoder controller; never queue old rotations."""
import argparse
import math
import threading
import time
from device_bridge import DeviceClient, parse_line


class LatestInput:
    def __init__(self):
        self.lock = threading.Lock()
        self.angles = None
        self.received = 0.
        self.shake = 0.
        self.orientation = None
        self.orientation_time = 0.
        self.center = False
        self.debug_line = 'bridge waiting for serial data'
        self.debug_time = time.monotonic()
        self.downstream = ''

    def accept(self, line, now=None):
        now = time.monotonic() if now is None else now
        if not line.strip():
            return
        command = None if line.startswith('#') else parse_line(line)
        with self.lock:
            self.debug_line,self.debug_time=line,now
            if command is None:
                return
            if command['cmd'] == 'angles':
                self.angles, self.received = command['degrees'], now
            elif command.get('_gesture'):
                self.shake = now
            elif command['cmd'] == 'orientation':
                self.orientation, self.orientation_time = command['quaternion'], now
            elif command['cmd'] == 'center':
                self.center = True

    def mark_downstream(self, line):
        with self.lock:
            self.downstream = line.strip()

    def take_debug(self):
        with self.lock:
            return self.debug_line,self.debug_time,self.downstream

    def take_view(self):
        with self.lock:
            result = self.orientation, self.orientation_time, self.center
            self.center = False
            return result

    def take(self):
        with self.lock:
            result = self.angles, self.received, self.shake
            self.shake = 0.
            return result


class AngleMapping:
    def __init__(self):
        self.epoch = None
        self.offset = None

    def map(self, physical, state):
        if state.get('busy') or state.get('transitioning'):
            self.offset = None
            return None
        epoch = state['stage_epoch']
        if self.offset is None or epoch != self.epoch:
            self.offset = [g-p for g, p in zip(state['angles'], physical)]
            self.epoch = epoch
        return [(p+o) % 360 for p, o in zip(physical, self.offset)]


class ViewMapping:
    """Relative quaternion: sensor X points forward, Z up in the neutral pose."""
    def __init__(self, yaw_gain=1., pitch_gain=1.):
        self.reference = None
        self.yaw_gain, self.pitch_gain = yaw_gain, pitch_gain
        self.yaw = self.pitch = 0.
        self.base_yaw = self.base_pitch = 0.
        self.last_time = None

    def map(self, quaternion, state, now, center=False):
        if state.get('transitioning'):
            self.reference = None
            return None
        length = math.sqrt(sum(v*v for v in quaternion))
        w,x,y,z = [v/length for v in quaternion]
        if self.reference is None or center:
            self.reference = (w,-x,-y,-z)
            self.base_yaw = 0. if center else state.get('view_yaw', 0.)
            self.base_pitch = 10. if center else state.get('view_pitch', 10.)
            self.yaw,self.pitch = self.base_yaw,self.base_pitch
            self.last_time = now
        a,b,c,d = self.reference
        # reference inverse multiplied by current quaternion
        rw,rx,ry,rz = a*w-b*x-c*y-d*z,a*x+b*w+c*z-d*y,a*y-b*z+c*w+d*x,a*z+b*y-c*x+d*w
        fx,fy,fz = 1-2*(ry*ry+rz*rz),2*(rx*ry+rw*rz),2*(rx*rz-rw*ry)
        yaw = self.base_yaw+math.degrees(math.atan2(fy,fx))*self.yaw_gain
        if math.hypot(fx,fy)<.05:
            yaw=self.yaw
        pitch = max(-75.,min(75.,self.base_pitch+math.degrees(math.asin(max(-1.,min(1.,fz))))*self.pitch_gain))
        alpha = 1-math.exp(-max(0.,now-self.last_time)/.08)
        self.yaw += ((yaw-self.yaw+180)%360-180)*alpha
        self.yaw = (self.yaw+180)%360-180
        self.pitch += (pitch-self.pitch)*alpha
        self.last_time=now
        return {'cmd':'view','yaw':self.yaw,'pitch':self.pitch}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True, help='COM5 or /dev/cu.usbmodem...')
    parser.add_argument('--yaw-gain', type=float, default=1., help='Negative reverses horizontal look')
    parser.add_argument('--pitch-gain', type=float, default=1., help='Negative reverses vertical look')
    args = parser.parse_args()
    import serial
    client = DeviceClient()
    state = client.send({'cmd': 'status'})
    if state.get('mode') != 'hallway' or 'angles' not in state:
        raise SystemExit('Start the updated Hallyway level in Play first.')
    latest, mapping = LatestInput(), AngleMapping()
    view_mapping = ViewMapping(args.yaw_gain,args.pitch_gain)
    with serial.Serial(args.serial, 115200, timeout=.05) as port:
        # Opening USB can reboot the board. The reader continues draining during UDP waits.
        stop = threading.Event()
        errors = []
        def reader():
            pending = bytearray()
            try:
                while not stop.is_set():
                    pending.extend(port.read(max(1, min(port.in_waiting, 512))))
                    while b'\n' in pending:
                        raw, _, pending = pending.partition(b'\n')
                        line = raw.decode('ascii', errors='replace').strip()
                        if line.startswith('#'):
                            print(line, flush=True)
                        else:
                            try:
                                latest.accept(line)
                            except ValueError:
                                pass
                    if len(pending) > 1024:
                        pending.clear()
            except Exception as exc:
                errors.append(exc)
        thread = threading.Thread(target=reader, daemon=True)
        thread.start()
        client.on_feedback = lambda event: port.write(event.encode('ascii'))
        next_play = 0.
        last_frame = 0.
        last_view = 0.
        last_debug = 0.
        last_downstream = ''
        center_pending = False
        def send_back_to_board(event):
            nonlocal last_downstream
            port.write(event.encode('ascii'))
            last_downstream=event.strip()
            latest.mark_downstream(last_downstream)
        client.on_feedback=send_back_to_board
        try:
            while not errors:
                state = client.send({'cmd': 'status'}, timeout=1)
                physical, timestamp, shake = latest.take()
                now = time.monotonic()
                orientation, view_time, center = latest.take_view()
                debug_line,debug_time,downstream=latest.take_debug()
                if debug_time>last_debug or downstream!=last_downstream:
                    try:
                        client.send({'cmd':'debug','text':debug_line,'downstream':downstream},timeout=.4)
                    except (ValueError,TimeoutError):
                        pass
                    last_debug=debug_time
                    last_downstream=downstream
                center_pending |= center
                if center_pending:
                    view_mapping.reference = None
                if orientation is not None and now-view_time<.5 and view_time>last_view:
                    view_command = view_mapping.map(orientation,state,now,center_pending)
                    if view_command:
                        center_pending = False
                        try:
                            client.send(view_command, timeout=.4)
                        except ValueError:
                            view_mapping.reference=None
                    last_view=view_time
                elif now-view_time>=.5:
                    view_mapping.reference=None
                if physical is None or now-timestamp > .5:
                    mapping.offset = None
                    if now >= next_play:
                        port.write(b'PLAY\n')
                        next_play = now+2
                    time.sleep(.02)
                    continue
                was_synced = mapping.offset is not None and mapping.epoch == state['stage_epoch']
                degrees = mapping.map(physical, state)
                if degrees is not None and timestamp > last_frame:
                    try:
                        state = client.send({'cmd': 'angles', 'degrees': degrees}, timeout=.4)
                        # A shake is short-lived and valid only after an established baseline.
                        if was_synced and shake and now-shake < .2 and state.get('ready_item', -1) >= 0:
                            client.send({'cmd': 'assemble', '_gesture': True}, timeout=.2)
                    except ValueError:
                        mapping.offset = None
                    last_frame = timestamp
                time.sleep(.01)
            if errors:
                raise errors[0]
        finally:
            stop.set()
            thread.join(timeout=1)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        pass
