import copy
import unittest
from unittest.mock import patch
from test_protocol import Network
from protocol_v04 import encode, valid
from endpoint import uid
from ntp_server import response


class Go2Tests(unittest.TestCase):
    def test_current_ids_and_only_center_polls(self):
        n=Network(); n.d.tick(); self.assertFalse(n.queue)
        n.c.tick(); m=__import__('protocol_v04').decode(n.queue[0][1])
        self.assertEqual((m['src_id'],m['dst_id']),('c2','dog_01'))
        self.assertEqual(m['body']['kind'],'RESYNC_REQUEST')
        self.assertIsNone(m['dst_session_id'])
        n.pump(1); self.assertTrue(n.c.up)

    def test_first_task_without_handshake_and_session_binding(self):
        n=Network(); tid=uid()
        b={'task_id':tid,'action':'GUIDE','params':{},'expire_at_unix_ms':int(n.wall*1000)+10000,
           'max_clock_skew_ms':100,'execution_timeout_ms':1000}
        m=n.c.message('TASK',b,tid); m['dst_session_id']=None
        self.assertTrue(valid(m)); n.d.receive(encode(m)); n.d.decide(tid,True)
        ack=n.d.acks[m['msg_id']]
        self.assertEqual(ack['dst_session_id'],n.c.session)
        self.assertTrue(ack['body']['accepted'])
        # Another center cannot silently replace an established session with a TASK.
        other=copy.deepcopy(m);other['session_id']=uid();other['msg_id']=uid()
        n.d.receive(encode(other)); self.assertNotIn(other['msg_id'],n.d.acks)

    def test_polling_does_not_extend_report_retry_window(self):
        n=Network();n.pump(1);n.drop=lambda m:m['type']=='RECEIPT'
        mid=n.d.event('A',.8,1);n.pump(61)
        self.assertTrue(n.d.pending[mid]['unconfirmed'])

    def test_ntp_preserves_origin_and_rejects_unsynced(self):
        request=bytearray(48);request[0]=0x23;request[40:48]=b'12345678'
        with patch('ntp_server.subprocess.check_output',return_value=b'Leap status : Not synchronised\n'):
            reply=response(request)
        self.assertEqual(reply[24:32],b'12345678')
        self.assertEqual(reply[0]>>6,3)
        self.assertIsNone(response(b'bad'))

if __name__=='__main__':unittest.main()
