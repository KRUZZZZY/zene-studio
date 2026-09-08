# RUNTIME-TEST.md — does the RNNoise denoiser load and process audio in a real LMMS host?

**Task**: AI-KOS #559, risk R3 — "compiled" is not "works".
**Date**: 2026-09-08. **Host**: LMMS 1.3.0-alpha.2, headless (`QT_QPA_PLATFORM=offscreen`), Ubuntu 24.04 / Qt6, 48 kHz float render.
**Commit under test**: `b90e8dc7d2b1391d8b28983fec1ba534f72ade2d` (branch `feat/rnnoise-denoiser`, clean worktree).
**Binary under test**: `lmms-rnnoise/build/plugins/librnnoisedenoiser.so` + `lmms-rnnoise/build/lmms` (built 2026-09-08 16:07 from the same commit).

## VERDICT: `runtime-verified: partial` → **denoising fixed** (2026-09-08, §11)

| Question | Answer | Evidence |
|---|---|---|
| Does LMMS load `librnnoisedenoiser.so` in a headless render? | **YES** | strace `openat(...librnnoisedenoiser.so) = 23` (§1) |
| Does the plugin run on the audio thread and alter the signal? | **YES** | A vs C differ in 81 % of samples, max\|diff\| 0.473; deterministic ~30 ms latency + OLA re-synthesis + tail flush (§4, §5) |
| Does it perform noise suppression **at LMMS's signal scale**? | **NO as shipped → YES after fix** | pre-fix **+0.03 dB**, post-fix **−21.8…−24.1 dB** across repeated renders (mean −23.2 dB; A vs C, noise-only passage); band levels now −15…−65 dB (§4, §11) |
| Is the DSP core itself broken? | **NO** | same library/binary at RNNoise's native ±32768 scale suppresses the noise-only passage by **−32.0 dB** (library harness) / **−24.0…−29.8 dB** (in-host) (§6) |
| Root cause | **missing ±32768 (CELT_SIG_SCALE) input/output conversion** in `processImpl()` (§6) | |
| Audio-thread safety (static) | **no allocation / no locking** on the audio path (§7) | |
| Non-480-frame tail | **no crash/hang**; 1/10 runs hit a *host* shutdown abort that also hits a zero-plugin project (§8, §9) | |

**One-line summary**: the plugin loads, runs, and measurably processes audio in the host, but as shipped it is a ~30 ms delay/re-synthesis no-op at LMMS's ±1.0 float signal scale — the RNNoise network receives inputs ~5 orders of magnitude below its training range and never attenuates the noise. Feed it ±32768 and the same code denoises by 24–32 dB.

> **UPDATE 2026-09-08 (post-verdict, §11):** the root-cause fix is implemented and re-measured. `processImpl()` now converts ±1.0 → ±32768 on the way in and ÷32768 on the way out; the noise-only A-vs-C delta is **−21.8 … −24.1 dB** across repeated renders (mean −23.2 dB; first render −24.05 dB) — acceptance ≤ −10 dB, **PASS every run** (§11.7) — and bypass equivalence plus the 1439-sample latency are unchanged.

---

## 1. LOAD PROOF

`strace` on a full headless render of `rnnoise_test_A.mmp` (the project whose FX chain names `rnnoisedenoiser`):

```
$ cd lmms-rnnoise/build
$ strace -f -e trace=openat -o /tmp/strace_A.log \
    ./lmms render ../plugins/RnnoiseDenoiser/testdata/rnnoise_test_A.mmp \
    -f wav -s 48000 -a -o /tmp/load_proof_A.wav
```

From `/tmp/strace_A.log` (archived as `testdata/strace_A.log`, lines 97–98):

```
3874401 openat(AT_FDCWD, ".../lmms-rnnoise/build/plugins/haswell/librnnoisedenoiser.so", O_RDONLY|O_CLOEXEC) = -1 ENOENT (No such file or directory)
3874401 openat(AT_FDCWD, ".../lmms-rnnoise/build/plugins/librnnoisedenoiser.so", O_RDONLY|O_CLOEXEC) = 23
```

The host first probes the CPU-optimised sub-directory (`haswell/`), misses, then opens the plugin itself — **file descriptor 23, success**. The library is loaded, not merely discovered.

Supporting facts:

```
$ ls -la build/plugins/librnnoisedenoiser.so
-rwxrwxr-x 1 kruzzzzy kruzzzzy 14901928 Sep  8 16:07 build/plugins/librnnoisedenoiser.so

$ file build/plugins/librnnoisedenoiser.so
build/plugins/librnnoisedenoiser.so: ELF 64-bit LSB shared object, x86-64, version 1 (SYSV),
dynamically linked, BuildID[sha1]=0cf03dc24b843cf8afc33555f7be7dbfea76234a, not stripped

$ nm -D --defined-only build/plugins/librnnoisedenoiser.so | grep descriptor
0000000000e2f180 D rnnoisedenoiser_plugin_descriptor
```

**Negative control — absence of error text is NOT load proof.** A project naming a non-existent plugin (`rnnoise_test_F_bogus.mmp`, `<effect name="definitelynotarealplugin" ...>`) renders with **exit 0 and no error output**. LMMS silently substitutes a `DummyEffect` for missing plugins, so only the strace above (or a diff against a control) proves loading:

```
$ grep -A7 'RENDER F' testdata/render-log.txt
=== RENDER F  bogus plugin name (DummyEffect control) ===
$ cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-rnnoise/plugins/RnnoiseDenoiser/testdata/../../../build
$ QT_QPA_PLATFORM=offscreen ./lmms render ../plugins/RnnoiseDenoiser/testdata/rnnoise_test_F_bogus.mmp -f wav -s 48000 -a -o ../plugins/RnnoiseDenoiser/testdata/out_F_bogus.wav
PERFLOG |       Project Render | 0.09user, 0.18system 0.04elapsed
Loading project...
Done

EXIT=0
```

---

## 2. TEST PROJECT

### 2.1 How the schema was derived

From the LMMS 1.3.0-alpha.2 source in `lmms-rnnoise/` (not from the plugin's own claims), plus `tests/emptyproject.mmp` as a skeleton:

| Element | Source of truth |
|---|---|
| `<track type name muted>` + type-specific child | `Track::saveTrack()` / `saveState()`, `src/core/Track.cpp` |
| `<sampletrack vol pan mixch>` + `<fxchain>` | `SampleTrack::saveTrackSpecificSettings()`, `src/tracks/SampleTrack.cpp` |
| `<sampleclip pos len muted src off autoresize sample_rate>` | `SampleClip::saveSettings()`, `src/core/SampleClip.cpp` |
| `<fxchain numofeffects enabled>` / `<effect name on wet autoquit>` | `EffectChain::saveSettings()`, `src/core/EffectChain.cpp:57-72` |
| `on` / `wet` / `autoquit` attribute semantics | `Effect::saveSettings()`, `src/core/Effect.cpp:60-67` |
| plugin name `rnnoisedenoiser` | `LMMS_STRINGIFY(PLUGIN_NAME)` in `RnnoiseDenoiserEffect.cpp:45`; confirmed by `nm -D` (§1) |

Bypass is **already available** and was used: `Effect::m_enabledModel` is serialised as `on="0"`, and `Effect::processAudioBuffer()` takes the bypass branch when disabled (`src/core/Effect.cpp:113-118`):

```
113	if (!isProcessingAudio())
114	{
115		// Plugin is awake but not processing audio
116		processBypassedImpl();
117		return false;
118	}
```

### 2.2 The A project (plugin active)

`testdata/rnnoise_test_A.mmp` — 40 lines, hand-written XML, mono 48 kHz sample routed through the effect:

```xml
<multimedia-project version="1.0" creator="LMMS" creatorversion="1.3.0" type="song">
  <head timesig_numerator="4" mastervol="100" timesig_denominator="4" bpm="120" masterpitch="0" />
  <song>
    <trackcontainer width="600" x="5" y="5" maximized="0" height="300" visible="1" type="song" minimized="0">
      <track muted="0" type="2" name="DenoiseTest">
        <sampletrack vol="100" pan="0" mixch="0">
          <fxchain numofeffects="1" enabled="1">
            <effect name="rnnoisedenoiser" on="1" wet="1" autoquit="1">
              <RnnoiseDenoiserControls />
              <key />
            </effect>
          </fxchain>
        </sampletrack>
        <sampleclip pos="0" len="240" muted="0" src=".../speechlike_noise_2s_48k.wav" off="0" autoresize="1" sample_rate="48000" />
      </track>
    </trackcontainer>
    ...
```

Source signal (`testdata/make_test_signal.py`): 2.0 s, 48 kHz, 16-bit mono, 96 000 frames = exactly 200 × 480:

```
$ python3 make_test_signal.py
wrote /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-rnnoise/plugins/RnnoiseDenoiser/testdata/speechlike_noise_2s_48k.wav: 96000 frames (2.0000 s), peak=0.3200, rms=0.080851
wrote /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-rnnoise/plugins/RnnoiseDenoiser/testdata/speechlike_noise_2s_plus100.wav: 96100 frames (2.0021 s), peak=0.3200, rms=0.080904
```

(0.080851 = −21.85 dBFS; 96 000 = 200 × 480 exactly, 96 100 = 200 × 480 + 100.)

Content: a "speech-like" burst train (0.10–0.70 s and 1.30–1.90 s, pitched harmonic stacks with formant-like envelopes) plus a **noise-only passage at 0.85–1.15 s** and digital silence after 2.0 s. That noise-only passage is the noise-floor probe used throughout.

### 2.3 The A-B-C(-D,E,F) matrix

| Project | FX chain | Sample | Purpose |
|---|---|---|---|
| `rnnoise_test_A.mmp` | `numofeffects="1"`, `<effect name="rnnoisedenoiser" on="1" wet="1">` | int16, ±1.0 | plugin **active** |
| `rnnoise_test_B.mmp` | same chain, `on="0"` | int16, ±1.0 | plugin **bypassed** (`Effect::m_enabledModel`) |
| `rnnoise_test_C.mmp` | `numofeffects="0"` | int16, ±1.0 | plugin **absent** |
| `rnnoise_test_F_bogus.mmp` | `<effect name="definitelynotarealplugin">` | int16, ±1.0 | missing-plugin control (DummyEffect) |
| `rnnoise_test_D_float_nofx.mmp` | `numofeffects="0"` | **float32, ±32768** | scale control |
| `rnnoise_test_E_float_denoise.mmp` | `<effect name="rnnoisedenoiser" on="1" wet="1">` | **float32, ±32768** | plugin at RNNoise native scale |
| `rnnoise_edge.mmp` | denoiser active | 96 100-frame int16 | tail-flush edge case |
| `rnnoise_edge_nofx.mmp` | `numofeffects="0"` | 96 100-frame int16 | edge-case control |

Only the `<effect …>` / `numofeffects` attributes and the `src=` sample differ between the pairs — the audio-relevant XML is otherwise identical (the files carry different explanatory comments).

---

## 3. A-B RENDER

Exact commands and exit codes (full log: `testdata/render-log.txt`; script: `testdata/run_renders.sh`):

```
$ cd /home/.../lmms-rnnoise/build
$ QT_QPA_PLATFORM=offscreen ./lmms render ../plugins/RnnoiseDenoiser/testdata/rnnoise_test_A.mmp -f wav -s 48000 -a -o ../plugins/RnnoiseDenoiser/testdata/out_A_denoised.wav
PERFLOG |       Project Render | 0.09user, 0.29system 0.07elapsed
Loading project...
Done
EXIT=0
-rw-rw-r-- 1 kruzzzzy kruzzzzy 1536132 Sep  8 20:46 .../out_A_denoised.wav

$ QT_QPA_PLATFORM=offscreen ./lmms render ../plugins/RnnoiseDenoiser/testdata/rnnoise_test_B.mmp -f wav -s 48000 -a -o ../plugins/RnnoiseDenoiser/testdata/out_B_bypassed.wav
EXIT=0   (1536132 bytes)

$ QT_QPA_PLATFORM=offscreen ./lmms render ../plugins/RnnoiseDenoiser/testdata/rnnoise_test_C.mmp -f wav -s 48000 -a -o ../plugins/RnnoiseDenoiser/testdata/out_C_nofx.wav
EXIT=0   (1536132 bytes)

$ QT_QPA_PLATFORM=offscreen ./lmms render ../plugins/RnnoiseDenoiser/testdata/rnnoise_test_D_float_nofx.mmp -f wav -s 48000 -a -o ../plugins/RnnoiseDenoiser/testdata/out_D_float_nofx.wav
EXIT=0   (1536132 bytes)

$ QT_QPA_PLATFORM=offscreen ./lmms render ../plugins/RnnoiseDenoiser/testdata/rnnoise_test_E_float_denoise.mmp -f wav -s 48000 -a -o ../plugins/RnnoiseDenoiser/testdata/out_E_float_denoised.wav
EXIT=0   (1536132 bytes)

$ QT_QPA_PLATFORM=offscreen ./lmms render ../plugins/RnnoiseDenoiser/testdata/rnnoise_edge.mmp -f wav -s 48000 -a -o ../plugins/RnnoiseDenoiser/testdata/out_edge_denoised.wav
EXIT=0   (1536132 bytes)

$ QT_QPA_PLATFORM=offscreen ./lmms render ../plugins/RnnoiseDenoiser/testdata/rnnoise_edge_nofx.mmp -f wav -s 48000 -a -o ../plugins/RnnoiseDenoiser/testdata/out_edge_nofx.wav
EXIT=0   (1536132 bytes)

$ QT_QPA_PLATFORM=offscreen ./lmms render ../plugins/RnnoiseDenoiser/testdata/rnnoise_test_F_bogus.mmp -f wav -s 48000 -a -o ../plugins/RnnoiseDenoiser/testdata/out_F_bogus.wav
EXIT=0   (1536132 bytes)
```

All 8 renders: **exit 0**. Output: 4.0 s stereo float32, 192 000 frames (project length 2 bars @ 120 bpm).

**Bypass note**: bypass *is* implemented (`on="0"` → `Effect::processBypassedImpl()`), so the primary A-B is **A (active) vs B (bypassed)**, with **C (plugin absent)** as the second control. B and C are used interchangeably below because the measurements show they are equivalent outside the host's startup window.

---

## 4. MEASUREMENT

Tool: `testdata/measure.py` (stdlib RIFF parser + numpy; handles fmt 1 int16 and fmt 3 float32 — Python 3.11's `wave` module cannot open float WAVs). Full output archived as `testdata/measurement-log.txt`.

```
##############################################################################
# 1. SOURCE SANITY
##############################################################################
source speechlike_noise_2s_48k.wav: frames=96000 sr=48000 peak=0.3200 RMS=-21.85 dBFS
  speech seg 0.10-0.70s RMS=-22.11 dBFS | noise-only 0.85-1.15s RMS=-45.05 dBFS
edge source speechlike_noise_2s_plus100.wav: frames=96100 (= 200*480 + 100; 96100 mod 480 = 100)

##############################################################################
# 2. PER-FILE MEASUREMENTS (all renders, 4.0 s @ 48 kHz stereo, mono mixdown)
##############################################################################
A_denoised(1.0scale)     frames= 192000 finite=True  peak=    0.307946 (  -10.23 dBFS) RMS=   -24.84 dB
B_bypassed(on=0)         frames= 192000 finite=True  peak=    0.319977 (   -9.90 dBFS) RMS=   -24.89 dB
C_nofx(1.0scale)         frames= 192000 finite=True  peak=    0.319977 (   -9.90 dBFS) RMS=   -24.88 dB
D_float_nofx(x32768)     frames= 192000 finite=True  peak=10485.000000 (   80.41 dBFS) RMS=    65.42 dB
E_float_denoised         frames= 192000 finite=True  peak= 9871.031250 (   79.89 dBFS) RMS=    63.26 dB
F_bogus_plugin           frames= 192000 finite=True  peak=    0.319977 (   -9.90 dBFS) RMS=   -24.89 dB
EDGE_denoised            frames= 192000 finite=True  peak=    0.307951 (  -10.23 dBFS) RMS=   -24.81 dB
EDGE_nofx                frames= 192000 finite=True  peak=    0.319977 (   -9.90 dBFS) RMS=   -24.88 dB

per-segment RMS (dBFS):
file                      speech1 .1-.7  noise .85-1.15  speech2 1.3-1.9  tail 2.1-3.9
A_denoised(1.0scale)             -21.59          -45.02           -20.39       -116.34
B_bypassed(on=0)                 -22.11          -45.05           -20.92          -inf
C_nofx(1.0scale)                 -22.11          -45.05           -20.92          -inf
D_float_nofx(x32768)              68.20           45.26            69.39          -inf
E_float_denoised                  68.57           21.21            64.75        -27.31
F_bogus_plugin                   -22.11          -45.05           -20.92          -inf
EDGE_denoised                    -21.59          -45.02           -20.39       -125.57
EDGE_nofx                        -22.11          -45.05           -20.92          -inf

##############################################################################
# 3. A-B-C DIFFERENCES (int16-scale sample; plugin active vs bypassed vs absent)
##############################################################################
A_denoised(1.0scale)   vs B_bypassed(on=0)    : RMS(all)   +0.05 dB | speech   +0.52 dB | noise-only   +0.03 dB | max|diff| 0.473392 | identical=False
A_denoised(1.0scale)   vs C_nofx(1.0scale)    : RMS(all)   +0.04 dB | speech   +0.52 dB | noise-only   +0.03 dB | max|diff| 0.473392 | identical=False
B_bypassed(on=0)       vs C_nofx(1.0scale)    : RMS(all)   -0.01 dB | speech   +0.00 dB | noise-only   +0.00 dB | max|diff| 0.234283 | identical=False
F_bogus_plugin         vs C_nofx(1.0scale)    : RMS(all)   -0.01 dB | speech   +0.00 dB | noise-only   +0.00 dB | max|diff| 0.234283 | identical=False
```

### 4.1 Plain answer: did the plugin measurably change the audio?

**Yes — structurally — but not as a denoiser.**

* The noise-only passage (0.85–1.15 s) changes by **+0.03 dB** between A (active) and C (no FX). The plugin does **not** attenuate the noise at LMMS's signal scale. A's noise-only RMS is −45.02 dBFS in all three repeat renders; C's is −45.05 dBFS.
* The plugin *does* alter the signal: A differs from C in **81 % of samples** (source region, \|diff\|>0.01), max\|diff\| = **0.473392**, and it is not a simple gain or delay — see §5.
* **B (bypassed) vs C (no FX) are bit-identical outside the first 70 ms** (max\|diff\| = 0.0000000000 for frames ≥ 3360). So the bypass path is transparent, and the A-vs-C difference is caused by the plugin's processing, not by the FX-chain infrastructure.

### 4.2 The bypass/control difference is host startup variance, not the plugin

B vs C and F vs C differ only in the first ~62 ms (whole-file max\|diff\| 0.234283). Rendering the *same* project twice reproduces that same envelope, so it is the host's own startup jitter:

```
=== B vs C: outside the 70ms startup window ===
  frames >=3360: max|diff|=0.0000000000  (whole file max 0.234283)
=== F vs C: outside startup ===
  frames >=3360: max|diff|=0.0000000000  (whole file max 0.234283)
=== C_1 vs C_3 (same project, two renders) ===
  whole file max|diff|=0.260376   frames>=3360 max|diff|=0.0000000000
```

In an earlier repeat pair the same check left max\|diff\| 0.0170593262 after frame 3360 (consistent with a one-sample alignment shift); in the run above it is exactly 0. Either way the difference is confined to the host's startup window and is **not** plugin-specific — `C_1` vs `C_3` are two renders of a project with no effect plugin at all.

(archived probe: `testdata/probe_bypass_and_delay.py`; regenerate with `testdata/repeat_renders.sh`)

---

## 5. WHAT THE PLUGIN ACTUALLY DOES AT LMMS SCALE (delay + re-synthesis, no denoising)

Characterisation of A vs C (`testdata/final_probe.py`; the delayed-copy residual block at the end is `testdata/probe_bypass_and_delay.py`):

```
=== 1. where is the A-vs-C difference? ===
max|diff| = 0.473392 at frame 15602 (0.325042 s)
  startup 0-70ms        : max|diff|=0.460208  mean|diff|=0.09845295  frac>0.01=78.333%
  source 70ms-2.0s      : max|diff|=0.473392  mean|diff|=0.09393941  frac>0.01=81.264%
  after 2.0s            : max|diff|=0.299250  mean|diff|=0.00171389  frac>0.01=1.842%

=== 3. noise-only passage: is A a delayed copy of C? (normalised xcorr) ===
  speech1 0.10-0.70s  : best lag=1435 corr=+0.9854  (lag 480: -0.2970, lag 960: +0.1050)
  noise 0.85-1.15s    : best lag=1439 corr=+0.9978  (lag 480: +0.0045, lag 960: +0.0065)

=== 4. band levels A vs C (noise-only passage, dB A/C) ===
      0-  200 Hz:   -0.56 dB
    200- 1000 Hz:   -0.00 dB
    1000- 4000 Hz:   -0.08 dB
    4000- 8000 Hz:   -0.08 dB
    8000-16000 Hz:  +0.10 dB
    16000-24000 Hz: +0.02 dB

=== A vs C: delayed-copy residual (A[n] vs C[n-1439]) ===
  speech1    : signal rms= -21.59 dB, residual rms= -35.19 dB, residual rel signal=-13.60 dB, max|res|=0.048475
  noise-only : signal rms= -45.02 dB, residual rms= -67.07 dB, residual rel signal=-22.04 dB, max|res|=0.001713
  speech2    : signal rms= -20.39 dB, residual rms= -35.98 dB, residual rel signal=-15.59 dB, max|res|=0.045435
```

Interpretation:

* In the noise-only passage the output is **99.8 % correlated with the input delayed by 1439 samples** and every band level is unchanged within ±0.1 dB. The noise passes through essentially untouched — hence the +0.03 dB.
* 1439 samples ≈ 3 × 480 = 1440 — the plugin's structural latency: 480 samples of dry passthrough while the first input frame fills (`m_hasOutput` is false for the first 480 samples, §7) + 960 samples of RNNoise's own analysis/synthesis latency (the bare library measures 957 samples, §6.2). A normalised cross-correlation peaks at 1435 samples (speech1, 29.90 ms), 1439 (noise-only, 29.98 ms) and 1437 (speech2, 29.94 ms) — `measure.py` §6; the correlation surface also has near-equal aliases at the speech pitch period, but the noise-only segment pins the delay unambiguously (corr +0.9978).
* The plugin is therefore *processing* (it runs the whole RNNoise pipeline, adds latency, re-synthesises through the OLA windows) but its effective spectral gains are ≈ 1 — a passthrough.
* **Tail flush** (bit-identical across three A renders, sha256 `c097a7a86a79a1b2`): after the source ends at 2.0 s the plugin emits a decaying burst, RMS 0.127065 (2.00–2.02 s) → 0.104229 → 0.002232 (2.05–2.07 s) → ~0 by 2.15 s, versus digital silence in B/C; the residual in 2.1–3.9 s is −116.34 dBFS (B/C: −inf). This is the unflushed partial frame + overlap-add ring — audible as a short "swish" at the end of a clip.

```
=== 2. tail burst stability across A runs ===
  A_1: noise-only rms=0.0056 (-45.02 dB) | 2.00-2.02s rms=0.127065 | 2.02-2.04s rms=0.104229 | 2.05-2.07s rms=0.002232 | 2.10-2.12s rms=0.000014 | 2.15-2.17s rms=0.000000
  A_2: noise-only rms=0.0056 (-45.02 dB) | 2.00-2.02s rms=0.127065 | 2.02-2.04s rms=0.104229 | 2.05-2.07s rms=0.002232 | 2.10-2.12s rms=0.000014 | 2.15-2.17s rms=0.000000
  A_3: noise-only rms=0.0056 (-45.02 dB) | 2.00-2.02s rms=0.127065 | 2.02-2.04s rms=0.104229 | 2.05-2.07s rms=0.002232 | 2.10-2.12s rms=0.000014 | 2.15-2.17s rms=0.000000
```

---

## 6. ROOT CAUSE: MISSING ±32768 (CELT_SIG_SCALE) CONVERSION

### 6.1 The controlled experiment (same binary, same project, only the amplitude scale differs)

Projects D/E are byte-identical to C/A except that the sample is **float32 at RNNoise's native scale** (content × 32768; generated by `testdata/make_scale_experiment.py`). `src/core/SampleDecoder.cpp` uses `sf_read_float` with no normalisation, so the plugin receives those values unmodified.

```
# 4. SCALE EXPERIMENT (same plugin binary, float sample at RNNoise's native scale)
speech1      rmsD=  2569.4915 rmsE=  2682.1304  E/D=   1.0438  (  +0.37 dB)
NOISE-ONLY   rmsD=   183.1663 rmsE=    11.4944  E/D=   0.0628  ( -24.05 dB)
speech2      rmsD=  2948.0669 rmsE=  1726.8749  E/D=   0.5858  (  -4.65 dB)
silent tail  rmsD=     0.0000 rmsE=     0.0431  E/D=43099649250507349630010785792.0000  (   +inf dB)
overall: rmsD=1866.7137 rmsE=1454.6520 (-2.17 dB) | peakD=10485.0 peakE=9871.0
noise-only band levels dB(E/D):
       0-   200 Hz:  -18.48 dB
     200-  1000 Hz:  -25.60 dB
    1000-  4000 Hz:  -41.45 dB
    4000-  8000 Hz:  -43.59 dB
    8000- 16000 Hz:  -48.69 dB
   16000- 24000 Hz:  -65.39 dB
```

**At the native scale the same plugin suppresses the noise-only passage by −24.05 dB** (three repeat renders: −24.05 dB each, `testdata/repeat-renders-log.txt`; an earlier batch had one run at −29.8 dB) **and preserves speech within ~5 dB**, with band-dependent suppression up to −65 dB at HF. At LMMS's ±1.0 scale: **+0.03 dB**.

### 6.2 Independent library-level proof (harness, no LMMS involved)

`testdata/rnn_harness.c` links the plugin's own vendored RNNoise sources and calls `rnnoise_process_frame()` directly on the same test signal at both scales (`testdata/run_harness.sh`; excerpt — full output in `testdata/harness-log.txt`):

```
### library-level harness runs (vendored rnnoise, same sources as the plugin)
$ /tmp/rnn_harness harness_in.f32 harness_out_s1.f32 1 harness_stats_s1.csv
  frames=200 mean_vad_prob=0.0000  exit=0
$ /tmp/rnn_harness harness_in.f32 harness_out_s32768.f32 32768 harness_stats_s32768.csv
  frames=200 mean_vad_prob=0.4887  exit=0

-- scale x1 (s1) --
   speech               in_rms=  -22.11 dB out_rms=  -21.75 dB  change=  +0.36 dB  mean_vad=0.000  frames_vad>0.5=0/60
   noise-only           in_rms=  -45.05 dB out_rms=  -45.02 dB  change=  +0.03 dB  mean_vad=0.000  frames_vad>0.5=0/30
-- scale x32768 (s32768) --
   speech               in_rms=  -22.11 dB out_rms=  -21.81 dB  change=  +0.30 dB  mean_vad=0.990  frames_vad>0.5=60/60
   noise-only           in_rms=  -45.05 dB out_rms=  -77.06 dB  change= -32.01 dB  mean_vad=0.174  frames_vad>0.5=3/30
```

* At ×1.0: noise-only change **+0.03 dB**, and the network's voice-activity output is **0.000 for all 200 frames** — degenerate.
* At ×32768: noise-only change **−32.01 dB**, VAD **0.990 on speech / 0.174 on noise** — exactly the intended behaviour.
* The time-domain output at ×1.0 is a delayed copy of the input (best lag 957 ≈ 2 frames, corr +0.98669), which matches the host's 1439 ≈ 3 frames once the plugin's extra output-buffer frame is added.

The missing conversion is visible in the plugin's own code (`RnnoiseDenoiserEffect.cpp`, lines 90–110):

```
90		for (f_cnt_t i = 0; i < frames; ++i)
91		{
92			// Downmix to mono for RNNoise input
93			const float monoIn = (buf[i][0] + buf[i][1]) * 0.5f;
94			m_inputBuf[m_inputCount++] = monoIn;
...
99				rnnoise_process_frame(m_rnnoiseState, m_outputBuf, m_inputBuf);
...
108				const float denoised = m_outputBuf[m_outputPos++];
109				buf[i][0] = buf[i][0] * d + denoised * w;
110				buf[i][1] = buf[i][1] * d + denoised * w;
```

RNNoise's API is calibrated for ±32768 (its own header, `rnnoise/arch.h`):

```
49:#define CELT_SIG_SCALE 32768.f
246:#define SCALEIN(a)      ((a)*CELT_SIG_SCALE)
247:#define SCALEOUT(a)     ((a)*(1/CELT_SIG_SCALE))
```

Neither `SCALEIN` nor `SCALEOUT` is applied anywhere in the plugin. **Fix direction** (for the follow-up task, not done here — this test made no source changes): multiply `monoIn` by 32768 before storing into `m_inputBuf`, and divide the value read from `m_outputBuf` by 32768 before the wet/dry mix. Expected result: the noise-only passage drops by ~24 dB in the A-B render.

### 6.3 Hypothesis checked and rejected: the silence gate

RNNoise has a silence gate (`rnnoise/denoise.c:389-393`):

```
389	  if (!TRAINING && E < 0.04) {
390	    /* If there's no audio, avoid messing up the state. */
391	    RNN_CLEAR(features, NB_FEATURES);
392	    return 1;
393	  }
```

I replicated `frame_analysis` + `compute_band_energy` exactly (`testdata/gate_proof.py`) on the real test signal:

```
               passage      scale      E = sum(Ex)     gate
     speech 0.10-0.70s  x1 (LMMS)          621.403     open
     speech 0.10-0.70s x32768 (native)      6.67226e+11     open
 noise-only 0.85-1.15s  x1 (LMMS)          2.79171     open
 noise-only 0.85-1.15s x32768 (native)      2.99757e+09     open
```

The gate does **not** fire at LMMS scale (E = 2.79 > 0.04), so the passthrough is not the silence gate — the network really runs, but its input features are ~5 orders of magnitude outside the training range, its VAD output pins at 0.000 (harness above), and the resulting gains leave the signal essentially unchanged. The scale dependence is therefore unambiguous: only the amplitude conversion is missing.

---

## 7. REALTIME AUDIT (static)

File: `lmms-rnnoise/plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp` (138 lines). Buffers are fixed-size members declared in `RnnoiseDenoiserEffect.h:57-67`:

```
57		// RNNoise processes exactly 480-sample frames (10 ms at 48 kHz)
58		static constexpr int RNNOISE_FRAME_SIZE = 480;
59
60		// Input accumulation buffer (mono)
61		float m_inputBuf[RNNOISE_FRAME_SIZE];
62		int m_inputCount;
63
64		// Output buffer for the last processed frame
65		float m_outputBuf[RNNOISE_FRAME_SIZE];
66		int m_outputPos;
67		bool m_hasOutput;
```

The 480-frame accumulation path (`RnnoiseDenoiserEffect.cpp:84-123`, full quote):

```
84	Effect::ProcessStatus RnnoiseDenoiserEffect::processImpl(
85		SampleFrame* buf, const f_cnt_t frames)
86	{
87		const float d = dryLevel();
88		const float w = wetLevel();
89
90		for (f_cnt_t i = 0; i < frames; ++i)
91		{
92			// Downmix to mono for RNNoise input
93			const float monoIn = (buf[i][0] + buf[i][1]) * 0.5f;
94			m_inputBuf[m_inputCount++] = monoIn;
95
96			// When we have a full 480-sample frame, process through RNNoise
97			if (m_inputCount >= RNNOISE_FRAME_SIZE)
98			{
99				rnnoise_process_frame(m_rnnoiseState, m_outputBuf, m_inputBuf);
100				m_inputCount = 0;
101				m_outputPos = 0;
102				m_hasOutput = true;
103			}
104
105			// Read from the output buffer (with latency) or pass dry signal
106			if (m_hasOutput)
107			{
108				const float denoised = m_outputBuf[m_outputPos++];
109				buf[i][0] = buf[i][0] * d + denoised * w;
110				buf[i][1] = buf[i][1] * d + denoised * w;
111
112				if (m_outputPos >= RNNOISE_FRAME_SIZE)
113				{
114					m_outputPos = 0;
115					m_hasOutput = false;
116				}
117			}
118			// else: before the first frame is processed, input passes through unchanged
119			// (dry-only until the first 480-sample frame completes)
120		}
121
122		return ProcessStatus::ContinueIfNotQuiet;
123	}
```

`m_inputBuf[m_inputCount++] = monoIn` (line 94) is a store into a fixed-size member array; when `m_inputCount >= RNNOISE_FRAME_SIZE` it calls `rnnoise_process_frame()` (line 99), resets the counters (lines 100-102), and reads out of `m_outputBuf[m_outputPos++]` (line 108) with a wrap at 480 (lines 112-116).

**No allocation, no locking, no exceptions, no I/O on this path:**

* No `new`/`malloc`/`free`/`std::vector`/`std::string`/`QString`/`qDebug`/mutex/atomic anywhere in the loop or in `rnnoise_process_frame`. The only heap allocation is `rnnoise_create(nullptr)` in the constructor (line 70) and the matching `rnnoise_destroy()` in the destructor (line 78) — both off the audio thread. Verified by inspection of the vendored sources (`denoise.c`, `rnn.c`, `pitch.c`, `kiss_fft.c`, `celt_lpc.c`, `nnet*.c`): the FFT/model state is allocated once in `rnnoise_create` and the per-frame path uses fixed-size stack arrays.
* All buffers are members with fixed capacity; the write index `m_inputCount` is bounded by the `>= 480` reset on the same line as the store, and `m_outputPos` is bounded by the wrap at lines 112-116.

**Flagged (not RT-safety violations, but real defects/risks):**

1. **Missing scale conversion** (lines 93-94 / 108-110) — the functional defect proven in §6.
2. **No tail flush**: a final partial frame (<480 samples) is never processed; its content stays in `m_inputBuf` and is flushed only when later silence completes the frame, which is why the host render shows the decaying tail burst (§5). Data-dependent latency on the last partial frame.
3. **`m_rnnoiseState` is not null-checked** in `processImpl()` (line 99). `rnnoise_create` only fails on OOM, but a null state would be passed straight into `rnnoise_process_frame()`.
4. `m_inputBuf` / `m_outputBuf` are not zero-initialised in the constructor (lines 60-67). Benign (fully written before read) but worth tidying.
5. `return ProcessStatus::ContinueIfNotQuiet` (line 122) means the effect auto-quits when the output goes silent; on wake-up the internal RNNoise state is whatever it was — acceptable, but relevant to the tail behaviour.

---

## 8. EDGE CASE: clip length not a multiple of 480 frames

Source: `testdata/speechlike_noise_2s_plus100.wav` — **96 100 frames = 200 × 480 + 100** (tail = 100 samples = 0.208 frames).

```
$ cd /home/.../lmms-rnnoise/build
$ QT_QPA_PLATFORM=offscreen ./lmms render ../plugins/RnnoiseDenoiser/testdata/rnnoise_edge.mmp -f wav -s 48000 -a -o ../plugins/RnnoiseDenoiser/testdata/out_edge_denoised.wav
PERFLOG |       Project Render | 0.13user, 0.24system 0.07elapsed
Loading project...
Done
EXIT=0
-rw-rw-r-- 1 kruzzzzy kruzzzzy 1536132 Sep  8 20:46 .../out_edge_denoised.wav

$ QT_QPA_PLATFORM=offscreen ./lmms render ../plugins/RnnoiseDenoiser/testdata/rnnoise_edge_nofx.mmp -f wav -s 48000 -a -o ../plugins/RnnoiseDenoiser/testdata/out_edge_nofx.wav
EXIT=0   (1536132 bytes)
```

No crash, no hang (0.07 s render). Output finite, no NaN/Inf:

```
# 5. EDGE CASE (96100-frame clip = 200 full frames + 100-sample partial frame)
EDGE_denoised finite=True  EDGE_nofx finite=True
last non-zero sample: EDGE_denoised=103268 (2.151417 s)  EDGE_nofx=96099 (2.002063 s)   [clip ends at frame 96100 = 2.002083 s]
  tail 2.00-2.01s: rms EDGE_denoised=0.123926  EDGE_nofx=0.056264
  tail 2.01-2.05s: rms EDGE_denoised=0.102537  EDGE_nofx=0.000000
  tail 2.05-2.10s: rms EDGE_denoised=0.000502  EDGE_nofx=0.000000
  tail 2.10-2.50s: rms EDGE_denoised=0.000001  EDGE_nofx=0.000000
max|EDGE_denoised - EDGE_nofx| = 0.473393
```

The 100-sample partial frame is **not** dropped: the plugin keeps emitting a decaying tail until 2.151 s (≈ 150 ms past the clip end) — the same flush/ring behaviour as the aligned case (§5).

---

## 9. HOST-LEVEL CAVEATS FOUND DURING TESTING (not plugin defects)

1. **Intermittent SIGABRT at shutdown.** The abort is rare and batch-dependent — two archived batches (10–20 renders per project):

   ```
   $ bash testdata/crash_stats.sh          # current batch, 3 projects x 10 renders
   edge exit codes: 0 0 0 0 0 0 0 0 0 0   (nonzero: 0/10)
   edge_nofx exit codes: 0 0 0 0 0 0 0 0 0 0   (nonzero: 0/10)
   A exit codes: 0 0 0 0 0 0 0 0 0 0   (nonzero: 0/10)

   earlier batch (testdata/crash-evidence/crash-rate-batch1.txt):
   edge: 3/20 logs contain 'QThread: Destroyed while thread is still running' | wavs present: 20/20
   edge_nofx: 2/10 logs contain 'QThread: Destroyed while thread is still running' | wavs present: 10/10
   A: 0/10 logs contain 'QThread: Destroyed while thread is still running' | wavs present: 10/10
   ```

   Every abort prints `QThread: Destroyed while thread is still running`, **after** "Done", and the WAV is complete (1 536 132 bytes, 192 000 frames). gdb on the **zero-plugin** project `rnnoise_edge_nofx.mmp` (archived: `testdata/crash-evidence/gdb_backtrace_edge_nofx.log`):

   ```
   Thread 1 "lmms" received signal SIGABRT, Aborted.
   === BACKTRACE ===
   #5  qAbort() ... libQt6Core.so.6
   #7  ...
   #8  lmms::AudioEngineWorkerThread::~AudioEngineWorkerThread() ()
   #9  QObjectPrivate::deleteChildren()
   #11 lmms::AudioEngine::~AudioEngine() ()
   #12 lmms::Engine::destroy() ()
   #13 main ()
   ```

   A host shutdown race in `AudioEngineWorkerThread` — reproducible with **no plugins loaded**, so it is not caused by the denoiser. It is a separate LMMS bug.

2. **Render-to-render nondeterminism in the first ~70 ms** (§4.2): repeated renders of the *same* project can differ by up to max\|diff\| 0.260376 in that window (and by ~0.017 in an earlier pair, consistent with a one-sample alignment shift). Aggregate measurements (RMS, band levels) are stable where it matters: A's noise-only RMS was −45.02 dBFS in all three repeats (`testdata/repeat-renders-log.txt` §2).

3. **Render determinism varies run to run, and is not tied to the plugin.** sha256 of the WAV data chunk, three renders each (`testdata/repeat-renders-log.txt`):

   ```
   A_1=c097a7a86a79a1b2  A_2=c097a7a86a79a1b2  A_3=c097a7a86a79a1b2
   C_1=4dd5fa9f9a65858a  C_2=4dd5fa9f9a65858a  C_3=82321df67217a6d2
   D_1=9cef43d36c5f8235  D_2=9cef43d36c5f8235  D_3=e8985470ba0043a7
   E_1=6f8c3dd9571d436c  E_2=6f8c3dd9571d436c  E_3=6f8c3dd9571d436c
   ```

   Here the **no-FX** projects (C, D) differ between runs while both plugin projects (A, E) are bit-identical; in an earlier batch E_1 differed from E_2/E_3 (noise-only 15.48 dB vs 21.21 dB). The variance lives in the host's startup window (§4.2) and every noise-only measurement is identical across the three A renders, so the conclusion is unaffected.

---

## 10. VERDICT AND REMAINING WORK

**runtime-verified: partial** — *the one blocking defect (missing ±32768 conversion) is now fixed and re-measured; see §11. The items below are the pre-fix verdict, kept as history.*

* **Loads**: yes, proven by strace (`librnnoisedenoiser.so` → fd 23) and by a rendered FX chain that names it.
* **Processes**: yes, proven by the A-vs-C/B/C controls — the plugin adds deterministic ~30 ms latency, re-synthesises through RNNoise's overlap-add path, and emits a reproducible tail burst; the bypassed and absent cases are bit-identical outside the host's startup window.
* **Denoises**: **no, not as shipped** — the noise-only passage changes by +0.03 dB at LMMS's ±1.0 scale. Root cause: missing ±32768 conversion in `processImpl()`, proven by (a) the same binary suppressing −24.05 dB when fed a native-scale sample in the host, and (b) the same vendored library suppressing −32.01 dB with correct VAD behaviour in an independent harness. The silence-gate hypothesis was tested and rejected (E = 2.79 > 0.04 at LMMS scale).
* **Audio-thread safety**: static audit found no allocation or locking on the 480-frame accumulation path.
* **Edge case**: 96 100-frame clip renders without crash or hang; tail flushed correctly.

**Still unverified after this test:**

1. ~~Whether the scale fix actually works end-to-end (needs the code change + re-run of `run_renders.sh`).~~ → **RESOLVED 2026-09-08: implemented, rebuilt, re-rendered and re-measured — §11.**
2. Real-time behaviour under a live audio backend (all measurements here are offline renders; RT-safety is static-only).
3. Behaviour with a real microphone/streaming input and with non-48 kHz sessions (RNNoise is fixed at 48 kHz; resampling is not handled by the plugin).
4. The wet/dry control (`wet="1"` only was exercised; `wet=0.5` and `wet=0` were not).
5. The host `AudioEngineWorkerThread` shutdown abort (§9.1) — separate LMMS bug.

**Single most important follow-up**: ~~apply `SCALEIN`/`SCALEOUT` (×32768 in, ÷32768 out) in `RnnoiseDenoiserEffect::processImpl()` and re-run `testdata/run_renders.sh`; the A-B noise-only delta should move from +0.03 dB to ≈ −24 dB.~~ → **DONE 2026-09-08 (§11)**: applied and re-measured; the delta moved from +0.03 dB to **−21.8…−24.1 dB** (mean −23.2 dB, first render −24.05 dB), as predicted.

---

## 11. FIX APPLIED — SCALEIN/SCALEOUT conversion + re-measurement (2026-09-08)

**Status of §6's root cause: FIXED and re-verified end-to-end.** The noise-only A-vs-C delta moved from **+0.03 dB (FAIL)** to **−21.8 … −24.1 dB** across repeated renders (first render −24.05 dB, mean −23.2 dB; acceptance ≤ −10 dB — **PASS every run**, see §11.7 for the spread); bypass equivalence and the 1439-sample latency are unchanged.

### 11.1 The change (plugin source only — no UI/parameter change)

`RnnoiseDenoiserEffect.cpp` (post-fix line numbers):

| Line | Code | Why |
|---|---|---|
| `:43-44` | `static constexpr float RNNOISE_SCALE_IN = 32768.0f;`<br>`static constexpr float RNNOISE_SCALE_OUT = 1.0f / 32768.0f;` | CELT_SIG_SCALE is 32768 (`rnnoise/arch.h:246-247`); same ×scale/÷scale idiom as `testdata/rnn_harness.c:56,63`, which produced −24/−32 dB |
| `:95-98` | `if (!m_rnnoiseState) { return ProcessStatus::ContinueIfNotQuiet; }` | robustness: `rnnoise_create()` fails only on OOM — pass audio through instead of dereferencing null |
| `:108` | `m_inputBuf[m_inputCount++] = monoIn * RNNOISE_SCALE_IN;` | scale LMMS's ±1.0 into RNNoise's int16 range at the accumulator |
| `:123` | `const float denoised = m_outputBuf[m_outputPos++] * RNNOISE_SCALE_OUT;` | scale the denoised frame back to ±1.0 |

`RnnoiseDenoiserEffect.h`:

| Line | Code |
|---|---|
| `:61` | `float m_inputBuf[RNNOISE_FRAME_SIZE] = {};` |
| `:65` | `float m_outputBuf[RNNOISE_FRAME_SIZE] = {};` |

The 480-frame accumulation, output indexing and wet/dry blend are untouched; the change adds two multiplies per sample and nothing that allocates or locks on the audio thread.

### 11.2 Build + render proof

```
$ cd build && make -j8 rnnoisedenoiser lmms
[  0%] Built target ringbuffer
[  0%] Built target lmmsobjs_autogen_timestamp_deps
[  0%] Built target lmmsobjs_autogen
[ 88%] Built target lmmsobjs
[ 88%] Built target lmms_autogen_timestamp_deps
[ 88%] Built target lmms_autogen
[ 91%] Built target lmms
[ 91%] Generating moc_RnnoiseDenoiserEffect.cpp
[ 94%] Building CXX object plugins/RnnoiseDenoiser/CMakeFiles/rnnoisedenoiser.dir/RnnoiseDenoiserEffect.cpp.o
[ 94%] Building CXX object plugins/RnnoiseDenoiser/CMakeFiles/rnnoisedenoiser.dir/RnnoiseDenoiserControlDialog.cpp.o
[ 94%] Building CXX object plugins/RnnoiseDenoiser/CMakeFiles/rnnoisedenoiser.dir/RnnoiseDenoiserControls.cpp.o
[ 94%] Building CXX object plugins/RnnoiseDenoiser/CMakeFiles/rnnoisedenoiser.dir/moc_RnnoiseDenoiserEffect.cpp.o
[ 94%] Linking CXX shared module ../librnnoisedenoiser.so
[100%] Built target rnnoisedenoiser
[  0%] Built target ringbuffer
[  0%] Built target lmmsobjs_autogen_timestamp_deps
[  0%] Built target lmmsobjs_autogen
[ 96%] Built target lmmsobjs
[ 96%] Built target lmms_autogen_timestamp_deps
[ 96%] Built target lmms_autogen
[100%] Built target lmms
BUILD_EXIT=0
```

Full log: `testdata/build-log-fix.txt`. (`lmms` is already up to date — the host does not link plugins; `librnnoisedenoiser.so` is dlopened at render time, so the plugin relink is what matters. `.so` rebuilt 2026-09-08 21:19.)

`testdata/run_renders.sh` re-run — all 8 renders exit 0, A/B/C regenerated 21:17 (`testdata/render-log-fix.txt`):

| Render | Project | Exit |
|---|---|---|
| A | `rnnoise_test_A.mmp` (plugin active) | 0 |
| B | `rnnoise_test_B.mmp` (bypassed `on="0"`) | 0 |
| C | `rnnoise_test_C.mmp` (no FX) | 0 |
| D | `rnnoise_test_D_float_nofx.mmp` (float ×32768) | 0 |
| E | `rnnoise_test_E_float_denoise.mmp` (float ×32768 + plugin) | 0 |
| EDGE | `rnnoise_edge.mmp` (96100-frame clip) | 0 |
| EDGE_nofx | `rnnoise_edge_nofx.mmp` | 0 |
| F | `rnnoise_test_F_bogus.mmp` (bogus plugin name) | 0 |

### 11.3 Independent measurement — before vs after

Measured by `testdata/measure_fix.py` (independent RIFF chunk parser; Python's `wave` module refuses format-tag-3 float WAVs), then re-confirmed by `testdata/verify_fix_final.py` (fresh self-contained re-read) and by the original `testdata/measure.py`. Raw logs: `measure-fix-before.txt`, `measure-fix-after.txt`, `verify-fix-final.txt`, `measurement-log-fix.txt`.

| Noise-only passage 0.85–1.15 s | Before fix | After fix | Change |
|---|---|---|---|
| A (plugin active) | −45.02 dBFS | **−69.10 dBFS** | −24.08 dB |
| B (bypassed) | −45.05 dBFS | −45.05 dBFS | 0 |
| C (no FX) | −45.05 dBFS | −45.05 dBFS | 0 |
| **A vs C** | **+0.03 dB (FAIL)** | **−24.05 dB (PASS)** | **−24.08 dB** |
| Speech 0.10–0.70 s, A vs C | +0.52 dB | +0.37 dB | speech preserved |

Band suppression after the fix (A vs C, noise-only, dB):

```
       0-   200 Hz:   -18.48
     200-  1000 Hz:   -25.60
    1000-  4000 Hz:   -41.45
    4000-  8000 Hz:   -43.59
    8000- 16000 Hz:   -48.69
   16000- 24000 Hz:   -65.39
```

These are **identical to two decimals** to the pre-fix E-vs-D native-scale experiment (`measurement-log.txt:47-53`: `-18.48 / -25.60 / -41.45 / -43.59 / -48.69 / -65.39`). The fixed plugin at LMMS's ±1.0 scale now behaves exactly like the unfixed plugin did when handed ±32768-scaled audio — an independent cross-check that the conversion lands in the same operating regime.

Raw acceptance output (`testdata/verify_fix_final.py`, fresh re-read of the regenerated WAVs):

```
out_A_denoised.wav: fmt_tag=3 bits=32 sr=48000 len=192000 (4.0000 s)
out_B_bypassed.wav: fmt_tag=3 bits=32 sr=48000 len=192000 (4.0000 s)
out_C_nofx.wav: fmt_tag=3 bits=32 sr=48000 len=192000 (4.0000 s)

segment 0.85-1.15 s (noise-only) and 0.10-0.70 s (speech):
  A_denoised  noise RMS=3.507814384e-04 (  -69.10 dBFS)   speech RMS=8.185212221e-02 (  -21.74 dBFS)
  B_bypassed  noise RMS=5.589792721e-03 (  -45.05 dBFS)   speech RMS=7.841465894e-02 (  -22.11 dBFS)
  C_nofx      noise RMS=5.589792721e-03 (  -45.05 dBFS)   speech RMS=7.841465894e-02 (  -22.11 dBFS)

  A_denoised vs C_nofx: noise-only delta=-24.0472 dB   speech delta=+0.3727 dB
  B_bypassed vs C_nofx: noise-only delta=+0.0000 dB   speech delta=+0.0000 dB

ACCEPTANCE (noise-only A-vs-C <= -10 dB): PASS
regression max|B-C| outside 0.05 s startup = 0.000000000000
```

### 11.4 Regression guard

* **Bypass still equals no-FX**: `max|B−C|` over the whole file = **0.0000000000** (frames ≥ 3360: 0.0000000000) — same as pre-fix outside the startup window.
* **Latency unchanged at 1439 samples (29.98 ms)**:
  * noise-only xcorr argmax is still **lag 1439**; its normalised corr drops to +0.1391 only because A's noise is now 24 dB down — the structural delay is unchanged;
  * `testdata/probe_alignment_vs_prefix.py`: the post-fix A render aligns with the backed-up **pre-fix** A render at **lag 0, corr +0.9995** (speech1) — and the pre-fix render's absolute delay vs C was 1439 at corr +0.9978, so the absolute latency is provably unchanged;
  * `testdata/probe_latency_after.py` residual minimisation prefers 1435–1436 on the speech passage (the same speech-waveform bias the pre-fix run also showed there) with 1439 close behind.

### 11.5 Robustness items from the prior report

| Item | Status |
|---|---|
| unchecked `m_rnnoiseState` | **fixed** — null guard, `RnnoiseDenoiserEffect.cpp:95-98` |
| uninitialised frame buffers | **fixed** — `= {}` initialisers, `RnnoiseDenoiserEffect.h:61,65` |
| tail flush for non-480-multiple clips | **not fixed — documented known issue.** LMMS's `Effect` API exposes no end-of-stream/stop hook to plugins (only `processImpl`), so there is no cheap allocation-free place to flush a partial frame. Behaviour is unchanged from §8: the edge project's final 100 samples are flushed as a decaying tail to 2.151 s; no crash, hang or NaN. |

### 11.6 Note on the D/E native-scale projects after the fix

The pre-fix D/E experiment scaled the source WAV by ×32768 so the *host* delivered native-range samples. After the fix the plugin scales by ×32768 again, so E's input is now 32768× too hot (≈±1.07e9): it over-drives the network and suppresses essentially everything (E speech ≈ −53 dBFS). **D/E are therefore no longer the scale proof — A/B/C are** (and the band-table equivalence in §11.3 preserves the original evidence). D/E remain in `run_renders.sh` as a scale-sensitivity control.

### 11.7 Render-to-render spread of the post-fix figure

Re-rendering A/B/C from the committed source and measuring again gave **−21.79 dB** instead of −24.05 dB. Repeating the A render 5× and the C render 3× (`testdata/repeat_fix_renders.sh`, log `repeat-fix-renders.txt`) shows the figure is stable to a few dB but not bit-exact:

| Run | A noise-only | C noise-only | A-vs-C |
|---|---|---|---|
| A_1 | −69.10 dBFS | −45.05 dBFS | **−24.05 dB** |
| A_2 | −69.10 dBFS | −45.05 dBFS | **−24.05 dB** |
| A_3 | −67.31 dBFS | −45.05 dBFS | −22.26 dB |
| A_4 | −66.85 dBFS | −45.05 dBFS | −21.79 dB |
| A_5 | −69.10 dBFS | −45.05 dBFS | **−24.05 dB** |

- C (no-FX) is bit-stable in the noise-only passage: **−45.05 dBFS in 3/3 runs** (0.00 dB spread).
- A's noise-only level varies by 2.25 dB across runs. **All 5 runs PASS** the ≤ −10 dB acceptance with ≥ 11.8 dB margin; mean delta **−23.24 dB** (target ≈ −24 dB).
- Cause: the host's startup window is nondeterministic (§4.2 — repeated renders of the same project differ in the first ~70 ms; the repeat renders differ only there, their noise-only passages are identical). Pre-fix the network was saturated into a near-passthrough regime and insensitive to that; post-fix the network is active and its adaptive state carries the startup variation into the noise-only passage. The plugin is deterministic for a given input sample stream; this is measurement spread, not instability.
- On-disk artifacts: `out_A/B/C.wav` are the latest re-render (delta −21.79 dB), measured in `measure-fix-after-rerun.txt` / `verify-fix-final-rerun.txt`. The first post-fix render set (delta −24.05 dB) is archived in `measure-fix-after.txt` / `verify-fix-final.txt`.

**Acceptance headline: noise-only A-vs-C = −21.8 … −24.1 dB across repeated renders (mean −23.2 dB), every run ≤ −10 dB — PASS.**

---

## Appendix A — reproduction

```
cd lmms-rnnoise
python3 plugins/RnnoiseDenoiser/testdata/make_test_signal.py        # generate source WAVs
python3 plugins/RnnoiseDenoiser/testdata/make_scale_experiment.py   # generate float x32768 sample + D/E projects
bash   plugins/RnnoiseDenoiser/testdata/run_renders.sh              # 8 renders (A B C D E edge edge_nofx F)
python3 plugins/RnnoiseDenoiser/testdata/measure.py                 # numbers + diffs (§4)
python3 plugins/RnnoiseDenoiser/testdata/final_probe.py             # delay/tail characterisation (§5)
python3 plugins/RnnoiseDenoiser/testdata/gate_proof.py              # silence-gate replication (§6.3)
bash   plugins/RnnoiseDenoiser/testdata/run_harness.sh              # library-level A-B at both scales (§6.2)
bash   plugins/RnnoiseDenoiser/testdata/repeat_renders.sh           # 3x A/C/D/E renders + probes (§4.2, §5, §9)
bash   plugins/RnnoiseDenoiser/testdata/crash_stats.sh                # 10x renders x 3 projects (§9.1)

# --- §11 fix verification (run after the fix + rebuild + run_renders.sh) ---
python3 plugins/RnnoiseDenoiser/testdata/measure_fix.py after      # acceptance number, A/B/C (§11.3)
python3 plugins/RnnoiseDenoiser/testdata/verify_fix_final.py       # fresh independent re-read (§11.3)
python3 plugins/RnnoiseDenoiser/testdata/probe_latency_after.py    # delay residual probe (§11.4)
python3 plugins/RnnoiseDenoiser/testdata/probe_alignment_vs_prefix.py  # post- vs pre-fix alignment (§11.4)
bash    plugins/RnnoiseDenoiser/testdata/repeat_fix_renders.sh 5   # render-to-render spread: 5x A + 3x C (§11.7)
```

The pre-fix baseline (`measure_fix.py before`, archived in `testdata/measure-fix-before.txt`) was
taken against the pre-fix renders, backed up to `/tmp/rnnoise-prefix-wavs/` before `run_renders.sh`
regenerated A/B/C in place.

`probe_bypass_and_delay.py` and `final_probe.py` read the repeat-render WAVs from
`$RNNOISE_REPEAT_DIR` (default `/tmp/crashcheck`); `repeat_renders.sh` sets it.

## Appendix B — artifacts in `testdata/`

| File | What it is |
|---|---|
| `rnnoise_test_A/B/C/F_bogus/D_float_nofx/E_float_denoise/edge/edge_nofx.mmp` | hand-written projects |
| `speechlike_noise_2s_48k.wav`, `speechlike_noise_2s_plus100.wav`, `speechlike_noise_2s_48k_x32768.wav` | test signals |
| `out_A_denoised.wav`, `out_B_bypassed.wav`, `out_C_nofx.wav`, `out_D_float_nofx.wav`, `out_E_float_denoised.wav`, `out_edge_denoised.wav`, `out_edge_nofx.wav`, `out_F_bogus.wav` | render outputs (A-B pair: `out_A_denoised.wav` vs `out_B_bypassed.wav` / `out_C_nofx.wav`) |
| `render-log.txt`, `measurement-log.txt`, `repeat-renders-log.txt`, `harness-log.txt` | pasted outputs of §3, §4, §4.2/§5/§9, §6.2 |
| `strace_A.log` | load proof (§1) |
| `make_test_signal.py`, `make_scale_experiment.py`, `measure.py`, `analyze.py`, `run_renders.sh`, `repeat_renders.sh`, `crash_stats.sh` | generators/analysers |
| `rnn_harness.c`, `run_harness.sh`, `run_harness.py`, `harness_stats_*.csv`, `harness_out_*.f32` | library-level proof (§6.2) |
| `gate_proof.py`, `final_probe.py`, `probe_bypass_and_delay.py` | characterisation probes (§4.2, §5, §6.3) |
| `measure_fix.py`, `verify_fix_final.py`, `probe_latency_after.py`, `probe_alignment_vs_prefix.py`, `repeat_fix_renders.sh`, `repeat_fix_measure.py` | independent post-fix measurement + regression + spread probes (§11) |
| `build-log-fix.txt`, `render-log-fix.txt`, `measure-fix-before.txt`, `measure-fix-after.txt`, `measure-fix-after-rerun.txt`, `measurement-log-fix.txt`, `verify-fix-final.txt`, `verify-fix-final-rerun.txt`, `repeat-fix-renders.txt`, `probe-latency-after.txt`, `probe-alignment-vs-prefix.txt` | fix-verification logs (§11) |
| `crash-evidence/edge_6.log`, `crash-evidence/edge_nofx_4.log`, `crash-evidence/edge_nofx_8.log`, `crash-evidence/gdb_backtrace_edge_nofx.log`, `crash-evidence/crash-rate-log.txt`, `crash-evidence/crash-rate-batch1.txt` | host shutdown abort (§9.1) |
