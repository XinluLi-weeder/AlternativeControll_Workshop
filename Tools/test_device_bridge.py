"""Protocol regression tests; no hardware or Unreal required."""
import unittest
import json
from device_bridge import parse_line, DeviceClient, serial_lines

class BridgeTests(unittest.TestCase):
    def test_live_samples_are_nonblocking_and_keep_epoch(self):
        class FakeSocket:
            def __init__(self): self.sent=[]
            def sendto(self,data,address): self.sent.append(json.loads(data))
            def recvfrom(self,count): raise AssertionError('Live samples must not wait for an ACK')
        client=DeviceClient();client.sock.close();client.sock=FakeSocket();client.stage_epoch=4
        client.send_latest({'cmd':'angles','degrees':[0,90,180,270,0]})
        client.send_latest({'cmd':'view','yaw':25,'pitch':12})
        self.assertEqual([p['cmd'] for p in client.sock.sent],['angles','view'])
        self.assertEqual([p['seq'] for p in client.sock.sent],[1,2])
        self.assertTrue(all(p['no_ack'] and p['stage_epoch']==4 for p in client.sock.sent))
        with self.assertRaises(ValueError): client.send_latest({'cmd':'assemble'})

    def test_status_poll_does_not_wait_for_game_frame(self):
        class FakeSocket:
            def __init__(self): self.sent=[]; self.replies=[]
            def sendto(self,data,address): self.sent.append(json.loads(data))
            def setblocking(self,value): pass
            def settimeout(self,value): pass
            def recvfrom(self,count):
                if not self.replies: raise BlockingIOError()
                return self.replies.pop(0),('127.0.0.1',19872)
        client=DeviceClient();client.sock.close();client.sock=FakeSocket()
        client.request_status()
        self.assertIsNone(client.poll_status())
        self.assertEqual(client.sock.sent[0]['cmd'],'status')
        client.sock.replies.append(json.dumps({'seq':1,'stage_epoch':7,'status':'ok'}).encode())
        self.assertEqual(client.poll_status()['stage_epoch'],7)
        self.assertEqual(client.stage_epoch,7)

    def test_shake_is_not_queued_behind_animation(self):
        self.assertEqual(parse_line('SHAKE'), {'cmd':'assemble','_gesture':True})
        class FakeSocket:
            def __init__(self):self.sent=[]
            def sendto(self,data,address):self.sent.append(json.loads(data))
            def recvfrom(self,count):
                req=self.sent[-1]
                return json.dumps({'seq':req['seq'],'status':'busy'}).encode(),('127.0.0.1',19872)
        c=DeviceClient();c.sock.close();c.sock=FakeSocket();c.stage_epoch=0
        with self.assertRaisesRegex(ValueError,'Shake discarded'):c.send(parse_line('SHAKE'))
        self.assertEqual(len(c.sock.sent),1)
        self.assertNotIn('_gesture',c.sock.sent[0])

    def test_stage_epoch_and_stale_turn_not_replayed(self):
        class FakeSocket:
            def __init__(self):self.sent=[]
            def sendto(self, data, address):self.sent.append(json.loads(data))
            def recvfrom(self, count):
                req=self.sent[-1]
                return json.dumps({'seq':req['seq'],'status':'ok' if req['cmd']=='status' else 'stale_stage','stage_epoch':2 if req['cmd']=='status' else 3}).encode(),('127.0.0.1',19872)
        client=DeviceClient();client.sock.close();client.sock=FakeSocket()
        with self.assertRaisesRegex(ValueError,'Room changed'):
            client.send({'cmd':'rotate','face':1,'dir':1})
        self.assertEqual(len(client.sock.sent),2)
        self.assertEqual(client.sock.sent[1]['stage_epoch'],2)
        self.assertEqual(client.stage_epoch,3)

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
