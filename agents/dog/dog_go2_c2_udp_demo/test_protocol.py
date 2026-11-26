import copy
import unittest
from collections import deque
from endpoint import Endpoint, uid
from protocol_v04 import encode, decode, valid

class Network:
    def __init__(self):
        self.now=0.; self.wall=1800000000.; self.queue=deque(); self.logs=[]; self.drop=None
        self.c=self.node(True); self.d=self.node(False)
    def node(self,center):
        def emit(raw):
            m=decode(raw)
            if self.drop and self.drop(m): return
            self.queue.append((center,raw))
        return Endpoint(center,emit,lambda k,v='':self.logs.append((center,k,copy.deepcopy(v))),mono=lambda:self.now,wall=lambda:self.wall+self.now,clock_bound=10)
    def pump(self,seconds=.3):
        for _ in range(round(seconds/.05)):
            self.now+=.05; self.c.tick(); self.d.tick()
            limit=0
            while self.queue:
                center,raw=self.queue.popleft(); (self.d if center else self.c).receive(raw)
                limit+=1
                if limit>100: raise AssertionError('packet loop')
    def task(self,timeout=10000,expiry=10000):
        b={'action':'OBSERVE','params':{'area':'A'},'expire_at_unix_ms':int((self.wall+self.now)*1000)+expiry,'max_clock_skew_ms':100,'execution_timeout_ms':timeout}
        tid=self.c.task(b); self.pump(); return tid

class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.n=Network(); self.n.pump(1); self.assertTrue(self.n.c.up)
    def test_manual_accept_result_loss_and_dedup(self):
        n=self.n; tid=n.task(); self.assertEqual(n.d.tasks[tid]['state'],'PENDING')
        n.drop=lambda m:m['type']=='ACK'; n.d.decide(tid,True); deadline=n.d.tasks[tid]['deadline']; n.pump(.5)
        self.assertEqual(n.d.tasks[tid]['deadline'],deadline)
        n.drop=None; n.pump(.3); self.assertEqual(n.c.tasks[tid]['state'],'RUNNING')
        n.drop=lambda m:m['type']=='RECEIPT'; n.d.finish(tid,'SUCCESS',{'text':'人工完成'}); n.pump(.6)
        self.assertEqual(n.c.tasks[tid]['state'],'TERMINAL'); self.assertEqual(len(n.d.pending),1)
        n.drop=None; n.pump(.3); self.assertFalse(n.d.pending)
        with self.assertRaises(ValueError): n.d.finish(tid,'FAILED',{})
    def test_rejection_immutable_fresh_msgid(self):
        n=self.n; tid=n.task(); n.d.decide(tid,False,'CAPABILITY'); n.pump()
        original=next(p['message'] for p in n.c.requests.values()); m=copy.deepcopy(original); m['msg_id']=uid()
        n.d.receive(encode(m)); self.assertEqual(n.d.acks[m['msg_id']]['body']['reject_reason'],'CAPABILITY')
        m['msg_id']=uid(); m['body']['params']={'area':'B'}; n.d.receive(encode(m))
        self.assertEqual(n.d.acks[m['msg_id']]['body']['reject_reason'],'ID_CONFLICT')
    def test_cancel_manual_and_terminal(self):
        n=self.n; tid=n.task(); n.d.decide(tid,True); n.pump()
        mid=n.c.cancel(tid,'operator',int((n.wall+n.now)*1000)+10000,100); n.pump()
        n.d.decide(mid,True); n.pump(); self.assertEqual(n.c.tasks[tid]['state'],'RUNNING')
        n.d.finish(tid,'ABORTED',{'reason':'manual stop confirmed'}); n.pump()
        self.assertEqual(n.c.tasks[tid]['result']['status'],'ABORTED')
        mid=n.c.cancel(tid,'again',int((n.wall+n.now)*1000)+10000,100); n.pump()
        self.assertEqual(n.d.acks[mid]['body']['reject_reason'],'ALREADY_FINISHED')
    def test_cancel_before_task(self):
        n=self.n; tid=uid(); trace=tid
        b={'task_id':tid,'reason':'early','expire_at_unix_ms':int((n.wall+n.now)*1000)+10000,'max_clock_skew_ms':100}
        m=n.c.message('CANCEL',b,trace); n.d.receive(encode(m)); self.assertEqual(n.d.acks[m['msg_id']]['body']['reject_reason'],'UNKNOWN_TASK')
        b.update(action='OBSERVE',params={},execution_timeout_ms=10000); del b['reason']
        m=n.c.message('TASK',b,trace); n.d.receive(encode(m)); self.assertEqual(n.d.acks[m['msg_id']]['body']['reject_reason'],'CANCELLED')
    def test_expiry_and_clock_guard(self):
        n=self.n; n.d.clock_bound=None; tid=n.task(); self.assertEqual(n.d.tasks[tid]['ack']['reject_reason'],'CLOCK_UNSYNCED')
        n.d.clock_bound=10; tid=n.task(expiry=500); n.pump(.5)
        self.assertEqual(n.d.tasks[tid]['ack']['reject_reason'],'EXPIRED')
    def test_execution_timeout(self):
        n=self.n; tid=n.task(timeout=300); n.d.decide(tid,True); n.pump(.6)
        self.assertEqual(n.c.tasks[tid]['result']['detail']['reason'],'EXECUTION_TIMEOUT')
    def test_result_before_ack(self):
        n=self.n; tid=n.task(); n.drop=lambda m:m['type']=='ACK'; n.d.decide(tid,True)
        n.d.finish(tid,'SUCCESS',{}); n.pump(); self.assertEqual(n.c.tasks[tid]['state'],'TERMINAL')
        n.drop=None
        ack=next(iter(n.d.acks.values())); n.c.receive(encode(ack)); self.assertEqual(n.c.tasks[tid]['state'],'TERMINAL')
    def test_reconnect_running_no_reexecute(self):
        n=self.n; tid=n.task(timeout=20000); n.d.decide(tid,True); n.pump(); deadline=n.d.tasks[tid]['deadline']
        n.drop=lambda m:True; n.pump(4); self.assertFalse(n.c.up)
        n.drop=None; n.pump(2); self.assertTrue(n.c.up); self.assertEqual(n.d.tasks[tid]['deadline'],deadline)
        self.assertEqual(n.c.tasks[tid]['state'],'RUNNING')
    def test_peer_restart_unknown_and_old_session(self):
        n=self.n; tid=n.task(); n.d.decide(tid,True); n.pump(); old=n.d; n.d=n.node(False); n.pump(2)
        self.assertTrue(n.c.up); self.assertEqual(n.c.tasks[tid]['state'],'RUNNING')
        self.assertTrue(any(k=='TASK_UNKNOWN_AFTER_RESTART' for _,k,_ in n.logs))
        self.assertIn(old.session,n.c.retired)
    def test_report_timeout_and_resume(self):
        n=self.n; n.drop=lambda m:m['type']=='RECEIPT'; mid=n.d.event('A',.8,1); n.pump(61)
        self.assertTrue(n.d.pending[mid]['unconfirmed']); n.drop=lambda m:True; n.pump(4)
        n.drop=None; n.pump(2); self.assertNotIn(mid,n.d.pending)
    def test_cancel_pending_task_prevents_execution(self):
        n=self.n; tid=n.task()
        mid=n.c.cancel(tid,'operator',int((n.wall+n.now)*1000)+10000,100); n.pump()
        n.d.decide(mid,True); n.pump()
        self.assertEqual(n.d.tasks[tid]['state'],'REJECTED')
        self.assertEqual(n.c.tasks[tid]['ack']['reject_reason'],'CANCELLED')
        with self.assertRaises(ValueError): n.d.decide(tid,True)
    def test_clock_jump_invalidates_bound(self):
        n=self.n; n.wall+=2; n.pump(.1)
        self.assertIsNone(n.d.clock_bound)
        tid=n.task(); self.assertEqual(n.d.tasks[tid]['ack']['reject_reason'],'CLOCK_UNSYNCED')
    def test_unsent_and_delivery_unknown_facts(self):
        n=self.n
        b={'action':'OBSERVE','params':{},'expire_at_unix_ms':int((n.wall+n.now)*1000)+500,'max_clock_skew_ms':100,'execution_timeout_ms':10000}
        tid=n.c.task(b); n.c.down(); n.drop=lambda m:True; n.pump(1)
        self.assertTrue(any(k=='EXPIRED_UNSENT' and v==tid for _,k,v in n.logs))
        n.drop=None; n.pump(2)
        n.drop=lambda m:m['type']=='TASK'; tid=n.task(expiry=500); n.pump(1)
        self.assertTrue(any(k=='DELIVERY_UNKNOWN' and v==tid for _,k,v in n.logs))
    def test_terminal_recovery_and_same_result_identity(self):
        n=self.n; tid=n.task(); n.d.decide(tid,True); n.pump()
        n.drop=lambda m:m['type']=='RESULT'; result=n.d.finish(tid,'SUCCESS',{'manual':True}); n.pump()
        n.drop=lambda m:True; n.pump(4); n.drop=None; n.pump(2)
        self.assertTrue(n.c.up); self.assertEqual(n.c.tasks[tid]['state'],'TERMINAL')
        self.assertNotIn(result['msg_id'],n.d.pending)
    def test_conflicting_result_does_not_change_terminal(self):
        n=self.n; tid=n.task(); n.d.decide(tid,True); n.d.finish(tid,'SUCCESS',{}); n.pump()
        m=n.d.message('RESULT',{'task_id':tid,'status':'FAILED','detail':{}},n.d.tasks[tid]['trace'])
        n.c.receive(encode(m)); self.assertEqual(n.c.tasks[tid]['result']['status'],'SUCCESS')
        self.assertTrue(any(k=='PROTOCOL_ERROR' for _,k,_ in n.logs))
    def test_all_body_fields_required(self):
        n=self.n; tid=uid(); ack={'ack_for_msg_id':uid(),'task_id':tid,'accepted':True,'reject_reason':None}
        result={'task_id':tid,'status':'SUCCESS','detail':{}}
        cases=[('EVENT',{'event_code':'FAULT','severity':2,'payload':{}}),
               ('TASK',{'task_id':tid,'action':'GUIDE','params':{},'expire_at_unix_ms':1,'max_clock_skew_ms':0,'execution_timeout_ms':1}),
               ('CANCEL',{'task_id':tid,'reason':'manual','expire_at_unix_ms':1,'max_clock_skew_ms':0}),
               ('ACK',ack),('RESULT',result),('RECEIPT',{'ack_for_msg_id':uid()}),
               ('LINK',{'kind':'HEARTBEAT','hb_seq':1}),
               ('LINK',{'kind':'RESYNC_REQUEST','request_id':uid(),'task_id':tid}),
               ('LINK',{'kind':'RESYNC_STATE','request_id':uid(),'task_id':tid,'task_state':'TERMINAL','task_ack':ack,'result':result})]
        for typ,body in cases:
            m=n.c.message(typ,body,tid); self.assertTrue(valid(m),(typ,body))
            self.assertEqual(decode(encode(m)),m)
            for key in body:
                bad=copy.deepcopy(m); del bad['body'][key]
                self.assertFalse(valid(bad),(typ,key))
    def test_bad_json_and_schema(self):
        for raw in (b'{"a":1,"a":2}',b'{"a":NaN}',b'x'*1201):
            with self.assertRaises(ValueError): decode(raw)
        m=self.n.d.message('EVENT',{'event_code':'A_BLOCKED','severity':1,'payload':{}},uid())
        self.assertTrue(valid(m)); m['dst_session_id']=None; self.assertFalse(valid(m))
        m['dst_session_id']=self.n.c.session; m['body']['severity']=True; self.assertFalse(valid(m))
    def test_result_snapshot_size(self):
        n=self.n; tid=n.task(); n.d.decide(tid,True)
        with self.assertRaises(ValueError): n.d.finish(tid,'SUCCESS',{'text':'中'*400})
        self.assertEqual(n.d.tasks[tid]['state'],'RUNNING')

if __name__=='__main__': unittest.main()
