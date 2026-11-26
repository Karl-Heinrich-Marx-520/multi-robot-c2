"""Stateful v0.4 transport and manual task endpoint. No robot control."""
import copy
import json
import time
import uuid
from protocol_v04 import encode, decode, valid, REASONS, STATUSES, is_id

def uid(): return uuid.uuid4().hex

def canonical(value): return json.dumps(value,sort_keys=True,ensure_ascii=False,allow_nan=False,separators=(',',':'))

class Endpoint:
    def __init__(self, center, emit, log, mono=time.monotonic, wall=time.time, clock_bound=None):
        self.center=center; self.emit=emit; self.log=log; self.mono=mono; self.wall=wall
        self.src,self.dst=('c2','dog_01') if center else ('dog_01','c2')
        self.session=uid(); self.peer=None; self.previous=None; self.candidate=None; self.retired=set()
        self.up=False; self.verified=False; self.last_rx=mono(); self.last_hb=-1e9; self.seq=0
        self.queries={}; self.recovery=set(); self.pending={}; self.requests={}; self.tasks={}
        self.incoming={}; self.acks={}; self.seen={}; self.clock_bound=clock_bound
        self.last_mono=mono(); self.last_wall=wall(); self.last_task=None

    def message(self,t,b,trace=None,target=None):
        return {'version':1,'type':t,'msg_id':uid(),'session_id':self.session,
                'dst_session_id':target or self.peer,'src_id':self.src,'dst_id':self.dst,
                'trace_id':trace,'ts_mono_ms':int(self.mono()*1000),'body':copy.deepcopy(b)}

    def send(self,m):
        m=copy.deepcopy(m); m['ts_mono_ms']=int(self.mono()*1000)
        self.emit(encode(m)); return m

    def report(self,t,b,trace):
        m=self.message(t,b,trace,target=self.peer or '0'*32)
        # Validate before retaining; reserve enough bytes for RESYNC_STATE.
        encode(m)
        if t=='RESULT':
            task=self.tasks[b['task_id']]
            snapshot=self.snapshot(uid(),b['task_id'],dict(task,state='TERMINAL',result=b))
            encode(self.message('LINK',snapshot,trace,target=self.peer or '0'*32))
        self.pending[m['msg_id']]={'message':m,'last':-1e9,'online':0.,'unconfirmed':False}
        return m

    def reset_reports(self):
        for p in self.pending.values():
            p['online']=0.; p['unconfirmed']=False; p['last']=-1e9

    def query(self,tid='',trace=None):
        b={'kind':'RESYNC_REQUEST','request_id':uid(),'task_id':tid}
        m=self.message('LINK',b,trace)
        if not tid: m['dst_session_id']=None
        self.queries[b['request_id']]={'message':m,'last':-1e9,'started':self.mono(),'warned':False}
        return b['request_id']

    def down(self):
        if self.up or self.verified: self.log('LINK_DOWN',self.peer)
        self.up=False; self.verified=False; self.queries.clear(); self.recovery.clear()
        for p in self.requests.values():
            if not p['done']: p['enabled']=False

    def start_recovery(self,sid):
        self.down(); self.peer=sid; self.candidate=sid
        self.query('')

    def recovered(self):
        if self.verified and not self.recovery and not self.up:
            self.up=True; self.log('LINK_UP',self.peer); self.reset_reports()

    def tick(self):
        now=self.mono(); wall=self.wall(); dt=now-self.last_mono
        if abs((wall-self.last_wall)-dt)>0.5:
            self.clock_bound=None; self.log('CLOCK_UNSYNCED','wall clock jumped; reconfirm clock bound')
        self.last_mono=now; self.last_wall=wall
        if (self.up or self.verified) and now-self.last_rx>=3: self.down()
        if self.center and now-self.last_hb>=1:
            self.last_hb=now
            if not any(q['message']['body']['task_id']=='' for q in self.queries.values()):
                self.query('')
        for q in list(self.queries.values()):
            if now-q['last']>=0.2: self.send(q['message']); q['last']=now
            if now-q['started']>=3 and not q['warned']:
                q['warned']=True; self.log('RECOVERY_TIMEOUT',q['message']['body']['task_id'])
        for mid,p in list(self.pending.items()):
            if self.up and not p['unconfirmed']:
                if p['last']>-1e8: p['online']+=max(0,dt)
                if p['online']>=60:
                    p['unconfirmed']=True; self.log('REPORT_UNCONFIRMED',mid)
                elif now-p['last']>=0.2:
                    p['message']['dst_session_id']=self.peer; self.send(p['message']); p['last']=now
        for p in self.requests.values():
            if p['done']: continue
            m=p['message']; b=m['body']
            expired=wall*1000>=b['expire_at_unix_ms'] or now>=p['deadline']
            if expired:
                p['done']=True
                fact='DELIVERY_UNKNOWN' if p['attempted'] else 'EXPIRED_UNSENT'
                self.log(fact,b['task_id'])
                if p['attempted'] and self.up: self.query(b['task_id'],m['trace_id'])
            elif self.up and p['enabled'] and now-p['last']>=0.2:
                self.send(m); p['attempted']=True; p['last']=now
        if not self.center:
            for mid,m in list(self.incoming.items()):
                if mid not in self.acks and m['type']=='TASK' and self.expiry_reason(m['body'])=='EXPIRED':
                    self.decide(mid,False,'EXPIRED')
            for tid,t in self.tasks.items():
                if t.get('state')=='RUNNING' and now>=t['deadline']:
                    self.finish(tid,'FAILED',{'reason':'EXECUTION_TIMEOUT','simulation':True})
        else:
            for tid,t in self.tasks.items():
                if t.get('state')=='RUNNING' and now>=t.get('wait_until',float('inf')) and not t.get('wait_warned'):
                    t['wait_warned']=True; self.log('RESULT_UNKNOWN',tid)
                    if self.up: self.query(tid,t['trace'])

    def expiry_reason(self,b):
        if self.wall()*1000+b['max_clock_skew_ms']>=b['expire_at_unix_ms']: return 'EXPIRED'
        if self.clock_bound is None or self.clock_bound>b['max_clock_skew_ms']: return 'CLOCK_UNSYNCED'
        return None

    def replay_result(self,t):
        original=t.get('result_message')
        if not original: return
        rm=copy.deepcopy(original); rm['dst_session_id']=self.peer
        p=self.pending.setdefault(rm['msg_id'],{'message':rm,'last':-1e9,'online':0.,'unconfirmed':False})
        p['message']['dst_session_id']=self.peer
        self.send(p['message']); p['last']=self.mono()

    def receipt(self,m):
        self.send(self.message('RECEIPT',{'ack_for_msg_id':m['msg_id']},m['trace_id']))

    def snapshot(self,rid,tid,t=None):
        return {'kind':'RESYNC_STATE','request_id':rid,'task_id':tid,
                'task_state':t['state'] if t else 'UNKNOWN',
                'task_ack':copy.deepcopy(t.get('ack')) if t else None,
                'result':copy.deepcopy(t.get('result')) if t else None}

    def receive(self,raw):
        try:
            m=decode(raw)
            if not valid(m,self.dst,self.src): raise ValueError('invalid message schema')
        except (ValueError,UnicodeDecodeError,RecursionError) as exc:
            self.log('DROP',str(exc)); return
        sid=m['session_id']; b=m['body']; typ=m['type']; now=self.mono()
        if sid in self.retired: self.log('DROP','retired session'); return
        if typ=='LINK' and b['kind']=='HEARTBEAT':
            if m['dst_session_id'] not in (None,self.session): return
            if sid==self.peer and (self.up or self.verified): self.last_rx=now
            elif self.center and not any(q['message']['body']['task_id']=='' for q in self.queries.values()):
                self.start_recovery(sid)
            return
        bootstrap_task=not self.center and typ=='TASK' and m['dst_session_id'] is None
        discovery=not self.center and typ=='LINK' and b['kind']=='RESYNC_REQUEST' and b['task_id']=='' and m['dst_session_id'] is None
        if bootstrap_task:
            if self.peer is not None and sid!=self.peer:
                self.log('DROP','new center must establish session before TASK'); return
            self.peer=sid; self.previous=sid; self.verified=True; self.up=True
        if m['dst_session_id']!=self.session and not (bootstrap_task or discovery):
            self.log('DROP','wrong destination session'); return
        if typ=='LINK' and b['kind']=='RESYNC_REQUEST' and not self.center:
            if b['task_id']!='' and (sid!=self.peer or not self.verified): return
            if b['task_id']=='':
                if self.previous and self.previous!=sid: self.retired.add(self.previous)
                reset=not self.up or self.peer!=sid
                self.peer=sid; self.previous=sid; self.verified=True; self.up=True
                if reset: self.reset_reports()
            self.last_rx=now
            t=self.tasks.get(b['task_id'])
            if t and t.get('state')=='PENDING':
                # No v0.4 snapshot state represents an undecided task. Wait for operator.
                self.log('RESYNC_WAIT_DECISION',b['task_id']); return
            self.send(self.message('LINK',self.snapshot(b['request_id'],b['task_id'],t),t['trace'] if t else m['trace_id']))
            if t and t.get('result_message'):
                rm=t['result_message']; self.pending.setdefault(rm['msg_id'],{'message':rm,'last':-1e9,'online':0.,'unconfirmed':False})
            return
        if typ=='LINK' and b['kind']=='RESYNC_STATE' and self.center:
            q=self.queries.get(b['request_id'])
            if not q or b['task_id']!=q['message']['body']['task_id'] or m['trace_id']!=q['message']['trace_id']: return
            if b['task_id']!='' and sid!=self.peer: return
            tid=b['task_id']
            if tid=='':
                if b['task_state']!='UNKNOWN': return
                changed=self.previous is not None and self.previous!=sid
                if changed: self.retired.add(self.previous)
                self.peer=sid; self.previous=sid; self.verified=True; self.last_rx=now
                del self.queries[b['request_id']]
                for taskid,t in (list(self.tasks.items()) if changed or not self.up else []):
                    if t['state'] not in ('TERMINAL','REJECTED'):
                        self.recovery.add(taskid); t['peer_changed']=changed; self.query(taskid,t['trace'])
                self.recovered(); return
            t=self.tasks.get(tid)
            if not t: return
            if b['task_state']=='UNKNOWN' and t.get('peer_changed'):
                self.log('TASK_UNKNOWN_AFTER_RESTART',tid)
            elif b['task_state']=='UNKNOWN':
                self.log('TASK_RECORD_UNKNOWN',tid)
            elif not self.apply_state(t,b['task_state'],b['task_ack'],b['result']): return
            self.last_rx=now; del self.queries[b['request_id']]; self.recovery.discard(tid); self.recovered(); return
        if sid!=self.peer or not self.verified: return
        # A task may finish while center is reconciling, so valid reports remain receivable.
        self.last_rx=now
        key=(m['src_id'],m['msg_id']); signature=canonical((typ,m['trace_id'],b))
        old=self.seen.get(key)
        if old is not None and old!=signature:
            self.log('PROTOCOL_ERROR','msg_id content conflict'); return
        self.seen[key]=copy.deepcopy(signature)
        if typ in ('TASK','CANCEL') and not self.center:
            self.receive_request(m); return
        if typ=='EVENT' and self.center:
            if old is None: self.log('EVENT',m)
            else: self.log('DUPLICATE',m['msg_id'])
            self.receipt(m); return
        if typ=='RECEIPT':
            p=self.pending.get(b['ack_for_msg_id'])
            if p and p['message']['trace_id']==m['trace_id']:
                del self.pending[b['ack_for_msg_id']]; self.log('RECEIPT',b['ack_for_msg_id'])
            return
        if typ=='ACK' and self.center:
            p=self.requests.get(b['ack_for_msg_id'])
            if not p or p['message']['body']['task_id']!=b['task_id'] or p['message']['trace_id']!=m['trace_id']: return
            p['done']=True; t=self.tasks[b['task_id']]
            if p['message']['type']=='TASK': self.apply_state(t,'RUNNING' if b['accepted'] else 'REJECTED',b,None)
            else: t['cancel_accepted']=b['accepted']
            self.log('ACK',m); return
        if typ=='RESULT' and self.center:
            t=self.tasks.get(b['task_id'])
            if not t or m['trace_id']!=t['trace']: return
            if t.get('result') is not None:
                if canonical(t['result'])!=canonical(b): self.log('PROTOCOL_ERROR','contradictory terminal result')
                else: self.receipt(m)
                return
            if t['state']=='REJECTED': self.log('PROTOCOL_ERROR','result after rejection'); return
            t['state']='TERMINAL'; t['result']=copy.deepcopy(b)
            for p in self.requests.values():
                if p['message']['body']['task_id']==b['task_id'] and p['message']['type']=='TASK': p['done']=True
            if old is None: self.log('RESULT',m)
            self.receipt(m)

    def apply_state(self,t,state,ack,result):
        if t.get('result') is not None:
            if result is not None and canonical(t['result'])!=canonical(result): self.log('PROTOCOL_ERROR','conflicting recovery result'); return False
            return True
        if t['state']=='REJECTED' and state!='REJECTED': self.log('PROTOCOL_ERROR','rejected task state changed'); return False
        if t['state']=='RUNNING' and state=='REJECTED': self.log('PROTOCOL_ERROR','running task rejected'); return False
        t['state']=state
        if ack is not None: t['ack']=copy.deepcopy(ack)
        if state=='RUNNING': t.setdefault('wait_until',self.mono()+t['body']['execution_timeout_ms']/1000+1)
        if result is not None: t['result']=copy.deepcopy(result)
        for p in self.requests.values():
            if p['message']['body']['task_id']==t['body']['task_id'] and p['message']['type']=='TASK': p['done']=True
        self.log('TASK_STATE',{'task_id':t['body']['task_id'],'state':state}); return True

    def reply_ack(self,m,accept,reason=None):
        b={'ack_for_msg_id':m['msg_id'],'task_id':m['body']['task_id'],'accepted':accept,'reject_reason':None if accept else reason}
        ack=self.message('ACK',b,m['trace_id']); self.acks[m['msg_id']]=ack
        self.send(ack); self.log('ACK_SENT',ack); return b

    def receive_request(self,m):
        mid=m['msg_id']; tid=m['body']['task_id']; b=m['body']; t=self.tasks.get(tid)
        if mid in self.acks:
            self.send(self.acks[mid]);
            if t and t.get('result_message'):
                self.replay_result(t)
            return
        if t and t['trace']!=m['trace_id']: self.log('PROTOCOL_ERROR','task trace conflict'); return
        if m['type']=='TASK' and t and t.get('body'):
            original=t['body']
            if any(canonical(original[k])!=canonical(b[k]) for k in ('action','params','execution_timeout_ms')):
                self.reply_ack(m,False,'ID_CONFLICT'); return
            if t['state'] in ('RUNNING','TERMINAL','REJECTED'):
                ab=t.get('ack')
                if ab: self.reply_ack(m,ab['accepted'],ab['reject_reason'])
                else: self.reply_ack(m,False,'CANCELLED')
                if t.get('result_message'):
                    self.replay_result(t)
                return
        reason=self.expiry_reason(b)
        if reason:
            self.reply_ack(m,False,reason)
            if m['type']=='TASK' and t is None:
                self.tasks[tid]={'state':'REJECTED','body':b,'trace':m['trace_id'],'ack':self.acks[mid]['body']}
            return
        already_pending=mid in self.incoming
        self.incoming[mid]=m; self.last_task=tid
        if m['type']=='CANCEL':
            if not t:
                self.reply_ack(m,False,'UNKNOWN_TASK')
                self.tasks[tid]={'state':'REJECTED','body':None,'trace':m['trace_id'],'ack':None,'cancelled':True}; return
            if t['state']=='TERMINAL':
                self.reply_ack(m,False,'ALREADY_FINISHED'); self.replay_result(t); return
            if t['state']=='REJECTED': self.reply_ack(m,False,'ALREADY_FINISHED'); return
            if not already_pending: self.log('CANCEL_WAIT_OPERATOR',m)
            return
        if t and t.get('cancelled'):
            self.reply_ack(m,False,'CANCELLED'); t['body']=b; t['ack']=self.acks[mid]['body']; return
        if not t: self.tasks[tid]={'state':'PENDING','body':copy.deepcopy(b),'trace':m['trace_id'],'ack':None}
        if not already_pending: self.log('TASK_WAIT_OPERATOR',m)

    def decide(self,reference,accept,reason='CAPABILITY'):
        m=self.incoming.get(reference)
        if m is None:
            candidates=[v for k,v in self.incoming.items() if v['body']['task_id']==reference and k not in self.acks]
            if not candidates: raise ValueError('no undecided request; use status to find msg_id')
            m=candidates[-1]
        if m['msg_id'] in self.acks: raise ValueError('decision is immutable')
        b=m['body']; tid=b['task_id']; t=self.tasks[tid]
        if m['type']=='CANCEL' and t['state']=='TERMINAL':
            return self.reply_ack(m,False,'ALREADY_FINISHED')
        if m['type']=='TASK' and t['state']!='PENDING': raise ValueError('task already decided')
        if accept:
            why=self.expiry_reason(b)
            if why: accept=False; reason=why
        if not accept and reason not in REASONS: raise ValueError('invalid reject reason')
        ack=self.reply_ack(m,accept,reason)
        if m['type']=='TASK':
            t['ack']=ack; t['state']='RUNNING' if accept else 'REJECTED'
            if accept: t['deadline']=self.mono()+b['execution_timeout_ms']/1000
            # Resolve all copies that arrived with fresh msg_id while awaiting operator.
            for mid,other in self.incoming.items():
                if mid not in self.acks and other['type']=='TASK' and other['body']['task_id']==tid: self.reply_ack(other,accept,reason)
        elif accept:
            t['cancel_accepted']=True
            if t['state']=='PENDING':
                for mid,other in list(self.incoming.items()):
                    if mid not in self.acks and other['type']=='TASK' and other['body']['task_id']==tid:
                        t['ack']=self.reply_ack(other,False,'CANCELLED')
                t['state']='REJECTED'; t['cancelled']=True
            else: self.log('CANCEL_ACCEPTED_WAIT_STOP',tid)
        return ack

    def finish(self,tid,status,detail):
        t=self.tasks.get(tid)
        if not t or t['state']!='RUNNING': raise ValueError('only RUNNING tasks can produce one final result')
        if status not in STATUSES or not isinstance(detail,dict): raise ValueError('invalid result')
        if status=='ABORTED' and not t.get('cancel_accepted'): raise ValueError('accept cancellation first')
        body={'task_id':tid,'status':status,'detail':detail}
        m=self.report('RESULT',body,t['trace'])
        t['state']='TERMINAL'; t['result']=copy.deepcopy(body); t['result_message']=m
        self.log('RESULT_QUEUED',m); return m

    def task(self,body,trace=None):
        if not self.center: raise ValueError('center command only')
        if not self.up: raise ValueError('REJECTED: link not ready')
        tid=body.get('task_id') or uid(); b=dict(body,task_id=tid)
        trace=trace or tid; m=self.message('TASK',b,trace); encode(m)
        if self.wall()*1000>=b['expire_at_unix_ms']: raise ValueError('EXPIRED')
        if tid in self.tasks: raise ValueError('task_id exists; use resend to preserve identity')
        self.tasks[tid]={'body':b,'trace':trace,'state':'QUEUED'}; self.last_task=tid
        self.queue_request(m); return tid

    def queue_request(self,m):
        self.requests[m['msg_id']]={'message':m,'last':-1e9,'done':False,'attempted':False,'enabled':True,
            'deadline':self.mono()+max(0,(m['body']['expire_at_unix_ms']-self.wall()*1000)/1000)}
        self.log('QUEUED',m)

    def cancel(self,tid,reason,expires,skew):
        if not self.center or not self.up: raise ValueError('center link not ready')
        t=self.tasks[tid]
        b={'task_id':tid,'reason':reason,'expire_at_unix_ms':expires,'max_clock_skew_ms':skew}
        m=self.message('CANCEL',b,t['trace']); encode(m)
        if self.wall()*1000>=expires: raise ValueError('EXPIRED')
        self.queue_request(m); return m['msg_id']

    def event(self,channel,ratio,severity):
        if self.center: raise ValueError('dog command only')
        m=self.report('EVENT',{'event_code':channel+'_BLOCKED','severity':severity,'payload':{'channel':channel,'block_ratio':ratio}},uid())
        self.log('EVENT_QUEUED',m); return m['msg_id']
