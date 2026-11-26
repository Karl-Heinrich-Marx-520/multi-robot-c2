"""multi-robot-c2 protocol v0.4 (wire version 1), Python 3.8+."""
import json
import math
import re
ID = re.compile(r'^[0-9a-f]{32}$')
REASONS = {'BUSY','MODE_MISMATCH','CAPABILITY','BAD_PARAM','EXPIRED','CLOCK_UNSYNCED','UNKNOWN_TASK','CANCELLED','ALREADY_FINISHED','ID_CONFLICT'}
STATUSES = {'SUCCESS','FAILED','ABORTED'}
EVENTS = {'A_BLOCKED','B_BLOCKED','C_BLOCKED','LOW_BATTERY','ARRIVED','FAULT'}
def is_id(x): return isinstance(x,str) and ID.fullmatch(x) is not None
def uint(x,n): return type(x) is int and 0 <= x < 2**n
def finite(x):
    if isinstance(x,float): return math.isfinite(x)
    if isinstance(x,dict): return all(finite(v) for v in x.values())
    if isinstance(x,list): return all(finite(v) for v in x)
    return True

def decode(data):
    def pairs(items):
        d={}
        for k,v in items:
            if k in d: raise ValueError('duplicate JSON key')
            d[k]=v
        return d
    if len(data)>1200: raise ValueError('datagram exceeds 1200 bytes')
    return json.loads(data.decode('utf-8'),object_pairs_hook=pairs,
                      parse_constant=lambda x: (_ for _ in ()).throw(ValueError('nonfinite JSON')))

def body_valid(t,b):
    if not isinstance(b,dict) or not finite(b): return False
    keys=set(b)
    if t=='EVENT': return keys=={'event_code','severity','payload'} and isinstance(b['event_code'],str) and b['event_code'] in EVENTS and uint(b['severity'],8) and b['severity']<=2 and isinstance(b['payload'],dict)
    if t=='TASK': return keys=={'task_id','action','params','expire_at_unix_ms','max_clock_skew_ms','execution_timeout_ms'} and is_id(b['task_id']) and isinstance(b['action'],str) and bool(b['action']) and isinstance(b['params'],dict) and uint(b['expire_at_unix_ms'],64) and uint(b['max_clock_skew_ms'],32) and uint(b['execution_timeout_ms'],32)
    if t=='CANCEL': return keys=={'task_id','reason','expire_at_unix_ms','max_clock_skew_ms'} and is_id(b['task_id']) and isinstance(b['reason'],str) and uint(b['expire_at_unix_ms'],64) and uint(b['max_clock_skew_ms'],32)
    if t=='ACK': return keys=={'ack_for_msg_id','task_id','accepted','reject_reason'} and is_id(b['ack_for_msg_id']) and is_id(b['task_id']) and type(b['accepted']) is bool and (b['reject_reason'] is None if b['accepted'] else isinstance(b['reject_reason'],str) and b['reject_reason'] in REASONS)
    if t=='RESULT': return keys=={'task_id','status','detail'} and is_id(b['task_id']) and isinstance(b['status'],str) and b['status'] in STATUSES and isinstance(b['detail'],dict)
    if t=='RECEIPT': return keys=={'ack_for_msg_id'} and is_id(b['ack_for_msg_id'])
    if t!='LINK': return False
    kind=b.get('kind')
    if kind=='HEARTBEAT': return keys=={'kind','hb_seq'} and uint(b['hb_seq'],32)
    if kind=='RESYNC_REQUEST': return keys=={'kind','request_id','task_id'} and is_id(b['request_id']) and (b['task_id']=='' or is_id(b['task_id']))
    if kind=='RESYNC_STATE':
        if keys!={'kind','request_id','task_id','task_state','task_ack','result'} or not is_id(b['request_id']) or not (b['task_id']=='' or is_id(b['task_id'])): return False
        st=b['task_state']; ack=b['task_ack']; result=b['result']
        if st=='UNKNOWN': return ack is None and result is None
        if not is_id(b['task_id']): return False
        if st=='REJECTED': return result is None and (ack is None or body_valid('ACK',ack) and not ack['accepted'] and ack['task_id']==b['task_id'])
        if st in ('RUNNING','TERMINAL'):
            return body_valid('ACK',ack) and ack['accepted'] and ack['task_id']==b['task_id'] and (result is None if st=='RUNNING' else body_valid('RESULT',result) and result['task_id']==b['task_id'])
    return False

def valid(m,src=None,dst=None):
    if not isinstance(m,dict) or not finite(m) or set(m)!={'version','type','msg_id','session_id','dst_session_id','src_id','dst_id','trace_id','ts_mono_ms','body'}: return False
    if type(m['version']) is not int or m['version']!=1 or not is_id(m['msg_id']) or not is_id(m['session_id']) or not uint(m['ts_mono_ms'],64): return False
    if not isinstance(m['src_id'],str) or not m['src_id'] or not isinstance(m['dst_id'],str) or not m['dst_id']: return False
    if src is not None and m['src_id']!=src or dst is not None and m['dst_id']!=dst: return False
    if not isinstance(m['type'],str) or not body_valid(m['type'],m['body']): return False
    hb=m['type']=='LINK' and (m['body'].get('kind')=='HEARTBEAT' or m['body'].get('kind')=='RESYNC_REQUEST' and m['body']['task_id']=='')
    if not is_id(m['dst_session_id']) and not ((hb or m['type']=='TASK') and m['dst_session_id'] is None): return False
    if not is_id(m['trace_id']) and not (m['type']=='LINK' and m['trace_id'] is None): return False
    return True

def encode(m):
    if not valid(m): raise ValueError('invalid protocol message')
    raw=json.dumps(m,ensure_ascii=False,allow_nan=False,separators=(',',':')).encode('utf-8')
    if len(raw)>1200: raise ValueError('datagram exceeds 1200 bytes')
    return raw
