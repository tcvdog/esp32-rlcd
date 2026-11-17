#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IP="${1:?usage: ota_upload.sh <board-ip> [firmware.bin]}"
BIN="${2:-$HERE/game_proto/build/game_proto.bin}"
[ -f "$BIN" ] || { echo "missing $BIN" >&2; exit 1; }
VER=$(grep -m1 'FW_VERSION' "$HERE/game_proto/main/fw_version.h" | sed -E 's/.*"(.*)".*/\1/')
echo "Firmware $VER"
echo "Uploading $BIN to http://$IP/update ..."
curl --fail --max-time 300 --data-binary @"$BIN" -H 'Content-Type: application/octet-stream' "http://$IP/update"
echo
