"""Arduino serial -> reliable local UDP commands. No serial dependency in --stdin mode."""
import argparse
import json
import math
import socket
import sys
import time
import uuid


def serial_lines(port):
    """Yield idle ticks for feedback polling; never interpret a partial serial line."""
    pending = bytearray()
    while True:
        data = port.read(max(1, min(port.in_waiting, 256)))
        if not data:
            yield ''
            continue
        pending.extend(data)
        while b'\n' in pending:
            line, _, remaining = pending.partition(b'\n')
            pending = bytearray(remaining)
            yield line.decode('ascii', errors='replace')
        if len(pending) > 512:
            pending.clear()
        yield ''


def parse_line(line):
    fields = [p.strip().upper() for p in line.strip().split(',')]
    if len(fields) == 3 and fields[0] == 'VIEW':
        yaw, pitch = float(fields[1]), float(fields[2])
        if not math.isfinite(yaw) or not math.isfinite(pitch):
            raise ValueError('VIEW angles must be finite degrees')
        return {'cmd': 'view', 'yaw': yaw, 'pitch': pitch}
    if len(fields) == 3 and fields[0] == 'ROTATE':
        if fields[1] not in ('A', 'B', 'C', 'D', 'E') or fields[2] not in ('1', '+1', '-1'):
            raise ValueError('Use ROTATE,A,1 or ROTATE,E,-1 (faces A/B/C/D/E)')
        return {'cmd': 'rotate', 'face': ord(fields[1]) - ord('A'), 'dir': int(fields[2])}
    if len(fields) == 1 and fields[0] in ('ASSEMBLE', 'UNDO', 'RESET'):
        return {'cmd': fields[0].lower()}
    raise ValueError('Expected ROTATE,A,1 / ASSEMBLE / UNDO / RESET')


class DeviceClient:
    def __init__(self, port=19872):
        self.address = ('127.0.0.1', port)
        self.session = uuid.uuid4().hex
        self.sequence = 0
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(.3)
        self.last_completion = None
        self.on_feedback = None

    def feedback(self, ack):
        serial = ack.get('completion_serial')
        if not isinstance(serial, int):
            return
        if self.last_completion is not None and serial > self.last_completion and self.on_feedback:
            self.on_feedback('HAPTIC,180,140\n')
        self.last_completion = serial

    def send(self, command, timeout=8):
        self.sequence += 1
        payload = dict(command, session=self.session, seq=self.sequence)
        encoded = json.dumps(payload).encode('utf-8')
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.sock.sendto(encoded, self.address)
            try:
                data, sender = self.sock.recvfrom(2048)
                if sender != self.address:
                    continue
                ack = json.loads(data)
                if ack.get('seq') != self.sequence:
                    continue
                self.feedback(ack)
                if ack.get('status') in ('ok', 'duplicate'):
                    return ack
                if ack.get('status') == 'invalid':
                    raise ValueError('Command rejected: check alignment or command arguments')
            except (socket.timeout, json.JSONDecodeError):
                pass
            time.sleep(.06)
        raise TimeoutError('No acceptance from Unreal. Enter Play and check UDP port 19872.')


def main():
    parser = argparse.ArgumentParser()
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--stdin', action='store_true', help='Type commands without Arduino')
    source.add_argument('--serial', metavar='COM_PORT')
    source.add_argument('--solve-demo', action='store_true', help='Exercise the five-face protocol and solve all six objects')
    source.add_argument('--status', action='store_true', help='Print current angles, progress and completion counter')
    parser.add_argument('--baud', type=int, default=115200)
    args = parser.parse_args()
    client = DeviceClient()
    if args.status:
        print(json.dumps(client.send({'cmd':'status'}), indent=2))
        return
    if args.solve_demo:
        client.on_feedback=lambda event: print(event.strip(),flush=True)
        state=client.send({'cmd':'status'})
        state=client.send({'cmd':'reset'})
        goals=[[1,3,-1,-1,-1],[2,-1,2,-1,-1],[-1,1,0,-1,-1],[-1,-1,1,3,-1],[3,-1,-1,1,2],[-1,2,3,-1,1]]
        for target in goals[:len(state['joined'])]:
            for face, goal in enumerate(target[:len(state['turns'])]):
                if goal < 0: continue
                delta=(goal-state['turns'][face])%4
                directions=[-1] if delta==3 else [1]*delta
                for direction in directions:
                    line=f'ROTATE,{chr(65+face)},{direction}'
                    state=client.send(parse_line(line))
                    print(line,state,flush=True)
            state=client.send({'cmd':'assemble'})
            print('ASSEMBLE',state,flush=True)
        while state.get('busy'):
            time.sleep(.1)
            state=client.send({'cmd':'status'})
        if not all(state['joined']): raise SystemExit('Solve failed')
        print('ALL_OBJECTS_RESTORED',flush=True)
        return
    if args.stdin:
        print('Faces A-E: ROTATE,A,1 / ROTATE,E,-1 / ASSEMBLE / UNDO / RESET. Ctrl+Z then Enter exits.')
        lines = sys.stdin
    else:
        try:
            import serial
        except ImportError:
            raise SystemExit('Serial mode needs pyserial: python -m pip install pyserial')
        port = serial.Serial(args.serial, args.baud, timeout=.05)
        client.on_feedback=lambda event: port.write(event.encode('ascii'))
        # Establish a baseline, so attaching hardware never replays an old completion.
        client.send({'cmd':'status'})
        lines = serial_lines(port)
    last_poll=0.
    for line in lines:
        if args.serial and time.monotonic()-last_poll>.1:
            client.send({'cmd':'status'})
            last_poll=time.monotonic()
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        try:
            print(line.strip(), client.send(parse_line(line)), flush=True)
        except ValueError as e:
            print(f'SKIPPED: {e}', file=sys.stderr)
        except TimeoutError as e:
            # Stop instead of replaying an old backlog after a game reset/reconnect.
            raise SystemExit(str(e))


if __name__ == '__main__':
    main()
