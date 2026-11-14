import math
import struct
import unittest

from osc_bridge import AngleMapping, ShakeDetector, ViewMapping, parse_osc_message


def osc_string(value):
    raw = value.encode("ascii") + b"\0"
    return raw + b"\0" * ((-len(raw)) % 4)


def osc_floats(address, values):
    return osc_string(address) + osc_string("," + "f" * len(values)) + b"".join(struct.pack(">f", v) for v in values)


class OscBridgeTests(unittest.TestCase):
    def test_current_firmware_rotate_and_view(self):
        packet=osc_string('/rotate')+osc_string(',si')+osc_string('D')+struct.pack('>i',-1)
        self.assertEqual(parse_osc_message(packet),{'cmd':'rotate','face':3,'dir':-1})
        self.assertEqual(parse_osc_message(osc_floats('/view',[32,12,0])),{'cmd':'view','yaw':32.,'pitch':12.})
        with self.assertRaises(ValueError):
            parse_osc_message(osc_string('/rotate')+osc_string(',si')+osc_string('F')+struct.pack('>i',1))
        with self.assertRaises(ValueError):
            parse_osc_message(osc_string('/rotary')+osc_string(',sssss')+osc_string('1')*5)

    def test_legacy_rotary_packet(self):
        parsed = parse_osc_message(osc_floats("/rotary", [0, 90, 180, 270, 359.5]))
        self.assertEqual(parsed["cmd"], "angles")
        self.assertEqual(parsed["degrees"], [0, 90, 180, 270, 359.5])

    def test_legacy_imu_packet_uses_euler_pitch_axis(self):
        parsed = parse_osc_message(osc_floats("/imu", [12, 3, 8, -0.2]))
        self.assertEqual(parsed["cmd"], "imu")
        self.assertAlmostEqual(parsed["yaw"], 12.0)
        self.assertAlmostEqual(parsed["roll"], 3.0)
        self.assertAlmostEqual(parsed["pitch"], 8.0)
        self.assertAlmostEqual(parsed["acc_z"], -0.2)

    def test_rejects_bad_shape_and_nonfinite_values(self):
        with self.assertRaises(ValueError):
            parse_osc_message(osc_floats("/rotary", [1, 2, 3]))
        with self.assertRaises(ValueError):
            parse_osc_message(osc_floats("/imu", [math.nan, 0, 0, 0]))

    def test_angle_baseline_preserves_current_game_angles(self):
        mapper = AngleMapping()
        state = {"angles": [45, 90, 180, 270, 0], "stage_epoch": 2, "busy": False, "transitioning": False}
        self.assertEqual(mapper.map([10, 20, 30, 40, 50], state), [45, 90, 180, 270, 0])
        mapped = mapper.map([20, 20, 30, 40, 50], state)
        self.assertEqual(mapped, [55, 90, 180, 270, 0])

    def test_view_baseline_and_yaw_wrap(self):
        mapper = ViewMapping()
        state = {"view_yaw": 0, "view_pitch": 10, "transitioning": False}
        self.assertIsNotNone(mapper.map({"yaw": 359, "pitch": 4}, state, 1.0))
        mapped = mapper.map({"yaw": 1, "pitch": 9}, state, 1.1)
        self.assertGreater(mapped["yaw"], 0)
        self.assertGreater(mapped["pitch"], 10)

    def test_new_play_same_epoch_reanchors_rotary_and_view(self):
        mapper = AngleMapping()
        first = {"run_id":"one", "angles":[0]*5, "stage_epoch":0}
        mapper.map([0]*5, first)
        self.assertEqual(mapper.map([90]*5, first), [90]*5)
        second = dict(first, run_id="two")
        self.assertEqual(mapper.map([90]*5, second), [0]*5)
        self.assertEqual(mapper.map([180]*5, second), [90]*5)
        view = ViewMapping()
        view.map({"yaw":0,"pitch":0}, first, 1)
        moved = view.map({"yaw":20,"pitch":10}, first, 2)
        self.assertAlmostEqual(moved['yaw'],50,places=4)
        self.assertAlmostEqual(moved['pitch'],30,places=4)
        neutral = view.map({"yaw":20,"pitch":10}, dict(second,view_yaw=0,view_pitch=-18), 3)
        self.assertEqual(neutral,{'cmd':'view','yaw':0.,'pitch':-18.})

    def test_amplified_yaw_is_continuous_across_half_turn(self):
        view = ViewMapping()
        state = {"view_yaw":0,"view_pitch":0}
        view.map({"yaw":0,"pitch":0}, state, 0)
        for i, yaw in enumerate([90,170,179],1):
            before = view.map({"yaw":yaw,"pitch":0},state,i)
        after = view.map({"yaw":181,"pitch":0},state,4)
        delta = (after['yaw']-before['yaw']+180)%360-180
        self.assertAlmostEqual(delta,5,places=4)

    def test_shake_detector_requires_two_peaks_and_cooldown(self):
        detector = ShakeDetector()
        self.assertFalse(detector.update(7, 1.0))
        self.assertFalse(detector.update(0, 1.1))
        self.assertTrue(detector.update(-7, 1.2))
        self.assertFalse(detector.update(7, 1.5))


if __name__ == "__main__":
    unittest.main()
