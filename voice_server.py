#!/usr/bin/env python3
"""Local voice notebook: ESP32 WAV -> faster-whisper -> local Ollama qwen2.5."""
import html, json, os, pathlib, time, wave, audioop, struct, math, threading, subprocess
import yaml
from concurrent.futures import ThreadPoolExecutor
from preprocess_wav import preprocess
import numpy as np
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import requests
import vosk

ROOT = pathlib.Path(__file__).resolve().parent
NOTES = ROOT / 'voice_notes.jsonl'
LAST_WAV = ROOT / 'voice_last.wav'
CMD_WAV = ROOT / 'voice_command.wav'
CMD_CLEAN = ROOT / 'voice_command_clean.wav'
CMD_LOG = ROOT / 'voice_commands.jsonl'
AUDIO_DIR = ROOT / 'voice_audio'
CAPTURE_DIR = ROOT / 'voice_capture'
BOARD_NOTE_URL = 'http://192.168.3.103/note'
AUDIO_DIR.mkdir(exist_ok=True)
SPEAKERS = ROOT / 'speaker_profiles.json'
PORT = 8765

VOSK_DIR = pathlib.Path('/home/tcvdog/voice-venv/vosk-model-small-cn-0.22')
print('loading vosk cn model...', flush=True)
MODEL = vosk.Model(str(VOSK_DIR))
print('vosk ready', flush=True)

def _read_audio(path):
    with wave.open(str(path), 'rb') as wf:
        if wf.getsampwidth() != 2:
            return None
        data = wf.readframes(wf.getnframes())
        if wf.getnchannels() == 2:
            data = audioop.tomono(data, 2, 0.5, 0.5)
        if wf.getframerate() != 16000:
            data, _ = audioop.ratecv(data, 2, 1, wf.getframerate(), 16000, None)
    x = np.frombuffer(data, dtype='<i2').astype(np.float32) / 32768.0
    if len(x) < 1600:
        return None
    peak = float(np.max(np.abs(x)))
    if peak > 0:
        x = x / peak * 0.9
    return x

def _mel_filterbank(n_mels=26, n_fft=512, sr=16000):
    def hz2mel(f): return 2595.0 * np.log10(1.0 + f / 700.0)
    def mel2hz(m): return 700.0 * (10.0 ** (m / 2595.0) - 1.0)
    mels = np.linspace(hz2mel(80), hz2mel(7600), n_mels + 2)
    bins = np.floor((n_fft + 1) * mel2hz(mels) / sr).astype(int)
    fb = np.zeros((n_mels, n_fft // 2 + 1), dtype=np.float32)
    for i in range(n_mels):
        l, c, r = bins[i], bins[i+1], bins[i+2]
        if c == l: c += 1
        if r == c: r += 1
        for k in range(l, min(c, fb.shape[1])):
            fb[i, k] = (k - l) / max(1, c - l)
        for k in range(c, min(r, fb.shape[1])):
            fb[i, k] = (r - k) / max(1, r - c)
    return fb

_MEL = _mel_filterbank()
_DCT = np.array([[math.cos(math.pi * k * (n + 0.5) / 26) for n in range(26)] for k in range(13)], dtype=np.float32)

def speaker_embedding(path):
    x = _read_audio(path)
    if x is None:
        return None
    frame, hop = 400, 160
    n = 1 + (len(x) - frame) // hop
    if n <= 2:
        return None
    frames = np.stack([x[i*hop:i*hop+frame] for i in range(n)])
    energy = np.sqrt(np.mean(frames * frames, axis=1) + 1e-12)
    active = energy > max(0.02, float(np.max(energy)) * 0.20)
    if int(np.sum(active)) < 3:
        return None
    frames = frames[active]
    frames = frames - np.mean(frames, axis=1, keepdims=True)
    frames = np.append(frames[:, 0:1], frames[:, 1:] - 0.97 * frames[:, :-1], axis=1)
    frames = frames * np.hamming(frame)[None, :]
    power = np.abs(np.fft.rfft(frames, n=512, axis=1)) ** 2
    logmel = np.log(power @ _MEL.T + 1e-8)
    mfcc = logmel @ _DCT.T
    # Timbre: average log-mel + MFCC mean/std; pitch: autocorrelation.
    mean_lm = np.mean(logmel, axis=0)
    mean_mf = np.mean(mfcc[:, 1:], axis=0)
    std_mf = np.std(mfcc[:, 1:], axis=0)
    f0s = []
    for fr in frames[::max(1, len(frames)//20)]:
        ac = np.correlate(fr, fr, mode='full')[len(fr)-1:]
        lo, hi = 40, 320
        if len(ac) > hi:
            lag = lo + int(np.argmax(ac[lo:hi]))
            if ac[lag] > 0.2 * ac[0]:
                f0s.append(16000.0 / lag)
    f0 = float(np.mean(f0s)) if f0s else 0.0
    vec = np.concatenate([mean_lm, mean_mf, std_mf, [f0 / 400.0]])
    vec = vec / (np.linalg.norm(vec) + 1e-9)
    return vec.astype(np.float32)

def _load_profiles():
    try:
        return json.loads(SPEAKERS.read_text())
    except Exception:
        return []

def _save_profiles(rows):
    SPEAKERS.write_text(json.dumps(rows, ensure_ascii=False, indent=1))

def identify_speaker(path, text):
    if not text:
        return '未知'
    vec = speaker_embedding(path)
    if vec is None:
        return '未知'
    rows = _load_profiles()
    best, best_sim = None, -1.0
    for r in rows:
        v = np.array(r.get('vec', []), dtype=np.float32)
        if v.shape != vec.shape:
            continue
        sim = float(np.dot(v, vec))
        if sim > best_sim:
            best, best_sim = r, sim
    if best is not None and best_sim >= 0.90:
        best['count'] = int(best.get('count', 1)) + 1
        _save_profiles(rows)
        print(f'speaker matched {best["name"]} sim={best_sim:.3f}', flush=True)
        return best['name']
    sid = max([int(r.get('id', 0)) for r in rows] + [0]) + 1
    name = f'说话人{sid}'
    rows.append({'id': sid, 'name': name, 'vec': [float(v) for v in vec], 'count': 1})
    _save_profiles(rows)
    print(f'speaker new {name} (best={best_sim:.3f})', flush=True)
    return name

def transcribe(path):
    with wave.open(str(path), 'rb') as wf:
        if wf.getsampwidth() != 2:
            return ''
        data = wf.readframes(wf.getnframes())
        if wf.getnchannels() == 2:
            data = audioop.tomono(data, 2, 0.5, 0.5)
        if wf.getframerate() != 16000:
            data, _ = audioop.ratecv(data, 2, 1, wf.getframerate(), 16000, None)
    # Normalize quiet board microphone input before ASR.
    peak = audioop.max(data, 2) if data else 0
    if peak > 0:
        gain = min(20.0, (32767 * 0.90) / peak)
        if gain > 1.0:
            data = audioop.mul(data, 2, gain)
    rec = vosk.KaldiRecognizer(MODEL, 16000)
    step = 4000
    for i in range(0, len(data), step):
        rec.AcceptWaveform(data[i:i+step])
    res = json.loads(rec.FinalResult())
    return res.get('text', '').strip()

BOARD_FEEDBACK = 'http://192.168.3.103/feedback'

def board_feedback(stage, detail='', text='', ds='', sv='', wh=''):
    try:
        requests.post(BOARD_FEEDBACK, json={'stage': stage, 'detail': detail,
                                           'text': text, 'ds': ds, 'sv': sv, 'wh': wh},
                      timeout=3)
    except Exception as e:
        print('feedback to board failed:', e, flush=True)

DEEPSEEK_CRED = pathlib.Path.home() / '.dsh' / '.credentials.yaml'

def deepseek_key():
    k = os.environ.get('DEEPSEEK_API_KEY', '').strip()
    if k:
        return k
    try:
        d = yaml.safe_load(DEEPSEEK_CRED.read_text())
        return (d.get('refs') or {}).get('DEEPSEEK_API_KEY', '').strip()
    except Exception:
        return ''

def transcribe_deepseek(path):
    key = deepseek_key()
    if not key:
        raise RuntimeError('no DEEPSEEK_API_KEY')
    with open(path, 'rb') as f:
        r = requests.post('https://api.deepseek.com/v1/tools/asr',
                          headers={'Authorization': 'Bearer ' + key},
                          files={'file': (pathlib.Path(path).name, f, 'audio/wav')},
                          timeout=120)
    if r.status_code != 200:
        raise RuntimeError(f'ASR HTTP {r.status_code}: {r.text[:200]}')
    j = r.json()
    return j.get('text', '').strip(), int(j.get('duration_ms', 0))

FUNASR_PY = '/mnt/documents/dsh-funasr-voice/venv/bin/python'
FUNASR_SCRIPT = ROOT / 'funasr_transcribe.py'

_WHISPER = None
WHISPER_DIR = '/mnt/documents/whisper-models/models--Systran--faster-whisper-small/snapshots/536b0662742c02347bc0e980a01041f333bce120'

def transcribe_whisper(path):
    global _WHISPER
    from faster_whisper import WhisperModel
    if _WHISPER is None:
        _WHISPER = WhisperModel(WHISPER_DIR, device='cpu', compute_type='int8')
    segs, _ = _WHISPER.transcribe(
        str(path), language='zh', vad_filter=False, beam_size=5,
        initial_prompt='以下是普通话的句子，请用简体中文转写。',
        condition_on_previous_text=False)
    return ''.join(s.text for s in segs).strip()

def transcribe_funasr(path):
    """Local SenseVoiceSmall; much better on short Chinese commands than Vosk-small."""
    r = subprocess.run([FUNASR_PY, str(FUNASR_SCRIPT), str(path)],
                       capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        raise RuntimeError(r.stderr[-400:])
    lines = [ln.strip() for ln in r.stdout.splitlines()
             if ln.strip() and not ln.startswith('funasr version')]
    return lines[-1] if lines else ''

def command_is_sane(text):
    if not text or len(text) > 80:
        return False
    # Reject repetitive hallucinations such as "哎 哎 哎 ..." or "啊 啊 ...".
    compact = text.replace(' ', '')
    if len(compact) >= 8:
        from collections import Counter
        c = Counter(compact)
        if c.most_common(1)[0][1] / len(compact) > 0.45:
            return False
    return True

def ask_llm(text):
    prompt = f'''你是一个温和、具体的生活记录助手。用户刚说了一段话，转写可能有错。
转写：{text}

请用中文回答，控制在120字以内：
1. 先用一句话复述你听到的重点（如果有听错，按最合理的理解）；
2. 提取值得记录的事项/情绪/身体状态/待办；
3. 给一个今天就能做的小建议。
不要夸张，不要说教。'''
    try:
        r = requests.post('http://127.0.0.1:11434/api/generate', json={
            'model': 'qwen2.5:7b', 'prompt': prompt, 'stream': False,
            'options': {'temperature': 0.45, 'num_predict': 220}
        }, timeout=150)
        r.raise_for_status()
        return r.json().get('response', '').strip()
    except Exception as e:
        return f'(LLM 暂不可用: {e})'

def latest(n=6):
    if not NOTES.exists():
        return []
    rows = []
    for line in NOTES.read_text(errors='ignore').splitlines()[-n:]:
        try:
            rows.append(json.loads(line))
        except Exception:
            pass
    return rows[::-1]


def _capture_meta(f):
    mp = f.with_suffix('.json')
    try:
        return json.loads(mp.read_text())
    except Exception:
        return {}

def push_note(meta):
    if not meta.get('text'):
        return False
    try:
        r = requests.post(BOARD_NOTE_URL, json={'text': meta.get('text', ''), 'speaker': meta.get('speaker', '后台'), 'time': meta.get('time', '')}, timeout=4)
        return r.status_code == 200
    except Exception as e:
        print('push note failed', e, flush=True)
        return False

def capture_worker():
    while True:
        try:
            for f in sorted(CAPTURE_DIR.glob('speech_*.wav'), key=lambda x: x.stat().st_mtime, reverse=True):
                mp = f.with_suffix('.json')
                try:
                    if mp.exists():
                        meta = _capture_meta(f)
                        if meta.get('text') and not meta.get('pushed'):
                            if push_note(meta):
                                meta['pushed'] = True
                                mp.write_text(json.dumps(meta, ensure_ascii=False))
                                print('pushed note', f.name, flush=True)
                        continue
                    if time.time() - f.stat().st_mtime < 2.0:
                        continue
                    text = transcribe(f)
                    meta = {'text': text, 'speaker': '后台', 'time': time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(f.stat().st_mtime)), 'pushed': False}
                    mp.write_text(json.dumps(meta, ensure_ascii=False))
                    print('capture transcript %s: %s' % (f.name, text), flush=True)
                    if text and push_note(meta):
                        meta['pushed'] = True
                        mp.write_text(json.dumps(meta, ensure_ascii=False))
                        print('pushed note', f.name, flush=True)
                except Exception as e:
                    print('capture worker', e, flush=True)
        except Exception as e:
            print('capture worker outer', e, flush=True)
        time.sleep(5)


def capture_page():
    files = sorted(CAPTURE_DIR.glob('speech_*.wav'), key=lambda x: x.stat().st_mtime, reverse=True)[:40]
    cards = []
    for f in files:
        meta = _capture_meta(f)
        ts = meta.get('time') or time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(f.stat().st_mtime))
        text = meta.get('text') if 'text' in meta else '(等待识别)'
        if not text:
            text = '(未识别到文字)'
        speaker = meta.get('speaker') or '未知'
        cards.append('<div class="card"><div class="meta">%s · %s · %d bytes</div><audio controls preload="none" src="/capture/%s"></audio><div class="label">%s</div><div class="text">%s</div></div>' % (
            html.escape(ts), html.escape(speaker), f.stat().st_size, html.escape(f.name), html.escape(f.name), html.escape(text)))
    body = '<h1>后台 VAD 音频 + 文字</h1><p><a href="/">返回语音记录</a></p>' + (''.join(cards) if cards else '<div class="card">还没有 VAD 语音段。说话后会自动上传。</div>')
    return '<!doctype html><html lang="zh"><head><meta charset="utf-8"><meta http-equiv="refresh" content="10"><meta name="viewport" content="width=device-width,initial-scale=1"><title>后台 VAD 音频 + 文字</title><style>body{font-family:system-ui,sans-serif;background:#f6f3ea;color:#111;margin:0;padding:24px}main{max-width:760px;margin:auto}.card{background:#fff;border:1px solid #d8d2c2;border-radius:14px;padding:16px;margin:12px 0}.meta,.label{color:#777;font-size:13px}.text{font-size:16px;line-height:1.6;margin-top:8px}audio{width:100%;margin:8px 0}a{color:#8a5a20}</style></head><body><main>' + body + '</main></body></html>'


class Handler(BaseHTTPRequestHandler):
    def _send(self, code, body, ctype='text/plain; charset=utf-8'):
        data = body.encode('utf-8') if isinstance(body, str) else body
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Access-Control-Allow-Origin', '*')
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path.startswith('/latest.json'):
            rows = latest(1)
            self._send(200, json.dumps(rows[0] if rows else {}, ensure_ascii=False),
                       'application/json; charset=utf-8')
            return
        if self.path.startswith('/voice_last.wav'):
            if LAST_WAV.exists():
                self._send(200, LAST_WAV.read_bytes(), 'audio/wav')
            else:
                self._send(404, 'no wav')
            return
        if self.path.startswith('/captures'):
            self._send(200, capture_page(), 'text/html; charset=utf-8')
            return
        if self.path.startswith('/capture/'):
            name = os.path.basename(self.path[len('/capture/'):])
            f = CAPTURE_DIR / name
            if f.exists() and f.suffix == '.wav':
                self._send(200, f.read_bytes(), 'audio/wav')
            else:
                self._send(404, 'no audio')
            return
        rows = latest()
        cards = []
        for r in rows:
            cards.append(f'''<div class="card"><div class="meta">{html.escape(r.get("time",""))} · {html.escape(r.get("speaker","未知"))}</div>
<div class="label">转写</div><div class="text">{html.escape(r.get("transcript",""))}</div>
<div class="label">Qwen 记录 / 建议</div><div class="text">{html.escape(r.get("reply",""))}</div>
<div class="label">音频</div><div class="text"><a href="/voice_last.wav">voice_last.wav</a></div></div>''')
        body = f'''<!doctype html><html lang="zh"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>语音记录 / 人生指导</title>
<style>
body{{font-family:system-ui,-apple-system,"Noto Sans CJK SC",sans-serif;background:#f6f3ea;color:#111;margin:0;padding:24px}}
main{{max-width:760px;margin:auto}}h1{{font-size:26px;margin:0 0 6px}}
.sub{{color:#666;margin-bottom:24px}}.card{{background:#fff;border:1px solid #d8d2c2;border-radius:14px;padding:18px;margin:0 0 16px;box-shadow:0 2px 10px #0001}}
.meta{{color:#888;font-size:13px}}.label{{font-size:12px;letter-spacing:.12em;color:#8a6d3b;margin-top:14px}}
.text{{font-size:17px;line-height:1.65;white-space:pre-wrap}}.empty{{color:#777}}
</style></head><body><main><h1>语音记录 / 人生指导</h1>
<div class="sub">ESP32 录音 → faster-whisper 转写 → 本机 qwen2.5:7b。数据不出局域网。</div>
{''.join(cards) if cards else '<div class="card empty">还没有记录。在 ESP32 目录选择“语音记录”，按 A 录 6 秒。</div>'}
</main></body></html>'''
        self._send(200, body, 'text/html; charset=utf-8')

    def do_POST(self):
        if self.path.startswith('/voicecmd'):
            try:
                n = int(self.headers.get('Content-Length', '0'))
                data = self.rfile.read(n)
                if len(data) < 1000:
                    self._send(400, 'too short')
                    return
                CMD_WAV.write_bytes(data)
                board_feedback('已接收', '电脑已收到录音，开始清洗和转写。')
                clean_meta = {}
                try:
                    clean_meta = preprocess(CMD_WAV, CMD_CLEAN, denoise=os.environ.get('VOICE_DENOISE','0')=='1', highpass=False)
                    asr_wav = CMD_CLEAN
                except Exception as e:
                    print('preprocess failed:', e, flush=True)
                    asr_wav = CMD_WAV
                ds_text, sv_text, wh_text, ds_ms = '', '', '', 0
                def run_sv():
                    return transcribe_funasr(asr_wav)
                def run_wh():
                    return transcribe_whisper(asr_wav)
                # DeepSeek ASR temporarily disabled: use SenseVoice + Whisper mutual evidence.
                with ThreadPoolExecutor(max_workers=2) as ex:
                    fsv = ex.submit(run_sv)
                    fwh = ex.submit(run_wh)
                    try:
                        sv_text = fsv.result()
                    except Exception as e:
                        print('sensevoice failed:', e, flush=True)
                    try:
                        wh_text = fwh.result()
                    except Exception as e:
                        print('whisper failed:', e, flush=True)
                board_feedback('已识别', 'SenseVoice + Whisper 双路 ASR 完成，准备送 DSH。',
                               text=(sv_text or wh_text), ds='', sv=sv_text, wh=wh_text)
                if not command_is_sane(sv_text):
                    sv_text = ''
                if not command_is_sane(wh_text):
                    wh_text = ''
                if not sv_text and not wh_text:
                    try:
                        import wave
                        with wave.open(str(CMD_CLEAN if CMD_CLEAN.exists() else CMD_WAV), 'rb') as wf:
                            dur = wf.getnframes() / wf.getframerate()
                    except Exception:
                        dur = 0
                    board_feedback('未识别', f'录音 {dur:.1f} 秒，但没有识别到有效人声；请靠近麦克风立即说短句。')
                fused_prompt = ''
                if sv_text or wh_text:
                    fused_prompt = (
                        '[语音命令｜SenseVoice + Whisper 双转写，请直接执行]\n'
                        f'SenseVoice：{sv_text or "(空)"}\n'
                        f'Whisper small：{wh_text or "(空)"}\n'
                        '请把两版互相引证，取一致且具体的命令并直接执行，不要只复述。'
                        '如果两版在关键动作/对象/文件上冲突，优先采用更符合当前上下文的一版并简短说明；'
                        '仍无法判断，或命令高风险（删除、重启、OTA/刷机）时先向用户确认。'
                        '执行过程中用 python3 /mnt/documents/桌面/web游戏/esp32-rlcd/esp_feedback.py 更新 ESP 反馈页，'
                        '完成后把阶段设为“完成”。'
                    )
                row = {'time': time.strftime('%Y-%m-%d %H:%M:%S'),
                       'deepseek': ds_text, 'sensevoice': sv_text,
                       'whisper': wh_text, 'deepseek_ms': ds_ms,
                       'audio': CMD_WAV.name, 'clean': clean_meta}
                with CMD_LOG.open('a', encoding='utf-8') as f:
                    f.write(json.dumps(row, ensure_ascii=False) + '\n')
                bridge = ROOT / 'dsh_voice_bridge.py'
                proc = 'not-started'
                compare_only = os.environ.get('VOICE_COMPARE_ONLY', '1') == '1'
                if compare_only and fused_prompt:
                    board_feedback('对比完成', '三路 ASR 已完成；当前为对比模式，不执行命令。',
                                   text=(ds_text or sv_text or wh_text),
                                   ds=ds_text, sv=sv_text, wh=wh_text)
                    proc = 'compare-only'
                elif fused_prompt and bridge.exists():
                    import subprocess
                    board_feedback('已送 DSH', '命令已进入当前会话队列，等待执行。',
                                   text=(ds_text or sv_text or wh_text), ds=ds_text, sv=sv_text, wh=wh_text)
                    subprocess.Popen(['/usr/bin/python3', str(bridge), fused_prompt],
                                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                    proc = 'queued'
                self._send(200, json.dumps({'ok': True, 'deepseek': ds_text, 'sensevoice': sv_text,
                                             'whisper': wh_text, 'deepseek_ms': ds_ms,
                                             'clean': clean_meta, 'bridge': proc}, ensure_ascii=False),
                           'application/json; charset=utf-8')
            except Exception as e:
                self._send(500, json.dumps({'ok': False, 'error': str(e)}, ensure_ascii=False),
                           'application/json; charset=utf-8')
            return
        if not self.path.startswith('/voice'):
            self._send(404, 'not found')
            return
        try:
            n = int(self.headers.get('Content-Length', '0'))
            data = self.rfile.read(n)
            if len(data) < 1000:
                self._send(400, 'too short')
                return
            LAST_WAV.write_bytes(data)
            t0 = time.time()
            text = transcribe(LAST_WAV)
            print(f'transcript ({time.time()-t0:.1f}s): {text}', flush=True)
            speaker = identify_speaker(LAST_WAV, text)
            stamp = time.strftime('%Y%m%d_%H%M%S')
            saved = AUDIO_DIR / f'{stamp}_{speaker}.wav'
            saved.write_bytes(data)
            reply = ask_llm(text) if text else '(没有听清，请靠近麦克风再说一次)'
            row = {'time': time.strftime('%Y-%m-%d %H:%M:%S'), 'speaker': speaker, 'transcript': text, 'reply': reply, 'audio': saved.name}
            with NOTES.open('a', encoding='utf-8') as f:
                f.write(json.dumps(row, ensure_ascii=False) + '\n')
            self._send(200, json.dumps({'ok': True, **row}, ensure_ascii=False),
                       'application/json; charset=utf-8')
        except Exception as e:
            print('POST error', e, flush=True)
            self._send(500, json.dumps({'ok': False, 'error': str(e)}, ensure_ascii=False),
                       'application/json; charset=utf-8')

    def log_message(self, fmt, *args):
        print('%s - %s' % (self.address_string(), fmt % args), flush=True)

if __name__ == '__main__':
    threading.Thread(target=capture_worker, daemon=True).start()
    print(f'voice server listening on http://0.0.0.0:{PORT}', flush=True)
    ThreadingHTTPServer(('0.0.0.0', PORT), Handler).serve_forever()
