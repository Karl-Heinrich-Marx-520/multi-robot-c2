import json
import unittest
from unittest.mock import patch
from endpoint import uid
from local_commands import handle
from test_protocol import Network


def request(endpoint, line):
    key=uid()
    raw=json.dumps({'request_id':key,'command':line}).encode()
    reply=json.loads(handle(endpoint,raw))
    assert reply['request_id']==key
    return reply


class LocalCommandsTests(unittest.TestCase):
    def setUp(self):
        self.output_patch=patch("c2_udp.output")
        self.output_patch.start();self.addCleanup(self.output_patch.stop)
        self.n=Network();self.n.pump(1)

    def test_all_events_and_legacy_trigger(self):
        n=self.n
        for code in ('A_BLOCKED','B_BLOCKED','C_BLOCKED','LOW_BATTERY','ARRIVED','FAULT'):
            reply=request(n.d,'event '+json.dumps({'event_code':code,'severity':1,'payload':{}}))
            self.assertTrue(reply['ok'],reply)
        self.assertTrue(handle(n.d,b'{"channel":"A","ratio":0.8,"severity":1}').startswith(b'QUEUED'))
        n.pump();self.assertFalse(n.d.pending)

    def test_accept_result_and_status(self):
        n=self.n;tid=n.task()
        self.assertTrue(request(n.d,'accept last')['ok'])
        self.assertTrue(request(n.d,'result last SUCCESS {"note":"test"}')['ok'])
        n.pump();self.assertEqual(n.c.tasks[tid]['state'],'TERMINAL')
        self.assertEqual(json.loads(request(n.d,'status')['result'])['tasks'][tid]['state'],'TERMINAL')
        self.assertFalse(request(n.d,'result last FAILED {}')['ok'])

    def test_role_and_command_validation(self):
        for line in ('quit','clock 0','task {}','event A 0.8 1\nevent B 0.5 1'):
            self.assertFalse(request(self.n.d,line)['ok'],line)
        for line in ('event A 0.8 1','accept last','result last SUCCESS {}'):
            self.assertFalse(request(self.n.c,line)['ok'],line)
        for raw in (b'[]',b'{"command":"status","command":"help"}'):
            self.assertIn(b'ERROR',handle(self.n.d,raw))
        self.assertFalse(json.loads(handle(self.n.d,b'{"command":3,"request_id":"x"}'))['ok'])

    def test_cancel_and_aborted_via_common_api(self):
        n=self.n;tid=n.task();request(n.d,'accept last');n.pump()
        r=request(n.c,'cancel last {"reason":"test","expire_in_ms":10000,"max_clock_skew_ms":100}')
        self.assertTrue(r['ok']);n.pump()
        self.assertTrue(request(n.d,'accept last')['ok'])
        self.assertTrue(request(n.d,'result last ABORTED {"reason":"simulation"}')['ok'])
        n.pump();self.assertEqual(n.c.tasks[tid]['result']['status'],'ABORTED')

if __name__=='__main__':unittest.main()
