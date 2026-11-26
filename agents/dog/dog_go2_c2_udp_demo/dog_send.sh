#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
exec python3 send_command.py --role dog "$@"
