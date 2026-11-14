"""Run only while Hallyway1 is in Play and Tools/osc_bridge.py is listening."""
import argparse
import socket
import struct
import time

from device_bridge import DeviceClient


def osc_string(value):
    raw = value.encode("ascii") + b"\0"
    return raw + b"\0" * ((-len(raw)) % 4)


def osc_floats(address, values):
    return osc_string(address) + osc_string("," + "f" * len(values)) + b"".join(struct.pack(">f", value) for value in values)


def stream(sock, port, address, values, duration=0.35):
    packet = osc_floats(address, values)
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        sock.sendto(packet, ("127.0.0.1", port))
        time.sleep(0.04)


def current(client):
    return client.send({"cmd": "status"}, timeout=8)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8000, help="OSC bridge port")
    args = parser.parse_args()
    client = DeviceClient()
    state = current(client)
    assert state.get("mode") == "hallway", "Start Hallyway1 in Play first."
    assert state.get("enabled_faces", [False, False])[1], "Face B must be enabled for the runtime check."
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        zeros = [0.0] * 5
        stream(sock, args.port, "/rotary", zeros)
        time.sleep(0.25)
        baseline = current(client)["angles"][1]
        moved = [0.0, 10.0, 0.0, 0.0, 0.0]
        stream(sock, args.port, "/rotary", moved)
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            after = current(client)
            if abs(after["angles"][1] - baseline) > 5:
                break
            time.sleep(0.05)
        assert abs(after["angles"][1] - baseline) > 5, f"/rotary did not move face B: {baseline} -> {after['angles'][1]}"
        stream(sock, args.port, "/rotary", zeros)
        time.sleep(0.25)

        stream(sock, args.port, "/imu", [0.0, 0.0, 0.0, 0.0])
        start_yaw = current(client)["view_yaw"]
        stream(sock, args.port, "/imu", [12.0, 0.0, 0.0, 0.0])
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            after_imu = current(client)
            delta = (after_imu["view_yaw"] - start_yaw + 180) % 360 - 180
            if abs(delta) > 2:
                break
            time.sleep(0.05)
        assert abs(delta) > 2, f"/imu did not move the view: {start_yaw} -> {after_imu['view_yaw']}"
        five_faces = [90.0, 180.0, 270.0, 45.0, 135.0]
        stream(sock, args.port, "/rotary", five_faces, duration=0.6)
        after_five = current(client)
        assert all(abs(actual - target) < 1 for actual, target in zip(after_five["angles"], five_faces)), after_five["angles"]
        stream(sock, args.port, "/rotary", zeros)
        stream(sock, args.port, "/imu", [0.0, 0.0, 0.0, 0.0])
        time.sleep(0.25)
        hud = current(client)
        assert " OSC /" in hud["controller_serial_line"], hud.get("controller_serial_line")
        print("OSC_RUNTIME_PASS", {"face_b_before": baseline, "face_b_moved": after["angles"][1],
                                  "five_faces": after_five["angles"],
                                  "view_yaw_before": start_yaw, "view_yaw_moved": after_imu["view_yaw"],
                                  "hud": hud["controller_serial_line"]}, flush=True)
    finally:
        sock.close()


if __name__ == "__main__":
    main()
