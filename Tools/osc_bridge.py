"""Receive the legacy ESP32 OSC protocol and forward it to the Hallway game."""
import argparse
import math
import socket
import struct
import threading
import time
from collections import deque

from device_bridge import DeviceClient


def _read_osc_string(packet, offset):
    end = packet.find(b"\0", offset)
    if end < 0:
        raise ValueError("unterminated OSC string")
    value = packet[offset:end].decode("ascii")
    return value, (end + 4) & ~3


def parse_osc_message(packet):
    """Parse the float/int OSC messages emitted by Arduino/new_rotate.ino."""
    address, offset = _read_osc_string(packet, 0)
    tags, offset = _read_osc_string(packet, offset)
    if not tags.startswith(","):
        raise ValueError("OSC type tag string is missing")
    values = []
    for tag in tags[1:]:
        if tag in "fi":
            if offset + 4 > len(packet):
                raise ValueError("truncated OSC argument")
            values.append(struct.unpack(">f" if tag == "f" else ">i", packet[offset:offset + 4])[0])
            offset += 4
        elif tag == 's':
            value, offset = _read_osc_string(packet, offset)
            values.append(value)
        else:
            raise ValueError(f"unsupported OSC type: {tag}")
    if offset != len(packet):
        raise ValueError("unexpected bytes after OSC arguments")
    if not all(isinstance(value, str) or math.isfinite(float(value)) for value in values):
        raise ValueError("OSC values must be finite")
    if address == '/rotate' and tags == ',si' and len(values) == 2:
        if values[0] not in ('A','B','C','D','E') or values[1] not in (-1,1):
            raise ValueError('Invalid face or quarter-turn direction')
        return {'cmd':'rotate','face':ord(values[0])-ord('A'),'dir':values[1]}
    if any(isinstance(value,str) for value in values):
        raise ValueError('Strings are only accepted in /rotate')
    if address == '/view' and len(values) == 3:
        return {'cmd':'view','yaw':float(values[0]),'pitch':max(-75.,min(75.,float(values[1])))}
    if address == "/rotary" and len(values) == 5:
        return {"cmd": "angles", "degrees": [float(v) % 360.0 for v in values]}
    if address == "/imu" and len(values) == 4:
        # new_rotate sends BNO055 Euler X, Y, Z, then linear acceleration Z.
        # Adafruit BNO055 Euler axes are heading, roll, pitch, respectively.
        return {"cmd": "imu", "yaw": float(values[0]), "roll": float(values[1]),
                "pitch": float(values[2]), "acc_z": float(values[3])}
    if address == "/shake" and not values:
        return {"cmd": "shake"}
    raise ValueError(f"unsupported OSC address/argument count: {address} ({len(values)})")


def _osc_packet(address, values):
    """Encode a float-only OSC message (used by --selftest)."""
    def osc_string(value):
        data = value.encode("ascii") + b"\0"
        return data + b"\0" * ((-len(data)) % 4)

    tags = "," + "f" * len(values)
    return osc_string(address) + osc_string(tags) + b"".join(
        struct.pack(">f", float(value)) for value in values)


def local_ipv4_addresses():
    """Best-effort list of this computer's LAN addresses (stdlib only)."""
    addresses = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            addresses.add(info[4][0])
    except OSError:
        pass
    try:
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.connect(("8.8.8.8", 80))
        addresses.add(probe.getsockname()[0])
        probe.close()
    except OSError:
        pass
    return sorted(
        address for address in addresses
        if not address.startswith("127.") and not address.startswith("169.254."))


class AngleMapping:
    """Preserve the game's current angles when the box is first connected."""
    def __init__(self):
        self.epoch = None
        self.offset = None

    def map(self, physical, state):
        if state.get("busy") or state.get("transitioning"):
            self.offset = None
            return None
        epoch = (state.get("run_id"), state["stage_epoch"])
        if self.offset is None or epoch != self.epoch:
            self.offset = [(game - box) % 360.0 for game, box in zip(state["angles"], physical)]
            self.epoch = epoch
        return [(box + offset) % 360.0 for box, offset in zip(physical, self.offset)]


class ViewMapping:
    """Map the old sketch's Euler OSC values to relative camera yaw/pitch."""
    def __init__(self, yaw_gain=2.5, pitch_gain=2.0, pitch_axis="pitch"):
        self.yaw_gain = yaw_gain
        self.pitch_gain = pitch_gain
        self.pitch_axis = pitch_axis
        self.run_id = None
        self.previous_yaw = 0.0
        self.accumulated_yaw = 0.0
        self.reference = None
        self.base_yaw = 0.0
        self.base_pitch = 10.0
        self.yaw = 0.0
        self.pitch = 10.0
        self.last_time = None

    def map(self, imu, state, now):
        if self.run_id != state.get("run_id"):
            self.run_id = state.get("run_id")
            self.reference = None
        if state.get("transitioning"):
            self.reference = None
            return None
        if self.reference is None:
            self.reference = (imu["yaw"], imu[self.pitch_axis])
            self.previous_yaw = imu["yaw"]
            self.accumulated_yaw = 0.0
            self.base_yaw = state.get("view_yaw", 0.0)
            self.base_pitch = state.get("view_pitch", 10.0)
            self.yaw = self.base_yaw
            self.pitch = self.base_pitch
            self.last_time = now
        ref_yaw, ref_pitch = self.reference
        self.accumulated_yaw += (imu["yaw"] - self.previous_yaw + 180.0) % 360.0 - 180.0
        self.previous_yaw = imu["yaw"]
        target_yaw = self.base_yaw + self.accumulated_yaw * self.yaw_gain
        delta_pitch = (imu[self.pitch_axis] - ref_pitch + 180.0) % 360.0 - 180.0
        target_pitch = max(-75.0, min(75.0, self.base_pitch + delta_pitch * self.pitch_gain))
        alpha = 1.0 - math.exp(-max(0.0, now - self.last_time) / 0.04)
        self.yaw += ((target_yaw - self.yaw + 180.0) % 360.0 - 180.0) * alpha
        self.yaw = (self.yaw + 180.0) % 360.0 - 180.0
        self.pitch += (target_pitch - self.pitch) * alpha
        self.last_time = now
        return {"cmd": "view", "yaw": self.yaw, "pitch": self.pitch}


class ShakeDetector:
    """Detect two vertical acceleration peaks from the legacy /imu message."""
    def __init__(self):
        self.armed = True
        self.peaks = 0
        self.first_peak = 0.0
        self.last_shake = -1.5

    def update(self, acc_z, now):
        magnitude = abs(acc_z)
        if now - self.last_shake < 1.5:
            return False
        if magnitude < 2.0:
            self.armed = True
        if magnitude > 6.0 and self.armed:
            self.armed = False
            if self.peaks == 0 or now - self.first_peak > 0.65:
                self.first_peak = now
                self.peaks = 1
            elif now - self.first_peak >= 0.07:
                self.peaks = 0
                self.last_shake = now
                return True
        if self.peaks and now - self.first_peak > 0.65:
            self.peaks = 0
        return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bind", default="0.0.0.0", help="network interface for ESP32 OSC packets")
    parser.add_argument("--port", type=int, default=8000, help="must match laptopPort in new_rotate.ino")
    parser.add_argument("--yaw-gain", type=float, default=2.5, help="camera degrees per BNO yaw degree; negative reverses direction")
    parser.add_argument("--pitch-gain", type=float, default=2.0, help="camera degrees per BNO tilt degree; negative reverses direction")
    parser.add_argument("--pitch-axis", choices=("pitch", "roll"), default="pitch", help="choose roll if the BNO board is mounted sideways")
    parser.add_argument("--allow-loopback", action="store_true", help="accept local 127.0.0.1 OSC test packets; hardware normally arrives from a Wi-Fi IP")
    parser.add_argument("--selftest", action="store_true", help="send synthetic OSC to itself, verify face rotation and view movement in Unreal, then exit")
    args = parser.parse_args()
    print(f"BNO view sensitivity: yaw x{args.yaw_gain:g}, {args.pitch_axis} x{args.pitch_gain:g}; neutral resets each Play.", flush=True)
    if args.selftest:
        args.allow_loopback = True

    client = DeviceClient()
    try:
        state = client.send({"cmd": "status"}, timeout=1.0)
    except TimeoutError:
        state = None
        print("Unreal is not responding yet; waiting for Hallyway1 Play on localhost UDP 19872.", flush=True)
    if state and (state.get("mode") != "hallway" or "angles" not in state):
        raise SystemExit("The responding Unreal game is not Hallyway1; start that level in Play mode.")

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.bind((args.bind, args.port))
    except OSError as exc:
        raise SystemExit(f"Cannot bind OSC UDP {args.bind}:{args.port}: {exc}. Check that no other OSC receiver is using port {args.port}.")
    sock.setblocking(False)
    print(f"Listening for legacy OSC on UDP {args.bind}:{args.port}; forwarding to Unreal UDP 127.0.0.1:19872", flush=True)
    addresses = local_ipv4_addresses()
    if addresses:
        print("This computer's IPv4 addresses: " + ", ".join(addresses), flush=True)
        print("laptopIP in Arduino/new_rotate/new_rotate.ino must be the Wi-Fi/hotspot address above.", flush=True)
    if args.allow_loopback:
        print("Local 127.0.0.1 OSC test packets are enabled (--allow-loopback).", flush=True)
    else:
        print("Local 127.0.0.1 OSC test packets are ignored; hardware must arrive from the ESP32 Wi-Fi IP.", flush=True)

    angles = None
    imu = None
    shake_pending = False
    rotate_events = deque(maxlen=32)
    last_angles_time = 0.0
    last_imu_time = 0.0
    last_angle_forwarded = 0.0
    last_view_forwarded = 0.0
    last_status = 0.0
    unreal_reported_offline = state is None
    last_debug = 0.0
    last_packet = "waiting for OSC /rotary"
    last_sender = "no hardware packet yet"
    last_packet_log = {"angles": 0.0, "imu": 0.0}
    last_angle_attempt = 0.0
    last_view_attempt = 0.0
    send_interval = 1.0 / 30.0  # Latest-value streaming without per-sample ACK waits.
    mapping = AngleMapping()
    view_mapping = ViewMapping(args.yaw_gain, args.pitch_gain, args.pitch_axis)
    shake_detector = ShakeDetector()

    started = time.monotonic()
    received_any = False
    last_rx_warning = 0.0
    selftest_state = None
    selftest_deadline = None
    if args.selftest:
        def selftest_sender():
            sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            begin = time.monotonic()
            while time.monotonic() - begin < 5.0:
                elapsed = time.monotonic() - begin
                rotary = [0.0, (elapsed * 90.0) % 360.0, 0.0, 0.0, 0.0]
                yaw = (elapsed * 20.0) % 360.0
                sender.sendto(_osc_packet("/rotary", rotary), ("127.0.0.1", args.port))
                sender.sendto(_osc_packet("/imu", [yaw, 0.0, 5.0, 0.0]), ("127.0.0.1", args.port))
                time.sleep(0.05)
            sender.close()

        threading.Thread(target=selftest_sender, daemon=True).start()
        print("SELFTEST: sending synthetic OSC for 5 s; checking face B and view yaw...", flush=True)

    try:
        while True:
            # Drain a burst and retain the newest sample from each sensor. The old
            # loop forwarded every 20 ms sample synchronously, which could block
            # OSC reception while waiting for a game-thread acknowledgement.
            for _ in range(128):
                try:
                    packet, sender = sock.recvfrom(2048)
                except BlockingIOError:
                    break
                now = time.monotonic()
                if sender[0] == "127.0.0.1" and not args.allow_loopback:
                    if not getattr(main, "loopback_notice", False):
                        print("Ignored local 127.0.0.1 OSC test traffic; hardware must arrive from the ESP32 Wi-Fi IP.", flush=True)
                        main.loopback_notice = True
                    continue
                try:
                    decoded = parse_osc_message(packet)
                    last_sender = f"{sender[0]}:{sender[1]}"
                    if not received_any:
                        received_any = True
                        print(f"OSC link online: first packet from {last_sender}", flush=True)
                    if decoded["cmd"] == "angles":
                        angles = decoded["degrees"]
                        last_angles_time = now
                        last_packet = "OSC /rotary " + ",".join(f"{v:.1f}" for v in angles)
                        if now - last_packet_log["angles"] >= 0.5:
                            print(f"{last_sender} {last_packet}", flush=True)
                            last_packet_log["angles"] = now
                    elif decoded["cmd"] == "imu":
                        imu = decoded
                        last_imu_time = now
                        last_packet = f"OSC /imu yaw={imu['yaw']:.1f} pitch={imu['pitch']:.1f} az={imu['acc_z']:.1f}"
                        shake_pending |= shake_detector.update(imu["acc_z"], now)
                        if now - last_packet_log["imu"] >= 0.5:
                            print(f"{last_sender} {last_packet}", flush=True)
                            last_packet_log["imu"] = now
                    elif decoded["cmd"] == "shake":
                        shake_pending = True
                        last_packet = "OSC /shake"
                        print(f"{last_sender} {last_packet}", flush=True)
                    elif decoded['cmd'] == 'rotate':
                        rotate_events.append((now, decoded))
                        last_packet=f"OSC /rotate {chr(65+decoded['face'])} {decoded['dir']}"
                        print(f'{last_sender} {last_packet}',flush=True)
                    elif decoded['cmd'] == 'view':
                        imu=decoded
                        last_imu_time=now
                        last_packet=f"OSC /view yaw={decoded['yaw']:.1f} pitch={decoded['pitch']:.1f}"
                        if now-last_packet_log['imu']>=.5:
                            print(f'{last_sender} {last_packet}',flush=True)
                            last_packet_log['imu']=now
                except ValueError as exc:
                    print(f"Ignored invalid OSC packet: {exc}", flush=True)

            now = time.monotonic()
            if not received_any and now - started > 5.0 and now - last_rx_warning > 10.0:
                last_rx_warning = now
                print("No OSC packets yet. Check that the ESP32 joined the same hotspot, "
                      "laptopIP in new_rotate.ino matches this computer, and inbound UDP "
                      f"{args.port} is allowed in Windows Firewall.", flush=True)
            new_state = client.poll_status()
            if new_state and new_state.get("mode") == "hallway" and "angles" in new_state:
                if state is None or state.get("run_id") != new_state.get("run_id"):
                    mapping.offset = None
                    view_mapping.reference = None
                    shake_pending = False
                    shake_detector = ShakeDetector()
                    rotate_events.clear()
                    last_angle_forwarded = last_angles_time
                    last_view_forwarded = last_imu_time
                    print("Unreal Play connected: recalibrating controller angles and camera for this run.", flush=True)
                state = new_state
                unreal_reported_offline = False
            if client.pending_status_seq is not None and now - client.pending_status_time > 1.0:
                client.pending_status_seq = None
                if not unreal_reported_offline:
                    print("Unreal Play stopped or unavailable; waiting for Play, then recalibrating automatically.", flush=True)
                unreal_reported_offline = True
                state = None
                shake_pending = False
                rotate_events.clear()
            status_interval = 0.5
            if client.pending_status_seq is None and now - last_status >= status_interval:
                try:
                    client.request_status()
                except OSError:
                    client.pending_status_seq = None
                    state = None
                last_status = now
            if state is not None and now - last_debug >= 0.25:
                try:
                    client.send_latest({"cmd": "debug", "text": f"{last_sender} {last_packet}", "downstream": "OSC -> local Unreal UDP 19872"})
                except (OSError, ValueError):
                    pass
                last_debug = now

            if args.selftest:
                if state is not None and selftest_state is None:
                    selftest_state = {
                        "angles": list(state.get("angles", [])),
                        "yaw": state.get("view_yaw", 0.0),
                        "pitch": state.get("view_pitch", 10.0),
                    }
                    selftest_deadline = now + 6.5
                if selftest_state is None and now - started > 8.0:
                    print("SELFTEST FAIL: Unreal did not answer on UDP 19872. "
                          "Enter Play in the Hallyway level first.", flush=True)
                    raise SystemExit(1)
                if selftest_state is not None and now >= selftest_deadline:
                    angles_changed = list(state.get("angles", [])) != selftest_state["angles"]
                    delta_yaw = (state.get("view_yaw", 0.0) - selftest_state["yaw"] + 180.0) % 360.0 - 180.0
                    view_moved = abs(delta_yaw) > 2.0
                    try:
                        client.send({"cmd": "angles", "degrees": selftest_state["angles"]}, timeout=0.5)
                        client.send({"cmd": "view", "yaw": selftest_state["yaw"], "pitch": selftest_state["pitch"]}, timeout=0.5)
                    except (TimeoutError, ValueError):
                        pass
                    passed = angles_changed and view_moved
                    print(f"SELFTEST angles_changed={angles_changed} view_moved={view_moved} "
                          f"{'PASS' if passed else 'FAIL'}", flush=True)
                    raise SystemExit(0 if passed else 1)

            if state is not None and angles is not None and now - last_angles_time < 0.5 and last_angles_time > last_angle_forwarded and now - last_angle_attempt >= send_interval:
                last_angle_attempt = now
                mapped = mapping.map(angles, state)
                if mapped is None:
                    # Discard in-flight rotation during an animation; resynchronize afterwards.
                    last_angle_forwarded = last_angles_time
                else:
                    try:
                        client.send_latest({"cmd": "angles", "degrees": mapped})
                        last_angle_forwarded = last_angles_time
                    except (OSError, ValueError) as exc:
                        print(f"Unreal missed /rotary sample (will continue from the same angle baseline): {exc}", flush=True)
                        last_angle_forwarded = last_angles_time

            if state is not None and imu is not None and now - last_imu_time < 0.5 and last_imu_time > last_view_forwarded and now - last_view_attempt >= send_interval:
                last_view_attempt = now
                view_command = ({'cmd':'view','yaw':imu['yaw'],'pitch':imu['pitch']}
                                if imu['cmd']=='view' else view_mapping.map(imu, state, now))
                if view_command:
                    try:
                        client.send_latest(view_command)
                    except (OSError, ValueError) as exc:
                        print(f"Unreal missed /imu view sample; retaining the current neutral reference: {exc}", flush=True)
                last_view_forwarded = last_imu_time

            while rotate_events and now-rotate_events[0][0]>2.0:
                rotate_events.popleft()
                print('Expired queued /rotate; input rate exceeded animation speed.',flush=True)
            if state is not None and rotate_events and not state.get('busy') and not state.get('transitioning'):
                _, event=rotate_events.popleft()
                try:
                    state=client.send(event,timeout=.3)
                except (TimeoutError,ValueError) as exc:
                    print(f'Unreal did not confirm /rotate (not replayed): {exc}',flush=True)

            if state is not None and shake_pending:
                shake_pending = False
                if not state.get("busy") and not state.get("transitioning"):
                    try:
                        state = client.send({"cmd": "assemble", "_gesture": True}, timeout=0.4)
                        print("OSC shake assembled aligned furniture", flush=True)
                    except ValueError:
                        print("OSC shake received; no furniture is aligned and steady yet.", flush=True)
                    except TimeoutError:
                        print("OSC shake received; Unreal did not confirm assembly.", flush=True)
            time.sleep(0.005)
    except KeyboardInterrupt:
        print("OSC bridge stopped", flush=True)
    finally:
        sock.close()


if __name__ == "__main__":
    main()
