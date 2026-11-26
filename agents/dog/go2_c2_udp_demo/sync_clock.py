#!/usr/bin/env python3
"""Explicit one-shot clock sync with the USB center's NTP service; run with sudo."""
import argparse
import math
import socket
import struct
import time
from clock_check import stamp, seconds


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--server',default='192.168.55.100')
    p.add_argument('--port',type=int,default=19123)
    a=p.parse_args()
    raw=bytearray(48);raw[0]=0x23
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
        s.settimeout(2);s.connect((a.server,a.port))
        start=time.time();mono=time.monotonic();raw[40:48]=stamp(start);s.send(raw)
        data=s.recv(512);end=time.time();elapsed=time.monotonic()-mono
    if len(data)<48 or data[0]>>6==3 or data[0]&7!=4 or not 1<=data[1]<=15 or data[24:32]!=raw[40:48]:
        raise ValueError('invalid or unsynchronized NTP source')
    if abs((end-start)-elapsed)>.01:raise ValueError('local clock jumped during measurement')
    received=seconds(data[32:40]);sent=seconds(data[40:48])
    delay=(end-start)-(sent-received)
    uncertainty=max(0,delay)/2+max(0,struct.unpack('!i',data[4:8])[0]/65536)/2+struct.unpack('!I',data[8:12])[0]/65536
    if delay<-.001 or uncertainty>1:raise ValueError('NTP uncertainty exceeds 1 second')
    offset=((received-start)+(sent-end))/2
    if not math.isfinite(offset):raise ValueError('invalid offset')
    time.clock_settime(time.CLOCK_REALTIME,time.time()+offset)
    print('Clock synchronized; offset_seconds=%.6f uncertainty_ms=%.1f'%(offset,uncertainty*1000))

if __name__=='__main__':main()
