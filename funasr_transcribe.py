#!/usr/bin/env python3
import contextlib, io, re, sys
from funasr import AutoModel
MODEL = '/mnt/documents/dsh-funasr-voice/models/SenseVoiceSmall'

def main(path):
    # FunASR prints its version banner to stdout; keep stdout clean for the caller.
    with contextlib.redirect_stdout(io.StringIO()):
        m = AutoModel(model=MODEL, disable_update=True, device='cpu')
        r = m.generate(input=path, language='zh', use_itn=True, batch_size_s=60)
    text = r[0].get('text', '') if r else ''
    text = re.sub(r'<\|[^|]+\|>', '', text).strip()
    print(text)

if __name__ == '__main__':
    main(sys.argv[1])
