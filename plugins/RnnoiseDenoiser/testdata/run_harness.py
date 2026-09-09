#!/usr/bin/env python3
"""Run the RNNoise library harness at both input scales and measure."""
import struct, subprocess, csv
import numpy as np

TD = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-rnnoise/plugins/RnnoiseDenoiser/testdata"
SR = 48000

# --- int16 test WAV -> raw float32 mono at +/-1.0 ---
raw = open(f"{TD}/speechlike_noise_2s_48k.wav", "rb").read()
pos = 12
while pos + 8 <= len(raw):
    cid, size = struct.unpack_from("<4sI", raw, pos)
    if cid == b"data":
        data = raw[pos + 8:pos + 8 + size]
    pos += 8 + size + (size & 1)
sig = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0
sig.astype("<f4").tofile("/tmp/harness_in.f32")

print("### library-level harness runs (vendored rnnoise, same sources as the plugin)")
for scale, tag in [(1.0, "s1"), (32768.0, "s32768")]:
    r = subprocess.run(["/tmp/rnn_harness", "/tmp/harness_in.f32", f"/tmp/harness_out_{tag}.f32",
                        str(scale), f"/tmp/harness_stats_{tag}.csv"],
                       capture_output=True, text=True)
    print(f"$ /tmp/rnn_harness harness_in.f32 harness_out_{tag}.f32 {scale:g} harness_stats_{tag}.csv")
    print(f"  {r.stdout.strip()}  exit={r.returncode}")

def load_stats(p):
    rows = list(csv.DictReader(open(p)))
    return (np.array([float(r["in_rms"]) for r in rows]),
            np.array([float(r["out_rms"]) for r in rows]),
            np.array([float(r["vad_prob"]) for r in rows]))

def db(v):
    return 20 * np.log10(v) if v > 0 else float("-inf")

print()
print("### per-frame stats (frame = 10 ms; passages: speech 0.10-0.70s = frames 10-70, "
      "noise-only 0.85-1.15s = frames 85-115)")
for tag, scale in [("s1", 1.0), ("s32768", 32768.0)]:
    ein, eout, vad = load_stats(f"/tmp/harness_stats_{tag}.csv")
    print(f"-- scale x{scale:g} ({tag}) --")
    for name, lo, hi in [("speech", 10, 70), ("noise-only", 85, 115), ("silence after 2.0s", 200, 380)]:
        i = ein[lo:hi]; o = eout[lo:hi]; v = vad[lo:hi]
        rin = np.sqrt(np.mean(i ** 2)); rout = np.sqrt(np.mean(o ** 2))
        print(f"   {name:20s} in_rms={db(rin):8.2f} dB out_rms={db(rout):8.2f} dB  "
              f"change={db(rout/rin) if rin>0 and rout>0 else float('nan'):+7.2f} dB  "
              f"mean_vad={v.mean():.3f}  frames_vad>0.5={int(np.sum(v>0.5))}/{len(v)}")

print()
print("### time-domain check: is the scale-1.0 output a delayed copy of the input?")
x = sig
y = np.fromfile("/tmp/harness_out_s1.f32", dtype="<f4").astype(np.float64)
best = (0, -1)
for lag in range(0, 2000):
    a, b = y[lag:], x[:len(x) - lag]
    n = min(len(a), len(b)); a, b = a[:n], b[:n]
    den = np.sqrt(np.sum(a ** 2) * np.sum(b ** 2))
    r = float(np.sum(a * b) / den) if den > 0 else 0
    if r > best[1]: best = (lag, r)
print(f"   best lag = {best[0]} frames, corr = {best[1]:+.5f}  (480 frames = 1 RNNoise frame = 10 ms)")
