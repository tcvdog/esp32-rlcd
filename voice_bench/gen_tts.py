#!/usr/bin/env python3
import pathlib, yaml, requests
ROOT = pathlib.Path(__file__).resolve().parent
d = yaml.safe_load((pathlib.Path.home()/'.dsh/.credentials.yaml').read_text())
key = d['refs']['DEEPSEEK_API_KEY']
(ROOT/'wavs').mkdir(exist_ok=True)
for line in (ROOT/'phrases.tsv').read_text(encoding='utf-8').splitlines()[1:]:
    if '\t' not in line: continue
    k, text = line.split('\t',1)
    r = requests.post('https://api.deepseek.com/v1/tools/tts',
        headers={'Authorization':'Bearer '+key,'Content-Type':'application/json'},
        json={'text':text,'voice_id':'stella','format':'wav'}, timeout=60)
    r.raise_for_status()
    (ROOT/'wavs'/f'{k}.wav').write_bytes(r.content)
    print(k, len(r.content))
