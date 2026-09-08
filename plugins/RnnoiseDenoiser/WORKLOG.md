# WORKLOG — RNNoise Denoiser Effect Plugin (AI-KOS #559)

## Status: RUNTIME-VERIFIED — DENOISING FIXED (loads, processes, and now suppresses noise by −24.05 dB at LMMS's ±1.0 sample scale; the ×32768 in / ÷32768 out conversion was applied 2026-09-08, commit `bd222bca1`). Full evidence: `RUNTIME-TEST.md` §11.

## Files Created

| Path | Description |
|------|-------------|
| `plugins/RnnoiseDenoiser/CMakeLists.txt` | Build definition using `BUILD_PLUGIN` macro, compiles C++ plugin + vendored RNNoise C sources + model data + artwork + MOC outputs |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.h` | Effect subclass header — forward-declares `DenoiseState`, declares `processImpl()`, 480-sample frame buffer (`m_inputBuf`, `m_outputBuf`), accumulation state (`m_inputCount`, `m_outputPos`) |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp` | Effect implementation — creates `rnnoise_create(nullptr)` in ctor, `rnnoise_process_frame()` per 480-frame block, accumulates partial input, wet/dry blend via `m_controls.m_wetDryModel` |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserControls.h` | Controls class — `wetDryModel` as `FloatModel` (0–100%), `m_effect` backref |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserControls.cpp` | Controls ctor — creates wet/dry knob model, `saveSettings`/`loadSettings` serialization |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserControlDialog.h` | Minimal dialog class header |
| `plugins/RnnoiseDenoiser/RnnoiseDenoiserControlDialog.cpp` | Dialog ctor — builds a vertical layout with knob + LCD display |
| `plugins/RnnoiseDenoiser/artwork.svg` | Placeholder SVG icon |
| `plugins/RnnoiseDenoiser/rnnoise/` | Vendored RNNoise C sources + model data + x86 SIMD sources |
| `cmake/modules/PluginList.cmake` | Registered `RnnoiseDenoiser` in `PLUGIN_LIST` (alphabetical between ReverbSC and Sf2Player) |

## Build Verification

- **Configure**: cmake configured successfully with Qt6 and minimal non-optional deps disabled.
- **Compilation**: `rnnoisedenoiser` target built 20+ translation units (12 C RNNoise files, 4 C++ plugin files, 4 MOC/generated files).
- **Output**: `build/plugins/librnnoisedenoiser.so` — 15 MB ELF 64-bit x86-64 shared object, all symbols resolved.
- **Warnings (benign)**: `#warning "Only SSE and SSE2 are available..."` from RNNoise's `nnet.h` — expected, disabled build config doesn't pass `-march=`.
- **Missing source fix applied**: `nnet.c` and `nnet_default.c` require `x86/x86_arch_macros.h` and `vec.h` includes `x86/` headers — fixed by vendoring the full `src/x86/` directory from the RNNoise repo.

## Unverified / Known Issues

> **Superseded 2026-09-08 by the runtime verification below** (task #559; every claim backed by pasted command output in `RUNTIME-TEST.md`). The original list is kept for history:
>
> 1. **Runtime**: No test runtime executed (no LMMS host available in this build environment). The plugin loads, registers, and links, but actual denoising is unverified.
> 2. **Wet/dry control**: `m_wetDryModel` range is 0–100 (integer percent); the effect blends `wet * processed + (1-wet) * original` per sample. Verified to compile but not tested in DAW.
> 3. **Frame accumulation with non-480 input sizes**: `processImpl()` handles arbitrary `frames` sizes by accumulating into `m_inputBuf` and only calling RNNoise when 480 samples are ready. Edge case: if LMMS sends a very small buffer (e.g., <480 samples at session end), remaining data is flushed on destruction only. This is acceptable for a skeleton.
> 4. **Model data**: Uses big-endian `rnnoise_data.c` (fallback). To use little-endian, swap `rnnoise_data_little.c` and `define` over `rnnoise_data.c` in CMakeLists.txt.

## Runtime verification (2026-09-08, task #559)

**Verdict: runtime-verified: PARTIAL.** Full report: `RUNTIME-TEST.md` (622 lines, every claim with pasted output). Artifacts: `testdata/` (hand-written projects, source/render WAVs, logs, scripts).

Proven in a real headless LMMS 1.3.0-alpha.2 host (`lmms-rnnoise/build/lmms`):

- **Loads — YES.** `strace -f -e trace=openat` during a headless render shows `openat(.../build/plugins/librnnoisedenoiser.so, O_RDONLY|O_CLOEXEC) = 23` (`testdata/strace_A.log:98`); `nm -D --defined-only` shows `rnnoisedenoiser_plugin_descriptor` exported. Absence of error text is *not* load proof: a project naming a non-existent plugin renders exit 0 with no error (LMMS silently substitutes DummyEffect) — hence strace.
- **Processes — YES.** The plugin adds a deterministic 1439-sample (29.98 ms) latency (480 dry passthrough + 2×480 RNNoise frames) and re-synthesises through overlap-add with a reproducible tail burst. A (active `on="1"`), B (bypassed `on="0"`), C (plugin absent `numofeffects="0"`) all render exit 0; B and C are equivalent outside the host's startup window.
- **Denoises — NO, not as shipped.** The noise-only passage (0.85–1.15 s of the test signal) changes by **+0.03 dB** at LMMS's ±1.0 sample scale (A vs C). Root cause: `processImpl()` feeds raw ±1.0 floats to `rnnoise_process_frame()`, which expects the ±32768 (CELT_SIG_SCALE) range. Proven two ways: (a) the same binary in the same host suppresses the noise-only passage by **−24.05 dB** when the source WAV is pre-scaled to ±32768; (b) an independent harness linking the plugin's own vendored RNNoise (`testdata/rnn_harness.c`) reproduces it at both scales with correct VAD behaviour. The silence-gate hypothesis (`E < 0.04`) was tested and rejected (E = 2.79 at ±1.0, `testdata/gate_proof.py`).
- **Audio-thread safety — static audit clean.** `m_inputBuf[m_inputCount++]` / `m_outputBuf[m_outputPos++]` are stores/reads into fixed-size member arrays; no allocation, no locking, no `new`/`malloc`/mutex on the 480-frame path (`RnnoiseDenoiserEffect.cpp:84-123`, quoted with line numbers in `RUNTIME-TEST.md` §7).
- **Edge case — PASS.** 96 100-frame clip (200×480 + 100): renders exit 0, no crash/hang; the 100-sample partial frame is flushed as a decaying tail to 2.151 s.

State of the original 4 items:

1. **Runtime** → resolved: tested; verdict partial (above).
2. **Wet/dry control** → still unverified: only `wet="1"` exercised; `wet=0.5`/`wet=0` not tested. (`controlCount()==0`; blending uses Effect's built-in wet/dry models.)
3. **Non-480 frame sizes** → resolved: no crash/hang, partial frame flushed (edge case above). The flush is a ~150 ms decaying burst rather than a hard stop — audible behaviour worth reviewing.
4. **Model data endianness** → unchanged/unverified: big-endian `rnnoise_data.c` fallback still in use.

Also found (host, not plugin): intermittent SIGABRT at shutdown in `lmms::AudioEngineWorkerThread::~AudioEngineWorkerThread()` (Qt `QThread: Destroyed while thread is still running`), reproducible with **zero plugins** (2/10 and 3/20 runs) and with complete WAVs — a separate LMMS bug. Evidence: `testdata/crash-evidence/`.

**Remaining unverified:** (1) the ×32768 fix end-to-end; (2) live-audio real-time behaviour (all measurements are offline renders; RT-safety is static-only); (3) real microphone/streaming input and non-48 kHz sessions (plugin does no resampling; RNNoise is fixed at 48 kHz); (4) wet=0.5/0; (5) the GUI control dialog in a real GUI session.

**Single most important follow-up:** apply `SCALEIN`/`SCALEOUT` (×32768 in, ÷32768 out) in `RnnoiseDenoiserEffect::processImpl()` and re-run `testdata/run_renders.sh` — the A-B noise-only delta should move from +0.03 dB to ≈ −24 dB. → **DONE 2026-09-08, see below.**

## Fix applied (2026-09-08, task #559 follow-up) — DENOISING NOW WORKS

**Verdict: the scale bug is fixed and re-verified end-to-end.** Full evidence: `RUNTIME-TEST.md` §11.

- **Change (plugin source only; no UI/parameter change)** — commit `bd222bca1bdc13d147f8493c58ad12f34a757702`:
  - `RnnoiseDenoiserEffect.cpp:43-44` `RNNOISE_SCALE_IN = 32768.0f`, `RNNOISE_SCALE_OUT = 1.0f/32768.0f`
  - `:95-98` pass audio through if `m_rnnoiseState` is null (OOM only) instead of dereferencing it
  - `:108` ×32768 at the accumulator store; `:123` ÷32768 at the output read
  - `RnnoiseDenoiserEffect.h:61,65` frame buffers zero-initialised (`= {}`)
  - No allocation/locking added; the 480-frame accumulation and output indexing are untouched.
- **Build**: `cd build && make -j8 rnnoisedenoiser lmms` → **exit 0** (`testdata/build-log-fix.txt`); `librnnoisedenoiser.so` relinked 21:19.
- **Render**: `testdata/run_renders.sh` → **8/8 exit 0**; A/B/C regenerated 21:17 (`testdata/render-log-fix.txt`).
- **Measured A-B result** (independent RIFF parse, `testdata/measure_fix.py`, re-confirmed by fresh `testdata/verify_fix_final.py` and by `testdata/measure.py`):
  - noise-only 0.85–1.15 s: A = **−69.10 dBFS**, C = −45.05 dBFS → **A-vs-C = −24.05 dB** (was **+0.03 dB**) → **PASS** (acceptance ≤ −10 dB)
  - band suppression 0–24 kHz: −18.48 / −25.60 / −41.45 / −43.59 / −48.69 / −65.39 dB — identical to 2 decimals to the pre-fix native-scale E-vs-D experiment
  - speech 0.10–0.70 s: A-vs-C **+0.37 dB** (speech preserved)
  - regression: `max|B−C|` = **0.0000000000** outside the startup window; latency unchanged at **1439 samples** (post-fix A aligns with the backed-up pre-fix A at lag 0, corr +0.9995)
- **Robustness**: null-state guard and buffer initialisation fixed. Tail flush for non-480-multiple clips remains a **documented known issue** — LMMS's `Effect` API has no end-of-stream hook, so there is no cheap allocation-free place to flush a partial frame; behaviour is unchanged from §8 (decaying tail to 2.151 s, no crash/hang/NaN).
- **Docs commit**: `RUNTIME-TEST.md` §11 + this section (see `git log` on `feat/rnnoise-denoiser`).

**Remaining unverified (unchanged):** (1) wet=0.5 / wet=0; (2) live real-time audio under a real backend (all measurements are offline renders; RT-safety is static-only); (3) microphone/streaming input and non-48 kHz sessions; (4) the GUI control dialog in a real GUI session; (5) the host `AudioEngineWorkerThread` shutdown abort (separate LMMS bug, §9.1).