"""Read-only NTP check against the configured PC, without changing system time."""
import math
import socket
import struct
import time
EPOCH=2208988800

def stamp(t):
    n=t+EPOCH; return struct.pack('!II',int(n),int((n-int(n))*2**32))
def seconds(raw):
    s,f=struct.unpack('!II',raw); return s-EPOCH+f/2**32

def clock_bound(server,timeout=.3,port=123):
    raw=bytearray(48); raw[0]=0x23
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
        s.settimeout(timeout); s.connect((server,port))
        start=time.time(); raw[40:48]=stamp(start); s.send(raw)
        data=s.recv(512); end=time.time()
    if len(data)<48 or data[0]>>6==3 or data[0]&7!=4 or not 1<=data[1]<=15 or data[24:32]!=raw[40:48]: raise ValueError('invalid or unsynchronized NTP response')
    recv=seconds(data[32:40]); sent=seconds(data[40:48])
    delay=(end-start)-(sent-recv)
    if delay<-.001 or end<start: raise ValueError('clock jumped during check')
    offset=((recv-start)+(sent-end))/2
    root_delay=struct.unpack('!i',data[4:8])[0]/65536
    dispersion=struct.unpack('!I',data[8:12])[0]/65536
    bound=math.ceil((abs(offset)+max(0,delay)/2+max(0,root_delay)/2+dispersion)*1000)+10
    if bound>1000: raise ValueError('NTP uncertainty/skew exceeds 1000ms')
    return bound
