#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
exec python3 c2_udp.py trigger --channel "${1:-A}" --ratio "${2:-0.8}" --severity "${3:-1}"
