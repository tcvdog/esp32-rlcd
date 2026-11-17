#!/usr/bin/env python3
"""Update the ESP32 voice-command feedback screen.
Usage: python3 esp_feedback.py STAGE [DETAIL] [TEXT]
"""
import json, sys, urllib.request
stage = sys.argv[1] if len(sys.argv) > 1 else '执行中'
detail = sys.argv[2] if len(sys.argv) > 2 else ''
text = sys.argv[3] if len(sys.argv) > 3 else ''
body = json.dumps({'stage': stage, 'detail': detail, 'text': text}).encode()
req = urllib.request.Request('http://192.168.3.103/feedback', data=body,
                             headers={'Content-Type': 'application/json'}, method='POST')
try:
    print(urllib.request.urlopen(req, timeout=4).read().decode())
except Exception as e:
    print('feedback failed:', e)
