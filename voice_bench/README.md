# Voice benchmark

Record 10 fixed commands through ESP C+D, save as:

```
voice_bench/wavs/01_closed_loop.wav
...
```

Then run:

```bash
/home/tcvdog/voice-venv/bin/python voice_bench.py
```

It compares DeepSeek ASR and SenseVoice on a conservative
mono/trim/normalize pipeline, and prints character error rate (CER).

Do not tune the pipeline on a single sentence; use the average.
