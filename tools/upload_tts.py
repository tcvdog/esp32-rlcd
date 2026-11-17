#!/usr/bin/env python3
"""Upload tts_build/*.mp3 to the board SD card under /sdcard/tts."""
import concurrent.futures, pathlib, requests, sys
ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "tts_build"
BOARD = sys.argv[1] if len(sys.argv) > 1 else "192.168.3.103"
files = sorted(SRC.glob("*.mp3"))
def up(p):
    r = requests.post(f"http://{BOARD}/upload?name=tts/{p.name}", data=p.read_bytes(), timeout=30)
    return p.name, r.status_code, r.text[:40]
with concurrent.futures.ThreadPoolExecutor(max_workers=4) as ex:
    for name, code, text in ex.map(up, files):
        if code != 200 or text.strip() != "OK": print("FAIL", name, code, text)
print("uploaded", len(files), "files to", BOARD)
