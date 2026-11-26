#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
exec python3 -u c2_udp.py center "$@"
