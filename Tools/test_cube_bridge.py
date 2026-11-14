import unittest
from cube_bridge import LatestInput, AngleMapping, ViewMapping
import math
from device_bridge import parse_line


class CubeTests(unittest.TestCase):
    def test_simultaneous_faces_remain_independent(self):
        mapping=AngleMapping()
        state={'stage_epoch':0,'angles':[0]*5}
        mapping.map([0]*5,state)
        self.assertEqual(mapping.map([45,90,135,180,225],state),[45,90,135,180,225])

    def test_quaternion_view_and_center(self):
        mapping=ViewMapping()
        state={'view_yaw':0.,'view_pitch':10.}
        mapping.map([1,0,0,0],state,0)
        q=[math.cos(math.pi/4),0,0,math.sin(math.pi/4)]
        result=mapping.map(q,state,1)
        self.assertAlmostEqual(result['yaw'],90.,places=2)
        self.assertAlmostEqual(result['pitch'],10.)
        result=mapping.map(q,state,2,center=True)
        self.assertEqual(result,{'cmd':'view','yaw':0.,'pitch':10.})
        # Changing the quaternion sign represents the same orientation.
        result=mapping.map([-v for v in q],state,3)
        self.assertAlmostEqual(result['yaw'],0.)

    def test_orientation_validation_and_separate_stream(self):
        latest=LatestInput()
        latest.accept('ORIENT,1,0,0,0',1)
        latest.accept('CENTER',2)
        self.assertEqual(latest.take_view(),([1.,0.,0.,0.],1,True))
        self.assertFalse(latest.take_view()[2])
        self.assertIsNone(latest.take()[0])
        for line in ['ORIENT,0,0,0,0','ORIENT,nan,0,0,0']:
            with self.assertRaises(ValueError):parse_line(line)

    def test_latest_frame_replaces_backlog_and_shake_consumed(self):
        data = LatestInput()
        for n in range(100):
            data.accept(f'ANGLES,{n},0,0,0,0', n)
        data.accept('SHAKE', 99.1)
        self.assertEqual(data.take(), ([99., 0., 0., 0., 0.], 99, 99.1))
        self.assertEqual(data.take()[2], 0)

    def test_rebase_after_transition_and_animation(self):
        mapping = AngleMapping()
        state = {'stage_epoch': 0, 'angles': [0]*5, 'busy': False}
        self.assertEqual(mapping.map([30]*5, state), [0]*5)
        self.assertEqual(mapping.map([75]*5, state), [45]*5)
        self.assertIsNone(mapping.map([100]*5, dict(state, busy=True)))
        self.assertEqual(mapping.map([130]*5, dict(state, angles=[45]*5)), [45]*5)
        self.assertEqual(mapping.map([160]*5, dict(state, stage_epoch=1)), [0]*5)

    def test_nonfinite_rejected(self):
        for value in ('nan', 'inf', '360001'):
            with self.assertRaises(ValueError):
                parse_line(f'ANGLES,{value},0,0,0,0')


if __name__ == '__main__':
    unittest.main()
