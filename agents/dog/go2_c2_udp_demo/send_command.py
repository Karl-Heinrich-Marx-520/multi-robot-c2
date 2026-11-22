#!/usr/bin/env python3
"""Send operator commands from a separate quiet terminal; no SDK or wire IDs here."""
import argparse
import json
import socket
import sys
import uuid


def send(line, port, timeout=3):
    request_id = uuid.uuid4().hex
    raw = json.dumps({'request_id': request_id, 'command': line},
                     ensure_ascii=False, allow_nan=False).encode('utf-8')
    if len(raw) > 1200:
        raise ValueError('local command exceeds 1200 bytes')
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.connect(('127.0.0.1', port))
        sock.settimeout(timeout)
        sock.send(raw)
        try:
            reply = sock.recv(65535)
        except (socket.timeout, ConnectionRefusedError) as exc:
            raise RuntimeError('No agent reply: start/restart start_dog.sh (or start_center.sh). '
                               'The command may have been processed; check status/logs before resending.') from exc
    try:
        result = json.loads(reply)
    except (ValueError, UnicodeError) as exc:
        raise RuntimeError('Agent uses the old local interface; restart it once to load this update. '
                           'Reply: ' + reply.decode('utf-8', errors='replace')) from exc
    if not isinstance(result, dict) or result.get('request_id') != request_id:
        raise RuntimeError('Mismatched local reply')
    if not result.get('ok'):
        raise ValueError(result.get('error', 'command rejected'))
    text = result['result']
    try:
        return json.dumps(json.loads(text), ensure_ascii=False, indent=2)
    except (ValueError, TypeError):
        return str(text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--role', choices=['dog', 'center'], default='dog')
    parser.add_argument('--port', type=int, help='override local control UDP port')
    parser.add_argument('command', nargs=argparse.REMAINDER,
                        help='e.g. event A 0.8 1; omit for interactive prompt')
    args = parser.parse_args()
    port = args.port or (45992 if args.role == 'dog' else 45993)
    if args.command:
        print(send(' '.join(args.command), port))
        return
    print('Local %s commands. Type help for commands; exit leaves the agent running.' % args.role)
    while True:
        try:
            line = input(args.role + '> ').strip()
            if line in ('exit', 'quit'):
                return
            if line:
                try:
                    print(send(line, port))
                except (OSError, ValueError, RuntimeError) as exc:
                    print('ERROR:', exc)
        except (EOFError, KeyboardInterrupt):
            print()
            return


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError) as exc:
        print('ERROR:', exc, file=sys.stderr)
        sys.exit(1)
