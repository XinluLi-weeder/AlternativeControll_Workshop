"""Protocol regression tests; no hardware or Unreal required."""
import unittest
from device_bridge import parse_line, DeviceClient, serial_lines

class BridgeTests(unittest.TestCase):
    def test_view_angles(self):
        self.assertEqual(parse_line('VIEW,-179.5,25'), {'cmd':'view','yaw':-179.5,'pitch':25.0})
        for line in ['VIEW,nan,0','VIEW,0,inf','VIEW,0','VIEW,no,0']:
            with self.assertRaises(ValueError): parse_line(line)

    def test_five_faces_and_invalid_inputs(self):
        for i, face in enumerate('ABCDE'):
            self.assertEqual(parse_line(f'ROTATE,{face},-1'),{'cmd':'rotate','face':i,'dir':-1})
        for bad in ['ROTATE,F,1','ROTATE,A,0','ROTATE,AB,1','ROTATE,A,1,extra']:
            with self.assertRaises(ValueError): parse_line(bad)

    def test_feedback_deduplicates_replies_and_reconnects(self):
        client=DeviceClient(); received=[]; client.on_feedback=received.append
        for counter in [5,5,6,6,6,7,0,0,1]: client.feedback({'completion_serial':counter})
        self.assertEqual(received,['HAPTIC,180,140\n']*3)
        client.sock.close()

    def test_serial_fragments_and_idle_ticks(self):
        class Port:
            in_waiting=0
            chunks=iter([b'ROT',b'',b'ATE,E,1\nASSE',b'MBLE\n'])
            def read(self,n): return next(self.chunks,b'')
        stream=serial_lines(Port())
        lines=[next(stream) for _ in range(6)]
        self.assertEqual([line for line in lines if line],['ROTATE,E,1','ASSEMBLE'])

if __name__=='__main__': unittest.main()
