#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$HERE/setup_idf.sh"
PORT="${1:-/dev/ttyACM0}"
cd "$HERE/game_proto/build"

if ! esptool.py --chip esp32s3 -p "$PORT" --before no_reset --after no_reset chip_id >/tmp/chip_id.log 2>&1; then
  echo "Board is not in ROM download mode (or $PORT is not accessible)." >&2
  echo "Long-press PWR, hold BOOT, click PWR, release BOOT after 2 s." >&2
  echo "Then: sudo chmod 666 $PORT" >&2
  exit 1
fi

esptool.py --chip esp32s3 -p "$PORT" -b 460800 --before no_reset --after no_reset write_flash @flash_args
esptool.py --chip esp32s3 -p "$PORT" --before no_reset --after watchdog_reset chip_id
