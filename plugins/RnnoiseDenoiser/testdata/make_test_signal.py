#!/usr/bin/env python3
"""Generate synthetic speech-like + noise test signals for the RNNoise denoiser
runtime test (AI-KOS task #559).

Output (16-bit PCM WAV, 48 kHz mono):
  speechlike_noise_2s_48k.wav       96000 frames (2.000 s = 200 x 480) - main A/B signal
  speechlike_noise_2s_plus100.wav   96100 frames (2.000 s + 100 = 200.2 x 480) - tail edge case

Signal layout (main file):
  0.00 - 0.80 s : speech-like harmonic stack, f0=120 Hz, 4 Hz syllabic AM, peak 0.32
  0.80 - 1.20 s : white noise only, RMS = 0.0056 (-45 dBFS)  <- noise-only passage
  1.20 - 2.00 s : speech-like harmonic stack, f0=150 Hz, 4 Hz syllabic AM, peak 0.32

The edge file is the main file + 100 extra frames of the f0=120 Hz tone, so the
final RNNoise frame (480 samples) is partial and must be handled without crashing.
"""
import wave
import numpy as np

SR = 48000
OUT_DIR = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-rnnoise/plugins/RnnoiseDenoiser/testdata"


def speech_like(dur, f0, rng, amp=0.32):
    t = np.arange(int(round(dur * SR))) / SR
    sig = np.zeros_like(t)
    # harmonic stack with formant-ish rolloff (keeps energy under ~3 kHz like speech)
    for k, w in [(1, 1.0), (2, 0.6), (3, 0.45), (4, 0.30), (5, 0.20), (6, 0.12), (8, 0.08)]:
        sig += w * np.sin(2 * np.pi * f0 * k * t + rng.uniform(0, 2 * np.pi))
    sig /= np.max(np.abs(sig))
    env = 0.55 + 0.45 * np.sin(2 * np.pi * 4.0 * t)   # syllabic-rate AM, never fully silent
    return amp * sig * env


def noise_only(dur, rms, rng):
    n = int(round(dur * SR))
    w = rng.standard_normal(n)
    w = w / np.sqrt(np.mean(w ** 2)) * rms
    return w


def write_wav(path, x):
    pcm = np.clip(x, -1.0, 1.0)
    pcm16 = (pcm * 32767.0).round().astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm16.tobytes())
    print(f"wrote {path}: {len(x)} frames ({len(x)/SR:.4f} s), "
          f"peak={np.max(np.abs(x)):.4f}, rms={np.sqrt(np.mean(x**2)):.6f}")


def main():
    rng = np.random.default_rng(559)  # deterministic
    seg_a = speech_like(0.8, 120.0, rng)
    seg_b = noise_only(0.4, 0.0056, rng)
    seg_c = speech_like(0.8, 150.0, rng)
    main_sig = np.concatenate([seg_a, seg_b, seg_c])
    assert len(main_sig) == 96000, len(main_sig)

    write_wav(f"{OUT_DIR}/speechlike_noise_2s_48k.wav", main_sig)

    tail = speech_like(100 / SR, 120.0, rng)[:100]
    edge = np.concatenate([main_sig, tail])
    assert len(edge) == 96100, len(edge)
    write_wav(f"{OUT_DIR}/speechlike_noise_2s_plus100.wav", edge)


if __name__ == "__main__":
    main()
