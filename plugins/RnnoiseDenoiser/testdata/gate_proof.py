#!/usr/bin/env python3
"""Replicate RNNoise's frame_analysis + compute_band_energy + silence gate
(denoise.c:332-345, 90-113, 389) on the actual test signal at both scales.

This shows numerically why the plugin is a no-op at LMMS's +/-1.0 signal scale
and active at RNNoise's native +/-32768 scale.
"""
import re, struct
import numpy as np

RNN = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-rnnoise/plugins/RnnoiseDenoiser/rnnoise"
TD = "/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-rnnoise/plugins/RnnoiseDenoiser/testdata"

# ---- parse rnn_half_window[] from rnnoise_tables.c ----
txt = open(f"{RNN}/rnnoise_tables.c").read()
m = re.search(r"const float rnn_half_window\[\] = \{(.*?)\};", txt, re.S)
W = np.array([float(x) for x in re.findall(r"[-+0-9.eE]+", m.group(1))])
assert len(W) == 480, len(W)

# ---- parse eband20ms[] from denoise.c (strip C comments first) ----
txt = re.sub(r"/\*.*?\*/", "", open(f"{RNN}/denoise.c").read(), flags=re.S)
m = re.search(r"const int eband20ms\[NB_BANDS\+2\] = \{(.*?)\};", txt, re.S)
EB = np.array([int(x) for x in re.findall(r"\d+", m.group(1))])
NB = len(EB) - 2
assert NB == 32, NB


def frame_analysis_energy(inp):
    """inp: 480 raw samples (the plugin feeds m_inputBuf directly).
    Returns E = sum of band energies, exactly as denoise.c computes it."""
    x = np.zeros(960)
    x[480:] = inp                      # analysis_mem is 0 for the first frame; we reset per frame
    for i in range(480):               # apply_window (denoise.c:219-225)
        x[i] *= W[i]
        x[959 - i] *= W[i]
    X = np.fft.rfft(x)                 # forward_transform -> FREQ_SIZE=481 bins
    s = np.zeros(NB + 2)
    for i in range(NB + 1):            # compute_band_energy (denoise.c:90-113)
        size = EB[i + 1] - EB[i]
        for j in range(size):
            frac = j / size
            tmp = X[EB[i] + j].real ** 2 + X[EB[i] + j].imag ** 2
            s[i] += (1 - frac) * tmp
            s[i + 1] += frac * tmp
    s[1] = (s[0] + s[1]) * 2 / 3
    s[NB] = (s[NB] + s[NB + 1]) * 2 / 3
    Ex = s[1:NB + 1]
    return float(np.sum(Ex)), Ex


# ---- load the test signal (16-bit PCM, +/-1.0) ----
raw = open(f"{TD}/speechlike_noise_2s_48k.wav", "rb").read()
pos = 12
while pos + 8 <= len(raw):
    cid, size = struct.unpack_from("<4sI", raw, pos)
    if cid == b"data":
        data = raw[pos + 8:pos + 8 + size]
    pos += 8 + size + (size & 1)
sig = np.frombuffer(data, dtype="<i2").astype(np.float64) / 32768.0

print("RNNoise silence gate: denoise.c:389  'if (!TRAINING && E < 0.04)'")
print("plugin feeds m_inputBuf values straight in, so E scales with the signal squared")
print()
print(f"{'passage':>22s} {'scale':>10s} {'E = sum(Ex)':>16s} {'gate':>8s}")
for name, lo, hi in [("speech 0.10-0.70s", 0.10, 0.70), ("noise-only 0.85-1.15s", 0.85, 1.15)]:
    s, e = int(lo * 48000), int(hi * 48000)
    seg = sig[s:e]
    for scale, label in [(1.0, "x1 (LMMS)"), (32768.0, "x32768 (native)")]:
        Es = [frame_analysis_energy(seg[i * 480:(i + 1) * 480] * scale)[0]
              for i in range(len(seg) // 480)]
        E = float(np.mean(Es))
        gate = "FIRES" if E < 0.04 else "open"
        print(f"{name:>22s} {label:>10s} {E:>16.6g} {gate:>8s}")
    print()
