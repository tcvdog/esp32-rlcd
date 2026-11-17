#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/setup_idf.sh"
cd "$HERE/joystick_test"
PORT="${1:-/dev/ttyACM0}"
echo "Flashing $PORT ..."
idf.py -p "$PORT" flash monitor
