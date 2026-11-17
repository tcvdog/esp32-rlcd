#!/usr/bin/env python3
"""Mono 16k -> high-pass -> mild spectral denoise -> silence trim -> normalize."""
import sys, wave, audioop
import numpy as np

def _read(path):
    with wave.open(str(path), 'rb') as w:
        sr, ch, sw = w.getframerate(), w.getnchannels(), w.getsampwidth()
        data = w.readframes(w.getnframes())
    if sw != 2:
        raise ValueError('need 16-bit PCM')
    if ch == 2:
        data = audioop.tomono(data, 2, 0.5, 0.5)
    if sr != 16000:
        data, _ = audioop.ratecv(data, 2, 1, sr, 16000, None)
        sr = 16000
    return np.frombuffer(data, dtype='<i2').astype(np.float32), sr

def _highpass(x, sr=16000, cutoff=100.0):
    a = np.exp(-2.0 * np.pi * cutoff / sr)
    y = np.empty_like(x)
    prev_x = 0.0; prev_y = 0.0
    for i in range(len(x)):
        y[i] = a * (prev_y + x[i] - prev_x)
        prev_x = x[i]; prev_y = y[i]
    return y

def _denoise(x, sr=16000):
    frame, hop = 512, 256
    if len(x) < frame * 4:
        return x
    win = np.hanning(frame).astype(np.float32)
    n = 1 + (len(x) - frame) // hop
    idx = np.arange(frame)[None, :] + hop * np.arange(n)[:, None]
    frames = x[idx] * win[None, :]
    spec = np.fft.rfft(frames, axis=1)
    mag = np.abs(spec)
    energy = np.mean(mag, axis=1)
    quiet = energy <= np.percentile(energy, 20)
    noise = np.median(mag[quiet], axis=0) if np.any(quiet) else np.median(mag, axis=0)
    gain = 1.0 - 1.15 * (noise[None, :] / (mag + 1e-6))
    gain = np.clip(gain, 0.25, 1.0)
    # Smooth gain across frequency to avoid musical noise.
    if gain.shape[1] >= 3:
        g = gain.copy()
        g[:, 1:-1] = (gain[:, :-2] + gain[:, 1:-1] + gain[:, 2:]) / 3.0
        gain = g
    rec = np.fft.irfft(spec * gain, n=frame, axis=1) * win[None, :]
    out = np.zeros(len(x), dtype=np.float32)
    norm = np.zeros(len(x), dtype=np.float32)
    for i in range(n):
        out[i*hop:i*hop+frame] += rec[i]
        norm[i*hop:i*hop+frame] += win * win
    out /= np.maximum(norm, 1e-6)
    return out

def _trim(x, sr=16000, pad_s=0.18):
    frame = 320
    n = len(x) // frame
    if n < 3:
        return x
    rms = np.sqrt(np.mean(x[:n*frame].reshape(n, frame) ** 2, axis=1) + 1e-9)
    peak = float(np.max(np.abs(x)) + 1e-9)
    floor = float(np.percentile(rms, 20))
    thr = max(80.0, peak * 0.02, floor * 2.2)
    active = np.where(rms > thr)[0]
    if len(active) < 3:
        return x
    pad = int(pad_s * sr)
    a = max(0, active[0] * frame - pad)
    b = min(len(x), (active[-1] + 1) * frame + pad)
    return x[a:b]

def preprocess(src, dst, denoise=False, highpass=False):
    x, sr = _read(src)
    if highpass:
        x = _highpass(x, sr)
    if denoise:
        x = _denoise(x, sr)
    x = _trim(x, sr)
    x = x - float(np.mean(x))
    p = float(np.max(np.abs(x)) + 1e-9)
    x = np.clip(x * (0.90 * 32767.0 / p), -32768, 32767).astype('<i2')
    with wave.open(str(dst), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(x.tobytes())
    return {'in_s': len(x)/sr, 'out_s': len(x)/sr, 'peak': int(p), 'denoise': denoise, 'highpass': highpass}

if __name__ == '__main__':
    print(preprocess(sys.argv[1], sys.argv[2]))
