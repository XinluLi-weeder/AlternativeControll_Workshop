"""Arduino serial -> reliable local UDP commands. No serial dependency in --stdin mode."""
import argparse
import json
import socket
import sys
import time
import uuid


def parse_line(line):
    fields = [p.strip().upper() for p in line.strip().split(',')]
    if len(fields) == 3 and fields[0] == 'ROTATE':
        if fields[1] not in ('A', 'B', 'C') or fields[2] not in ('1', '+1', '-1'):
            raise ValueError('Use ROTATE,A,1 or ROTATE,B,-1 (faces A/B/C)')
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
    source.add_argument('--solve-demo', action='store_true', help='Exercise hardware commands and solve all three objects')
    parser.add_argument('--baud', type=int, default=115200)
    args = parser.parse_args()
    client = DeviceClient()
    if args.solve_demo:
        lines = ['RESET', 'ROTATE,A,1', 'ROTATE,B,-1', 'ASSEMBLE',
                 'ROTATE,A,1', 'ROTATE,C,1', 'ROTATE,C,1', 'ASSEMBLE',
                 'ROTATE,B,-1', 'ROTATE,B,-1', 'ROTATE,C,-1', 'ROTATE,C,-1', 'ASSEMBLE']
        for line in lines:
            print(line, client.send(parse_line(line)), flush=True)
        return
    if args.stdin:
        print('Enter ROTATE,A,1 / ROTATE,B,-1 / ASSEMBLE / UNDO / RESET. Ctrl+Z then Enter exits.')
        lines = sys.stdin
    else:
        try:
            import serial
        except ImportError:
            raise SystemExit('Serial mode needs pyserial: python -m pip install pyserial')
        port = serial.Serial(args.serial, args.baud, timeout=1)
        lines = (port.readline().decode('ascii', errors='replace') for _ in iter(int, 1))
    for line in lines:
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
