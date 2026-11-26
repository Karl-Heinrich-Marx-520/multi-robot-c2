"""Loopback-only operator API; wire messages still use the protocol endpoint."""
import json
from protocol_v04 import decode


def handle(ep, raw, execute=None):
    if execute is None:
        from c2_udp import command as execute
    request_id = None
    try:
        request = decode(raw)
        if not isinstance(request, dict):
            raise ValueError('local request must be an object')
        # Keep the previous send_event.sh / trigger interface working.
        if 'command' not in request:
            result = execute(ep, 'event %s %s %s' % (
                request['channel'], request['ratio'], request['severity']))
            return result.encode('utf-8')
        request_id = request.get('request_id')
        line = request['command']
        if not isinstance(request_id, str) or not isinstance(line, str):
            raise ValueError('request_id and command must be strings')
        allowed = ({'status', 'help', 'task', 'cancel', 'query'} if ep.center
                   else {'status', 'help', 'event', 'accept', 'reject', 'result'})
        if not line.strip() or '\n' in line or '\r' in line:
            raise ValueError('send exactly one command')
        if line.strip().split()[0] not in allowed:
            raise ValueError('command unavailable for this role: ' + ', '.join(sorted(allowed)))
        result = execute(ep, line)
        if line.strip() == 'help':
            result = ', '.join(sorted(allowed))
        return json.dumps({'request_id': request_id, 'ok': True,
                                       'result': result}, ensure_ascii=False).encode('utf-8')
    except (ValueError, KeyError, TypeError, IndexError, OverflowError, UnicodeError) as exc:
        if request_id is None:
            return ('ERROR ' + str(exc)).encode('utf-8')
        return json.dumps({'request_id': request_id, 'ok': False,
                                       'error': str(exc)}, ensure_ascii=False).encode('utf-8')
