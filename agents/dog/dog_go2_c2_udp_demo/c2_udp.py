#!/usr/bin/env python3
"""Interactive manual simulator using multi-robot-c2 JSON/UDP v0.4."""
import argparse
import json
import math
import os
from pathlib import Path
import select
import socket
import sys
import time
from clock_check import clock_bound
from endpoint import Endpoint
from protocol_v04 import decode, uint, valid

HELP = '''Local commands (never sent as plaintext over the robot link):
  status
  event A 0.8 1                         dog: report blockage
  accept <task_id|msg_id|last>           dog: accept pending TASK/CANCEL
  reject <task_id|msg_id|last> CAPABILITY
  result <task_id|last> SUCCESS {"note":"manual simulation complete"}
  result <task_id|last> FAILED {"reason":"manual failure"}
  result <task_id|last> ABORTED {"reason":"manual stop confirmed"}
  task {"action":"OBSERVE","params":{"area":"A","alt_m":15,"loiter_s":60},"execution_timeout_ms":120000,"expire_in_ms":1200000,"max_clock_skew_ms":600000}
  cancel <task_id|last> {"reason":"operator","expire_in_ms":1200000,"max_clock_skew_ms":600000}
  query <task_id|last>                  center: query task state
  clock <bound_ms|unknown>             confirm measured absolute clock skew bound
  help / quit
Task action and params are opaque to this manual simulator; accepting them does
not certify robot capability. No ROS, SDK or physical control calls are made.
'''

def output(kind,value=''):
    print(time.strftime('%H:%M:%S'),kind,json.dumps(value,ensure_ascii=False,allow_nan=False) if isinstance(value,(dict,list)) else value,flush=True)

def command(ep,line):
    cmd,_,rest=line.strip().partition(' ')
    if cmd in ('help','?'): print(HELP,flush=True); return 'OK'
    if cmd=='quit': raise KeyboardInterrupt
    if cmd=='status':
        st={'session_id':ep.session,'peer_session_id':ep.peer,'link_up':ep.up,'clock_bound_ms':ep.clock_bound,
            'tasks':{tid:{'state':t['state'],'trace_id':t['trace'],'cancel_accepted':t.get('cancel_accepted',False)} for tid,t in ep.tasks.items()},
            'undecided':[{'msg_id':mid,'task_id':m['body']['task_id'],'type':m['type']} for mid,m in ep.incoming.items() if mid not in ep.acks],
            'unconfirmed_reports':list(ep.pending)}
        output('STATUS',st); return json.dumps(st)
    if cmd=='clock':
        value=None if rest=='unknown' else int(rest)
        if value is not None and not uint(value,32): raise ValueError('invalid clock bound')
        ep.clock_bound=value; output('CLOCK_BOUND_MS',value); return 'OK'
    if cmd=='event':
        if rest.startswith('{'):
            if ep.center: raise ValueError('dog command only')
            body=decode(rest.encode('utf-8'))
            from endpoint import uid
            m=ep.report('EVENT',body,uid()); output('EVENT_QUEUED',m); return 'QUEUED '+m['msg_id']
        args=rest.split(); ch=args[0] if args else 'A'; ratio=float(args[1]) if len(args)>1 else .8; severity=int(args[2]) if len(args)>2 else 1
        if ch not in ('A','B','C') or not math.isfinite(ratio) or not 0<=ratio<=1 or severity not in (0,1,2): raise ValueError('invalid event arguments')
        return 'QUEUED '+ep.event(ch,ratio,severity)
    if cmd=='task':
        x=decode(rest.encode('utf-8')); trace=x.pop('trace_id',None)
        if 'expire_in_ms' in x: x['expire_at_unix_ms']=int(ep.wall()*1000)+x.pop('expire_in_ms')
        return 'QUEUED '+ep.task(x,trace)
    if cmd in ('accept','reject','result','cancel','query'):
        ref,_,tail=rest.partition(' '); ref=ep.last_task if ref=='last' else ref
        if not ref: raise ValueError('specify a task_id or msg_id')
        if cmd in ('accept','reject'):
            if ep.center: raise ValueError('dog command only')
            ep.decide(ref,cmd=='accept',tail or 'CAPABILITY'); return 'OK'
        if cmd=='result':
            if ep.center: raise ValueError('dog command only')
            status,_,detail=tail.partition(' '); ep.finish(ref,status,decode((detail or '{}').encode('utf-8'))); return 'QUEUED RESULT'
        if cmd=='query':
            if not ep.center or not ep.verified: raise ValueError('center session not ready')
            return 'QUEUED '+ep.query(ref,ep.tasks[ref]['trace'])
        x=decode(tail.encode('utf-8'))
        return 'QUEUED '+ep.cancel(ref,x['reason'],int(ep.wall()*1000)+x['expire_in_ms'],x['max_clock_skew_ms'])
    raise ValueError('unknown command; type help')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('role',choices=['center','dog','trigger'])
    p.add_argument('--bind'); p.add_argument('--peer'); p.add_argument('--port',type=int); p.add_argument('--peer-port',type=int)
    p.add_argument('--control-port',type=int,help='loopback operator port: dog 45992, center 45993')
    p.add_argument('--ntp-port',type=int,default=19123,help='center read-only NTP service port')
    p.add_argument('--clock-bound-ms',type=int,default=None,help='externally measured absolute wall-clock skew upper bound; default unknown')
    p.add_argument('--channel',choices=['A','B','C'],default='A'); p.add_argument('--ratio',type=float,default=.8); p.add_argument('--severity',type=int,choices=[0,1,2],default=1)
    p.add_argument('--drop-first-receipt',action='store_true'); p.add_argument('--drop-first-ack',action='store_true'); p.add_argument('--drop-first-result',action='store_true')
    a=p.parse_args()
    if a.role=='trigger':
        if not math.isfinite(a.ratio) or not 0<=a.ratio<=1: p.error('invalid ratio')
        s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.settimeout(2)
        s.sendto(json.dumps({'channel':a.channel,'ratio':a.ratio,'severity':a.severity}).encode(),('127.0.0.1',45992))
        try: reply=s.recv(4096).decode(); print(reply)
        except socket.timeout: sys.exit('dog agent did not respond; start it first')
        if not reply.startswith('QUEUED'): sys.exit(1)
        return
    if a.clock_bound_ms is not None and not uint(a.clock_bound_ms,32): p.error('invalid clock bound')
    center=a.role=='center'; local=a.bind or ('192.168.55.100' if center else '192.168.55.1'); peer=a.peer or ('192.168.55.1' if center else '192.168.55.100')
    port=a.port or (19000 if center else 19003); peer_port=a.peer_port or (19003 if center else 19000)
    sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); sock.bind((local,port)); sock.setblocking(False)
    journal=None
    def record(direction,raw):
        if journal is not None:
            entry={'direction':direction,'local_unix_ms':int(time.time()*1000),'message':decode(raw)}
            journal.write(json.dumps(entry,ensure_ascii=False,allow_nan=False,separators=(',',':'))+'\n'); journal.flush()
    drops={'RECEIPT':a.drop_first_receipt,'ACK':a.drop_first_ack,'RESULT':a.drop_first_result}
    def emit(raw):
        typ=decode(raw)['type']
        if drops.get(typ): drops[typ]=False; record('tx_test_drop',raw); output('TEST_DROP',typ); return
        sock.sendto(raw,(peer,peer_port)); record('tx',raw)
    ep=Endpoint(center,emit,output,clock_bound=a.clock_bound_ms)
    logdir=Path(__file__).resolve().parent/'logs'; logdir.mkdir(exist_ok=True)
    journal_path=logdir/('wire-'+a.role+'-'+ep.session+'.jsonl')
    journal=journal_path.open('a',encoding='utf-8')
    output('JSONL_LOG',str(journal_path))
    ntp=None
    if center:
        ntp=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        ntp.bind((local,a.ntp_port)); ntp.setblocking(False)
    trigger=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
    trigger.bind(('127.0.0.1',a.control_port or (45993 if center else 45992)))
    trigger.setblocking(False)
    output('START',{'role':a.role,'local':[local,port],'peer':[peer,peer_port],'session_id':ep.session})
    print(HELP,flush=True); stdin_open=True; last_clock=-1e9; input_buffer=b''
    try:
        while True:
            if not center and a.clock_bound_ms is None and time.monotonic()-last_clock>=10:
                last_clock=time.monotonic()
                try: ep.clock_bound=clock_bound(peer,port=a.ntp_port); output('CLOCK_BOUND_MS',ep.clock_bound)
                except (OSError,ValueError) as exc: ep.clock_bound=None; output('CLOCK_UNSYNCED',str(exc))
            ep.tick()
            readers=[sock]+([ntp] if ntp else [])+([trigger] if trigger else [])+([sys.stdin] if stdin_open else [])
            ready,_,_=select.select(readers,[],[],.05)
            for inp in ready:
                if inp is sys.stdin:
                    chunk=os.read(sys.stdin.fileno(),4096)
                    if not chunk: stdin_open=False; continue
                    input_buffer+=chunk
                    while b'\n' in input_buffer:
                        raw_line,input_buffer=input_buffer.split(b'\n',1)
                        try: output('LOCAL',command(ep,raw_line.decode('utf-8')))
                        except (ValueError,KeyError,TypeError,IndexError,OverflowError) as exc: output('COMMAND_ERROR',str(exc))
                else:
                    raw,addr=inp.recvfrom(65535)
                    if inp is ntp:
                        if addr[0]==peer:
                            from ntp_server import response
                            reply=response(raw)
                            if reply is not None: ntp.sendto(reply,addr)
                        continue
                    if inp is sock:
                        if addr==(peer,peer_port):
                            try:
                                if valid(decode(raw),ep.dst,ep.src): record('rx',raw)
                            except (ValueError,UnicodeDecodeError,RecursionError): pass
                            ep.receive(raw)
                        else: output('DROP','unexpected source address')
                    else:
                        from local_commands import handle
                        reply=handle(ep,raw,command)
                        trigger.sendto(reply,addr)
    finally:
        sock.close()
        if journal: journal.close()
        if trigger: trigger.close()
        if ntp: ntp.close()

if __name__=='__main__':
    try: main()
    except KeyboardInterrupt: output('STOP')
