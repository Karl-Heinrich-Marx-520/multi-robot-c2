"""Small read-only NTP responder for USB-connected agents (unprivileged port).

Reports unsynchronized unless the center's system time is synchronized.
No clock-setting operations are performed here.
"""
import struct
import subprocess
import time
from clock_check import stamp


def response(data):
    if len(data) < 48 or data[0] & 7 != 3:
        return None
    received = time.time()
    synced = False
    dispersion = 1.0
    delay = 0.0
    stratum = 16
    try:
        tracking = subprocess.check_output(
            ['chronyc', 'tracking'], timeout=1,
            stderr=subprocess.DEVNULL).decode()
        fields = dict(line.split(':', 1) for line in tracking.splitlines() if ':' in line)
        fields = {k.strip(): v.strip() for k, v in fields.items()}
        synced = fields.get('Leap status') == 'Normal'
        dispersion = (float(fields['Root dispersion'].split()[0])
                      + abs(float(fields['System time'].split()[0])) + 0.01)
        delay = max(0.0, float(fields['Root delay'].split()[0]))
        stratum = min(16, int(fields['Stratum']) + 1)
        synced = synced and stratum < 16
    except (OSError, subprocess.SubprocessError, ValueError, KeyError):
        synced = False
    raw = bytearray(48)
    raw[0] = 0x24 if synced else 0xe4
    raw[1] = stratum if synced else 16
    raw[2] = 4
    raw[3] = 0xec
    raw[4:8] = struct.pack('!i', min(2**31-1, int(delay*65536)))
    raw[8:12] = struct.pack('!I', min(2**32-1, int(dispersion*65536)+1))
    raw[12:16] = b'LOCL'
    raw[16:24] = stamp(received)
    raw[24:32] = data[40:48]
    raw[32:40] = stamp(received)
    raw[40:48] = stamp(time.time())
    return bytes(raw)
