#!/usr/bin/env python3
"""Benchmark ASR variants against a small fixed command set.
Put recordings in voice_bench/wavs/<id>.wav and run:
  /home/tcvdog/voice-venv/bin/python voice_bench.py
"""
import pathlib, re, sys, difflib, wave
import preprocess_wav, voice_server as v

ROOT = pathlib.Path(__file__).resolve().parent
BENCH = ROOT / 'voice_bench'
WAVS = BENCH / 'wavs'
REFS = {}
for line in (BENCH / 'phrases.tsv').read_text(encoding='utf-8').splitlines()[1:]:
    if '\t' in line:
        k, t = line.split('\t', 1)
        REFS[k] = t

def norm(s):
    return re.sub(r'[^\u4e00-\u9fffA-Za-z0-9]', '', s or '')

def cer(ref, hyp):
    a, b = norm(ref), norm(hyp)
    if not a:
        return 1.0
    return 1.0 - difflib.SequenceMatcher(None, a, b).ratio()

def run(engine, path):
    try:
        if engine == 'deepseek':
            return v.transcribe_deepseek(path)[0]
        if engine == 'sensevoice':
            return v.transcribe_funasr(path)
        if engine == 'whisper':
            return v.transcribe_whisper(path)
        return v.transcribe(path)
    except Exception as e:
        return f'<ERR {e}>'

def main():
    rows = []
    for k, ref in REFS.items():
        src = WAVS / f'{k}.wav'
        if not src.exists():
            continue
        variants = {}
        try:
            preprocess_wav.preprocess(str(src), '/tmp/bench_norm.wav', denoise=False, highpass=False)
            variants['norm'] = '/tmp/bench_norm.wav'
        except Exception as e:
            print('preprocess', k, e)
            continue
        for eng in ['deepseek', 'sensevoice', 'whisper', 'vosk']:
            for vn, path in variants.items():
                hyp = run(eng, path)
                rows.append((k, eng, vn, cer(ref, hyp), hyp))
    if not rows:
        print('No recordings. Put WAVs in', WAVS); return
    print(f"{'id':22} {'engine':10} {'var':6} {'CER':>6}  text")
    for k, eng, vn, c, hyp in rows:
        print(f'{k:22} {eng:10} {vn:6} {c:6.3f}  {hyp}')
    for eng in ['deepseek','sensevoice','whisper','vosk']:
        xs=[c for _,e,_,c,_ in rows if e==eng]
        if xs: print(f'AVG {eng:10} CER {sum(xs)/len(xs):.3f}')

if __name__ == '__main__':
    main()
