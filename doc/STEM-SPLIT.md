# Stem separation — offline job manager (task #578)

> **Historical program artifact.** Written inside the FL-program worktree `lmms-stems/`
> (branch `feat/stem-split`), which is not part of this product repo. The feature is in
> `main` (`src/core/StemJobManager.cpp` and friends); this note is the original
> task-level evidence, kept for provenance — not a product document.

Branch `feat/stem-split`, worktree `lmms-stems/`. Inherited WIP: `c70f83c35`.

**Every number below was produced by the command shown next to it. Nothing is
estimated.** Results come from exactly two build directories — `build-nort/`
(no ONNX Runtime) and `build-ort/` (ONNX Runtime present) — see "Build
directories" for why that matters.

## Gate summary

| check | command (workdir `lmms-stems/`) | exit |
|---|---|---|
| configure, no ORT | `cmake -S . -B build-nort -DCMAKE_BUILD_TYPE=Release -DWANT_QT6=ON -DWANT_STEM_SPLIT=ON` | **0** |
| configure, ORT present | `cmake -S . -B build-ort -DCMAKE_BUILD_TYPE=Release -DWANT_QT6=ON -DWANT_STEM_SPLIT=ON -DONNXRUNTIME_ROOT=/tmp/ort-sdk-real` | **0** |
| build, no ORT | `cmake --build build-nort -j4` | **0** (447 s) |
| build, ORT present | `cmake --build build-ort -j4` | **0** (440 s) |
| ctest, no ORT | `cd build-nort/tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure` | **0** — 11/11 passed |
| ctest, ORT present | `cd build-ort/tests && QT_QPA_PLATFORM=offscreen ctest --output-on-failure` | **0** — 12/12 passed |

Clean-room: both directories were deleted and reconfigured from an empty state
after the fixes, so these are full builds of the committed tree, not
incrementals. ONNX Runtime SDK used: **v1.28.0 linux-x64** staged at
`/tmp/ort-sdk-real/` (headers + `libonnxruntime.so.1.28.0`).

## What this is

Offline 4-stem separation (drums / bass / other / vocals) for LMMS, run as a
**background job**. Real time is explicitly out of scope: the backend's
receptive field is ~7.8 s (HTDemucs), so a job necessarily runs behind the
transport. No stem code touches the audio thread (see "Audio-thread audit").

Two interchangeable backends behind `StemSeparator`:

| backend | inference path | availability |
|---|---|---|
| `ExternalProcessStemSeparator` | spawns `/usr/bin/python3 tools/stem_split_cli.py`, Python `onnxruntime` | whenever a python3 with `onnxruntime` is found |
| `OnnxRuntimeStemSeparator` | in-process C++ `Ort::Session` | only when built with the ORT SDK (`LMMS_HAVE_ONNXRUNTIME`) |

`StemJobManager` owns one worker `QThread` (`QThread::create`), a job table,
progress/cancel, and delivers results via queued signals.
`StemSplitController` is the GUI side; `StemTrackBuilder` turns a finished
`StemSet` into tracks/clips in the Song.

## Quality: the segment overlap (Q050-062.1)

Both backends and `tools/stem_split_cli.py` run the model over overlapping
segments and average the Hann-windowed predictions (overlap-add). The overlap
is the quality setting, `StemQuality` in `include/StemSeparation/StemTypes.h`:

| quality | overlap | hop (frames between segment starts) | passes vs standard |
|---|---|---|---|
| `fast` | 25 % | `floor(segment * 3 / 4)` | about 2/3 |
| `standard` (default) | 50 % | `floor(segment * 2 / 4)` = `segment / 2` | 1 |
| `high` | 75 % | `floor(segment * 1 / 4)` | about 2x |

The hop is never below 1. `standard` is the overlap every backend used before
the setting existed, so a caller that omits it gets the legacy output bit for
bit. The C++ backends call `stemHopFrames()`; the CLI's `--quality` uses the
same table and integer formula, and its `info` line reports `quality`, `hop`
and `chunks`. `stem.job_start` takes `quality` (omitted = `standard`) and every
job object reports the effective `quality`, `overlap` and `hop_frames` (read
from `StemJobManager::quality()`, not the surface's record of the request) and
`inference_quality`: the setting the worker handed `separate()`, published just
before the call (empty until then).
`segment_frames` is NOT a quality control: it changes the model's context, and
HTDemucs is trained at its own 343,980-frame segment.

The sample clip's **Split to stems** submenu (Q050-062.2,
`StemSplitController::addSplitToStemsMenu`) has one item per quality and passes
its name to `stem.job_start`. The progress label and completion message name
the job's own `inference_quality` (else `quality`), and the source clip is not
touched: `stem.tracks_create` only adds tracks.

Measured on the real model (Q050-062.3): against `standard`, `fast` loses
0.11 dB mean SDR at about 0.64x the time and `high` gains 0.045 dB at about
2.0x the time; see "End-to-end §5" for the reference set, the per-stem figures
and the run conditions.

## The ONNX gate: both configure modes

There is no system-wide ONNX Runtime on this machine, so the integration is
optional behind `find_package(ONNXRuntime)` (`cmake/modules/FindONNXRuntime.cmake`).

**A. Without the runtime** — clean-room configure:

```
$ cmake -S . -B build-nort -DCMAKE_BUILD_TYPE=Release -DWANT_QT6=ON -DWANT_STEM_SPLIT=ON
-- Stem separation: ONNX Runtime not found - C++ inference backend disabled, external-process backend enabled
NORT_CONFIGURE_EXIT=0
```

**B. With the runtime** — same tree, real SDK:

```
$ cmake -S . -B build-ort -DCMAKE_BUILD_TYPE=Release -DWANT_QT6=ON -DWANT_STEM_SPLIT=ON -DONNXRUNTIME_ROOT=/tmp/ort-sdk-real
-- Stem separation: ONNX Runtime 1.28.0 found (/tmp/ort-sdk-real/lib/libonnxruntime.so) - C++ inference backend enabled
ORT_CONFIGURE_EXIT=0
```

The gate is decided solely by `find_package`; both modes configure, build and
pass tests. `ctest -N` shows the gate's effect on the test set: **11 tests
without** the runtime, **12 with** it (`OnnxRuntimeStemSeparatorTest` is only
registered in ORT mode).

## Tests (from the build dirs named, not from any other tree)

`build-nort/tests` — no ONNX Runtime, 11/11 passed, 0 failed, 8.93 s:
`ArrayVectorTest`, `AudioBufferTest`, `AutomatableModelTest`, `MathTest`,
`ProjectVersionTest`, `RelativePathsTest`, `TimelineTest`,
`AutomationTrackTest`, **`StemJobManagerTest`**, **`StemModelStoreTest`**,
**`StemSplitPipelineTest`**.

`build-ort/tests` — ONNX Runtime present, 12/12 passed, 0 failed, 9.03 s: the
same 11 plus **`OnnxRuntimeStemSeparatorTest`**.

## End-to-end

### 1. Job manager → 4 outputs, progress observed (`build-nort/tests`)

`StemSplitPipelineTest::testJobManagerRunsTheRealBackend` submits through
`StemJobManager`, counts `jobProgress` emissions with a `QSignalSpy`, and
asserts all four stems reconstruct the mix. Verbatim:

```
QINFO  : StemSplitPipelineTest::testSeparatorProducesFourStemsWithProgress() separator wall time: 321 ms for 44100 frames (1 s at 44100 Hz)
QINFO  : StemSplitPipelineTest::testJobManagerRunsTheRealBackend() job wall time: 358 ms
QINFO  : StemSplitPipelineTest::testJobManagerRunsTheRealBackend() max |sum(stems) - mix| = 2.980e-08
Totals: 7 passed, 0 failed, 0 skipped, 0 blacklisted, 4904ms
```

**Inference path that actually ran:** external process → `/usr/bin/python3`
(Python `onnxruntime` **1.28.0**, `CPUExecutionProvider`) →
`tools/stem_split_cli.py` → `tests/data/stub-4stem-linear.onnx`.
`ExternalProcessStemSeparator::locatePython()` prefers `/usr/bin/python3`
first, then `python3`, then `/usr/local/bin/python3` — on this machine the
first candidate is the one with onnxruntime 1.28.0.

### 2. In-process C++ ONNX Runtime (`build-ort/tests`)

```
QINFO  : OnnxRuntimeStemSeparatorTest::testRuntimeReportsItself() ONNX Runtime 1.28.0
PASS   : OnnxRuntimeStemSeparatorTest::testSeparatesWithTheStubModel()
PASS   : OnnxRuntimeStemSeparatorTest::testCancelIsObserved()
Totals: 7 passed, 0 failed, 0 skipped, 0 blacklisted, 65ms
```

**Inference path:** in-process C++ `Ort::Session` linked against
`libonnxruntime.so.1.28.0`, `CPUExecutionProvider`, same stub model. This test
proves the C++ backend compiles, links, loads a model, separates, and honours
cancellation — it had never been compiled against a real ORT SDK before.

### 3. Standalone CLI (the exact program the external backend drives), 3 s stereo

```
$ /usr/bin/python3 tools/stem_split_cli.py --model tests/data/stub-4stem-linear.onnx \
    --input /tmp/mix3s-final.f32 --in-format f32 --in-rate 44100 \
    --out-dir /tmp/stems-out-final --out-format f32 --segment 16384 --progress-json
{"type": "info", "onnxruntime": "1.28.0", "providers": ["AzureExecutionProvider", "CPUExecutionProvider"], "model": ".../stub-4stem-linear.onnx", "segment": 16384, "sample_rate": 44100, "frames": 132300, "chunks": 20}
{"type": "progress", "fraction": 0.05}   ... 20 events, 0.05 → 1.0 ...
{"type": "done", "stems": {"drums": ".../drums.f32", "bass": ".../bass.f32", "other": ".../other.f32", "vocals": ".../vocals.f32"}, "chunks": 20, "elapsed_s": 0.127579, "audio_s": 3.0, "rtf": 0.042526}
CLI_EXIT=0
WALL_SECONDS=0.450210113
```

Four outputs written, 1,058,400 bytes each (132,300 frames × 2 ch × 4 B).
Independent reconstruction check with numpy:

```
max |sum(stems)-mix| = 2.9802322387695312e-08
per-stem gains vs mix: drums=0.400, bass=0.300, other=0.200, vocals=0.100
```

**Wall time, plainly:** 358 ms wall for 1 s of audio through the job manager;
450 ms wall for a 3 s file through the CLI (CLI's own timer: 127.6 ms of
separation, rtf 0.0425). These are **stub-model** numbers on this machine and
say nothing about HTDemucs throughput.

### 4. Real-model benchmark — the only throughput figures here (Q050-131, 2026-10-17/18/19)

Sections 1–3 above are stub-model only. This section is the **real pinned
HTDemucs fp16 model** on real reference audio, and every number is produced by
the command printed next to it. It does NOT measure separation *quality*; the
SNR figure in "Model handling" is that pipeline's self-check. The canonical
table below is the 2026-10-19 re-measurement (every cell captured); the
2026-10-18 and 2026-10-17 measurements are kept in the same table for
reproducibility across days and load conditions.

**What ran, end to end:**

- **Model:** the pinned spec (`StemModelStore::defaultModelSpec`, owner decision
  14) — commit `d54ed9eb60e258ea82131c6ee14578628816456a` of
  `StemSplitio/htdemucs-onnx`. Downloaded from that pinned commit with
  `curl -L`, size **165,612,636 bytes**, sha256 verified **before** each
  measurement day (`sha256sum`, exit 0):
  `d05c269d0178d2a72ad484b10b11dd370193fc923201c3b27a99f848745db70a`.
- **Inference path:** the exact path the GUI drives today —
  `/usr/bin/python3` (3.14.8) with `onnxruntime` **1.28.0**
  (`CPUExecutionProvider`) and numpy 2.5.4 running `tools/stem_split_cli.py`
  (`ExternalProcessStemSeparator::locatePython()` prefers exactly this).
- **Reference audio:** deterministic synthetic 30.0 s stereo 44.1 kHz float32
  mix — 1,323,000 frames, 10,584,000 bytes (110 Hz sine + 220 Hz sine +
  seeded gaussian noise, PCG64 seed 20261017, R delayed 100 frames), sha256
  `548aab6c6fc07424d9ed752a084eb18a9bde7a37a2d004d11be8b9c6eb47d2ad`.
  Generator (exact command; regenerate anywhere and hash-compare against the
  sha256 above before using the figures; regenerating it on 2026-10-18 and
  2026-10-19 reproduced the same sha256 byte-for-byte, exit 0):

  ```
  $ python3 -c '
  import numpy as np
  SR, DUR = 44100, 30
  frames = SR * DUR
  t = (np.arange(frames, dtype=np.int64) / float(SR)).astype(np.float64)
  rng = np.random.default_rng(20261017)
  mix = (0.4 * np.sin(2 * np.pi * 110.0 * t)
         + 0.3 * np.sin(2 * np.pi * 220.0 * t)
         + 0.3 * rng.standard_normal(frames)).astype(np.float32)
  inter = np.empty(frames * 2, dtype="<f4")
  inter[0::2] = mix
  inter[1::2] = np.concatenate([np.zeros(100, dtype=np.float32), mix[:-100]])
  inter.tofile("ref-mix-30s.f32")'
  ```
- **Hardware:** Intel Core Ultra 7 255HX — 20 cores / 1 thread per core
  (`lscpu`: byte order little-endian, NUMA 1); 30.7 GiB RAM (`free -b`);
  kernel `7.2.9-2-cachyos`; CPU max/min MHz as reported 1700/800.
- **Load condition, honestly:** the box was NOT idle — other program lanes
  were compiling and running tests during every run (1-min loadavg recorded
  per run below). Treat these figures as **upper bounds** for this machine;
  an idle run can only be faster.

**Per-run command** (workdir = the stem-split worktree; `--out-dir` per run;
the model path is a scratch file, not the store):

```
$ /usr/bin/python3 tools/stem_split_cli.py \
    --model htdemucs_fp16weights.onnx \
    --input ref-mix-30s.f32 --in-format f32 --in-rate 44100 \
    --out-dir stems-out-N --out-format f32 --progress-json
{"type": "info", "onnxruntime": "1.28.0", "providers": [...], "segment": 343980, "sample_rate": 44100, "frames": 1323000, "chunks": 11}
{"type": "done", ...}
```

(343,980 = the model's fixed input segment, ≈7.8 s at 44100 Hz.) `elapsed_s`
and `rtf` are the CLI's own `perf_counter` timers over the 11 chunks; the
process wall column is `date +%s.%N` taken around the whole process and
subtracted in awk; loadavg is `uptime` read immediately before and after each
process. Every cell below was captured on the run it describes. (The
2026-10-17 run-1 wall cell says "(not captured)" because that day's timing
helper failed on that one invocation; the CLI's own timer was unaffected.)

| run | date | loadavg 1/5/15 before → after | chunks | CLI elapsed_s | **RTF** | process wall |
|---|---|---|---|---|---|---|
| 1 | 2026-10-19 | 7.48 7.57 4.66 → 19.28 10.63 5.84 | 11 | 38.865 | **1.296** | 47.253 s |
| 2 | 2026-10-19 | 17.06 10.56 5.90 → 21.20 12.45 6.75 | 11 | 37.441 | **1.248** | 45.864 s |
| 3 | 2026-10-19 | 18.94 12.25 6.75 → 21.34 13.58 7.42 | 11 | 33.988 | **1.133** | 42.200 s |
| 1 | 2026-10-18 | 30.89 27.17 20.51 → 46.21 32.57 22.90 | 11 | 64.414 | **2.147** | 75.443 s |
| 2 | 2026-10-18 | 46.21 32.57 22.90 → 50.01 36.65 25.07 | 11 | 65.382 | **2.179** | 75.761 s |
| 3 | 2026-10-18 | 50.01 36.65 25.07 → 48.35 38.95 26.76 | 11 | 67.381 | **2.246** | 76.702 s |
| 1 (cold) | 2026-10-17 | 15.09 14.94 11.16 → 45.02 23.18 14.23 | 11 | 55.836 | **1.861** | (not captured) |
| 2 (warm) | 2026-10-17 | 39.71 22.77 14.19 → 56.69 31.01 17.68 | 11 | 64.735 | **2.158** | 73.695 s |
| 3 (warm) | 2026-10-17 | 17.74 24.06 16.97 → 47.15 31.49 20.06 | 11 | 59.831 | **1.994** | 69.183 s |

(The "after" figure is the loadavg read the moment the run exited. The
2026-10-17/18 runs are contended — load kept spiking past 45/50 during each
run; the 2026-10-19 runs ran on a lightly loaded box.)

**Plainly:** a 30 s clip separates in **42–47 s of wall time (RTF ≈ 1.13–1.30)**
on this 20-core box when it is lightly loaded (2026-10-19, 1-min loadavg
7–21), and **60–77 s (RTF ≈ 1.86–2.25)** under the heavy concurrent load other
lanes produced on 2026-10-17/18 (loadavg up to 50) — i.e. roughly **1.1–2.3×
the clip length at CPU real time**, plus one fixed ~8–11 s of model/session
load per new CLI process (47.3 s wall vs 38.9 s separation in the 2026-10-19
run 1). Equivalently ≈ 0.4–0.9 audio-minutes per wall-minute. Users should
expect about a minute for a 30 s clip on an uncongested modern CPU and minutes
for a full song; never real time — the 7.8 s receptive field forbids that
anyway.

**Output sanity (run 3 each day, verified with numpy):** four files, exactly
10,584,000 bytes each (1,323,000 frames × 2 ch × 4 B), every sample finite;
per-stem RMS: drums 0.221, bass 0.231, other 0.257, vocals 0.005 — identical
on all three days (the pipeline is deterministic for a pinned model and
input). The mix contains no vocals, so the model routes nearly nothing there.
This is sanity of the I/O path, not a quality claim.

### 5. Real-model quality per setting: SDR and time (Q050-062.3, 2026-10-27)

Every quality setting (Q050-062.1: the segment overlap) measured on the
**real pinned HTDemucs model** against reference stems known sample for sample.
The figures come from one run of `tools/stem_quality_bench.py`. Its full JSON
record, with every row, hash and run condition, is committed unedited (as one
fenced block) in `docs/050-features/Q050-062.3-bench.md`.

**Reference set (fixed, hash-pinned):** four tracks of the MUSDB18-HQ **test**
split (Zenodo record 3338373, `musdb18hq.zip`, 22,656,664,047 bytes; HTDemucs
was not trained on the test split): *Al James - Schoolboy Facination*, *Angels
In Amplifiers - I'm Alright*, *Hollow Ground - Ill Fate*, *Tom McKenzie -
Directions*. Each excerpt is 60.0 s to 90.0 s of the track (1,323,000 frames, stereo,
44.1 kHz). `fetch` reads only those tracks' stem WAVs out of the zip, using HTTP range
requests (zipfile checks each member's CRC). It writes the four reference
stems as float32, and writes the mix as **their sum** (computed in float64, stored as float32). Mix and
references are therefore aligned by construction; the official
`mixture.wav` is not used. Every stem in every excerpt is active (RMS 0.037 to
0.106). The sha256 of all 20 excerpt files is pinned in the tool
(`REFERENCE_SHA256`). `fetch` refuses any file that does not match, and a
second fetch into an empty directory reproduced all 20 byte for byte (see the
unit's report). The audio is research-licensed and is not committed.

**Model:** the pinned spec (owner decision 14), `htdemucs_fp16weights.onnx`,
165,612,636 bytes, sha256
`d05c269d0178d2a72ad484b10b11dd370193fc923201c3b27a99f848745db70a` (the
record hashes it at run time). **Inference path:** the GUI's own,
`/usr/bin/python3` 3.14.8 running `tools/stem_split_cli.py` (sha256
`7705c993ff56134c3cb5f74128338f3256d6db35d9be24d2e299c51de54be187`) with
onnxruntime 1.28.0 `CPUExecutionProvider` and numpy 2.5.4.

**SDR:** the global SDR of the Music Demixing challenges (the "nSDR" Demucs
reports). It is computed per stem and excerpt over both channels:
`10 log10((Σ ref² + 1e-7) / (Σ (ref − est)² + 1e-7))`. Higher is better. It is not
BSSEval v4 (museval), and the two scales are not comparable.

**Commands** (the workdir is the repo; `$R` is a scratch directory):

```
$ python3 tools/stem_quality_bench.py fetch --out $R/refset
$ /usr/bin/python3 tools/stem_quality_bench.py run --refset $R/refset \
    --model htdemucs_fp16weights.onnx --work $R/work \
    --out Q050-062.3-bench.json --repeats 2
```

`run` separates every excerpt at `fast`, `standard` and `high`, interleaving
the settings within each excerpt so that load drift hits all three alike. It
makes two full passes: 24 CLI processes, 2026-10-27 22:21 to 23:13 UTC.

**Run conditions, plainly:** Intel Core Ultra 7 255HX (20 logical CPUs, all
in the affinity mask), 30.7 GiB RAM, kernel 7.3.0-4-cachyos, no
`OMP_NUM_THREADS`. The run was inside the fix loop's systemd cgroup, whose
**CPU quota is 4 CPUs** (`cpu.max` = `400000 100000`). Other lanes in the same
cgroup were building and testing throughout (1-min loadavg 1.2 to 22.5 per
row). The absolute times are therefore **contended and slower than §4**: `standard`
here runs at RTF 2.7 to 4.4, against 1.1 to 1.3 in §4's lightly loaded run. Two
figures do not depend on load: the pass count, which is deterministic, and the
per-run ratio against `standard` on the same excerpt in the same pass.
Use those to compare settings, and §4 for absolute speed.

**Result: mean over the four excerpts** (each SDR is the mean of both
passes; the passes were identical, see below):

| quality | overlap | passes per 30 s excerpt | SDR drums | bass | other | vocals | **mean SDR** | vs standard | CLI time, all runs | time vs standard (median of per-run ratios) |
|---|---|---|---|---|---|---|---|---|---|---|
| `fast` | 25 % | 8 | 8.968 | 7.595 | 4.809 | 9.080 | **7.613 dB** | -0.111 dB | 566.0 s (8 runs) | 0.64x (range 0.44-1.10) |
| `standard` | 50 % | 11 | 9.034 | 7.699 | 4.967 | 9.193 | **7.723 dB** | +0.000 dB | 817.4 s (8 runs) | 1.00x (range 1.00-1.00) |
| `high` | 75 % | 22 | 9.066 | 7.758 | 5.012 | 9.236 | **7.768 dB** | +0.045 dB | 1522.8 s (8 runs) | 2.01x (range 1.44-2.31) |

`passes` is the number of model runs the CLI makes over a 30 s excerpt. It
follows the Q050-062.1 hop formula: 8 / 11 / 22. "time vs standard" is the
median of the eight per-run ratios `elapsed_s(setting) / elapsed_s(standard)`
on the same excerpt in the same pass. The pass ratio alone predicts
0.73x and 2.0x.

**Plainly:**
- **`high`** is better on average, by **+0.045 dB**, for about **2x** the time.
  It is not better on every excerpt: on *Tom McKenzie* it scores 0.012 dB
  below `standard`.
- **`fast`** costs **0.11 dB** for about **0.64x** the time. The cost is spread
  unevenly: 0.02 dB on two excerpts, 0.17 and 0.23 dB on the other two.
- The differences between settings are small next to the differences between
  excerpts (5.6 to 9.5 dB mean SDR). The setting trades speed for a small,
  consistent averaging gain; it does not change what the model can separate.

**Per excerpt** (identical in both passes, which shows the pipeline is
deterministic for a pinned model and input):

| excerpt | quality | SDR drums | bass | other | vocals | mean | repeats identical |
|---|---|---|---|---|---|---|---|
| Al_James___Schoolboy_Facination | `fast` | 7.148 | 10.159 | 1.962 | 7.486 | 6.689 | yes (2) |
| Al_James___Schoolboy_Facination | `standard` | 7.101 | 10.222 | 2.261 | 7.856 | 6.860 | yes (2) |
| Al_James___Schoolboy_Facination | `high` | 7.192 | 10.390 | 2.419 | 7.994 | 6.998 | yes (2) |
| Angels_In_Amplifiers___I_m_Alright | `fast` | 8.020 | 10.230 | 9.048 | 10.485 | 9.446 | yes (2) |
| Angels_In_Amplifiers___I_m_Alright | `standard` | 7.982 | 10.299 | 9.080 | 10.511 | 9.468 | yes (2) |
| Angels_In_Amplifiers___I_m_Alright | `high` | 8.046 | 10.334 | 9.104 | 10.533 | 9.504 | yes (2) |
| Hollow_Ground___Ill_Fate | `fast` | 6.444 | 2.794 | 5.675 | 7.548 | 5.615 | yes (2) |
| Hollow_Ground___Ill_Fate | `standard` | 6.531 | 2.784 | 5.684 | 7.547 | 5.636 | yes (2) |
| Hollow_Ground___Ill_Fate | `high` | 6.564 | 2.788 | 5.688 | 7.567 | 5.652 | yes (2) |
| Tom_McKenzie___Directions | `fast` | 14.258 | 7.197 | 2.549 | 10.803 | 8.701 | yes (2) |
| Tom_McKenzie___Directions | `standard` | 14.523 | 7.492 | 2.844 | 10.860 | 8.930 | yes (2) |
| Tom_McKenzie___Directions | `high` | 14.462 | 7.520 | 2.837 | 10.851 | 8.918 | yes (2) |

**Every run** (`elapsed_s` and `rtf` are the CLI's own `perf_counter` timers
over the separation. `process wall` also includes the 6 to 15 s of model and session
load per process. Loadavg is read immediately before and after each process):

| repeat | excerpt | quality | hop | passes | CLI elapsed_s | RTF | process wall s | loadavg before → after |
|---|---|---|---|---|---|---|---|---|
| 1 | Al_James___Schoolboy_Facination | `fast` | 257985 | 8 | 62.064 | 2.069 | 68.659 | 2.67 8.09 10.89 → 1.87 6.7 10.2 |
| 1 | Al_James___Schoolboy_Facination | `standard` | 171990 | 11 | 107.856 | 3.595 | 115.418 | 1.87 6.7 10.2 → 1.34 4.98 9.16 |
| 1 | Al_James___Schoolboy_Facination | `high` | 85995 | 22 | 218.233 | 7.274 | 225.836 | 1.34 4.98 9.16 → 13.51 10.92 10.62 |
| 1 | Angels_In_Amplifiers___I_m_Alright | `fast` | 257985 | 8 | 73.874 | 2.462 | 80.733 | 13.51 10.92 10.62 → 3.99 8.44 9.77 |
| 1 | Angels_In_Amplifiers___I_m_Alright | `standard` | 171990 | 11 | 79.814 | 2.660 | 87.193 | 3.99 8.44 9.77 → 1.62 6.58 9.0 |
| 1 | Angels_In_Amplifiers___I_m_Alright | `high` | 85995 | 22 | 167.669 | 5.589 | 174.486 | 1.62 6.58 9.0 → 1.55 4.27 7.68 |
| 1 | Hollow_Ground___Ill_Fate | `fast` | 257985 | 8 | 57.882 | 1.929 | 64.629 | 1.55 4.27 7.68 → 18.73 8.79 9.01 |
| 1 | Hollow_Ground___Ill_Fate | `standard` | 171990 | 11 | 130.672 | 4.356 | 138.317 | 18.73 8.79 9.01 → 8.97 10.04 9.57 |
| 1 | Hollow_Ground___Ill_Fate | `high` | 85995 | 22 | 188.306 | 6.277 | 198.173 | 8.97 10.04 9.57 → 2.04 6.17 8.14 |
| 1 | Tom_McKenzie___Directions | `fast` | 257985 | 8 | 60.936 | 2.031 | 67.935 | 2.04 6.17 8.14 → 1.8 5.24 7.67 |
| 1 | Tom_McKenzie___Directions | `standard` | 171990 | 11 | 94.032 | 3.134 | 100.766 | 1.8 5.24 7.67 → 18.21 9.27 8.81 |
| 1 | Tom_McKenzie___Directions | `high` | 85995 | 22 | 135.628 | 4.521 | 142.502 | 18.21 9.27 8.81 → 4.41 8.23 8.6 |
| 2 | Al_James___Schoolboy_Facination | `fast` | 257985 | 8 | 52.535 | 1.751 | 59.268 | 4.41 8.23 8.6 → 2.6 7.1 8.19 |
| 2 | Al_James___Schoolboy_Facination | `standard` | 171990 | 11 | 82.585 | 2.753 | 89.197 | 2.6 7.1 8.19 → 1.15 5.5 7.52 |
| 2 | Al_James___Schoolboy_Facination | `high` | 85995 | 22 | 190.746 | 6.358 | 198.526 | 1.15 5.5 7.52 → 17.79 8.04 7.88 |
| 2 | Angels_In_Amplifiers___I_m_Alright | `fast` | 257985 | 8 | 54.303 | 1.810 | 60.797 | 17.79 8.04 7.88 → 17.63 9.98 8.57 |
| 2 | Angels_In_Amplifiers___I_m_Alright | `standard` | 171990 | 11 | 117.100 | 3.903 | 132.224 | 17.63 9.98 8.57 → 2.46 6.66 7.53 |
| 2 | Angels_In_Amplifiers___I_m_Alright | `high` | 85995 | 22 | 209.205 | 6.974 | 220.192 | 2.46 6.66 7.53 → 1.43 3.83 6.19 |
| 2 | Hollow_Ground___Ill_Fate | `fast` | 257985 | 8 | 92.409 | 3.080 | 103.498 | 1.43 3.83 6.19 → 22.48 9.91 8.07 |
| 2 | Hollow_Ground___Ill_Fate | `standard` | 171990 | 11 | 103.856 | 3.462 | 115.212 | 22.48 9.91 8.07 → 8.98 9.69 8.26 |
| 2 | Hollow_Ground___Ill_Fate | `high` | 85995 | 22 | 211.314 | 7.044 | 223.081 | 8.98 9.69 8.26 → 1.15 5.33 6.82 |
| 2 | Tom_McKenzie___Directions | `fast` | 257985 | 8 | 112.046 | 3.735 | 119.872 | 1.15 5.33 6.82 → 19.01 8.41 7.62 |
| 2 | Tom_McKenzie___Directions | `standard` | 171990 | 11 | 101.468 | 3.382 | 112.389 | 19.01 8.41 7.62 → 14.65 11.62 8.98 |
| 2 | Tom_McKenzie___Directions | `high` | 85995 | 22 | 201.706 | 6.724 | 212.217 | 14.65 11.62 8.98 → 1.88 6.61 7.51 |

## Model handling

- Models are never bundled. `StemModelStore` downloads HTTPS-only, refuses
  unpinned specs, and verifies SHA-256 + size before use.
- **Pinned since owner decision 14 (2026-09-29)**: HTDemucs fp16, MIT, from the
  model card https://huggingface.co/StemSplitio/htdemucs-onnx, pinned to commit
  `d54ed9eb60e258ea82131c6ee14578628816456a` (`htdemucs_fp16weights.onnx`,
  165612636 bytes, sha256
  `d05c269d0178d2a72ad484b10b11dd370193fc923201c3b27a99f848745db70a`, which is also
  its Hugging Face LFS oid). Measured by download and `sha256sum`, not copied
  from a card. Its input is `mix float32 [1,2,343980]` and its ONE output is
  `stems float32 [1,4,2,343980]` (drums, bass, other, vocals). Both backends take
  that stacked layout as well as four named outputs.
- **Fetched on first use:** when a job starts with the model absent and the
  spec pinned, the job manager's worker fetches the model before separating. It
  streams to `<name>.onnx.part`, verifies it, then renames it into place. The
  job reports `fetching_model` and `download_progress`, and a cancel aborts the
  transfer. Nothing is fetched when `LMMS_STEM_MODEL` names a file (that path is
  the operator's) or when `LMMS_STEM_OFFLINE` is set; offline mode also refuses
  `stem.model_download`.
- **Measured on the real model** (this box, CPU, while a build ran): a 10 s
  synthetic stereo mix (110/220 Hz sines plus a square wave) through
  `tools/stem_split_cli.py`. It took 44.8 s wall, 6 chunks, and every stem was
  finite. Bass carried the energy (RMS 0.219 of the mix's 0.224), and the four
  stems sum back to the mix at 33.2 dB SNR. That is a sanity check of the
  pipeline, not a separation-quality figure.
- Tests use `tests/data/stub-4stem-linear.onnx` — 458 B, sha256
  `feda86ca23ddda75dc67c2ccbd73a346e5c4ad33afb14bd9d0f9019848a1976d`, a linear
  4-way split (drums 0.4, bass 0.3, other 0.2, vocals 0.1) built by
  `tools/make_stub_model.py` from the Python standard library. It exercises the
  real ORT execution path, the real model contract (`float32 [1,2,T]` in, four
  `float32 [1,2,T]` outputs named drums/bass/other/vocals) and the real
  overlap-add segmentation, while making correctness exactly checkable (the
  gains sum to 1, so the stems must reconstruct the mix).
- `tests/data/stub-4stem-stacked.onnx` (189 B, sha256 `ee2d30cf3c9d8d9e95911d3e4df659b8b145d3dbd1162ad0bd8682cb5522a9ad`, built by
  `tools/make_stub_model.py --stacked`) uses the same gains in the pinned
  model's stacked `[1,4,2,T]` layout, as one broadcast `Mul`.
  `StemSplitPipelineTest` requires that it and the four-output stub split a mix
  into bit-identical stems.

## Audio-thread audit

```
$ grep -rn -E 'StemJobManager|StemSplitController|StemSeparator|StemModelStore|StemTrackBuilder' \
    src/core/audio/ src/core/AudioEngine.cpp include/AudioEngine.h
(no matches)
```

The manager's `QMutex` is only ever taken on its own worker thread and the
caller's thread (GUI); the audio callback path has no stem symbols, no
allocation and no lock to take. `StemTrackBuilder` mutates the Song only from
the GUI thread when a job finishes. Real time remains forbidden by design:
background job only, ~7.8 s receptive field.

## Defects fixed on top of the inherited WIP

`b54cf2aff` — "stem-split: fix compile and runtime defects found by the first
full build/test run":

- `OnnxRuntimeStemSeparator.cpp`: `mix[i]` does not compile — `SampleBuffer` has
  no `operator[]`; now uses `mix.data()[i]`. The in-process backend had never
  been compiled against a real ORT SDK.
- `OnnxRuntimeStemSeparatorTest.cpp`: called a non-existent
  `isRuntimeAvailable()`; the progress lambda mixed `QVERIFY`'s bare `return`
  with `return true` (inconsistent lambda return type).
- `StemTrackBuilder.cpp`: `Track` and `Clip` constructors already register
  themselves (`Track.cpp:68`, `Clip.cpp:86`), so the extra `addTrack()` /
  `addClip()` listed every stem twice — 8 tracks instead of 4, and a double-free
  SIGSEGV at teardown.
- `StemJobManager.cpp`: unknown job ids returned `Failed`; now `Queued`
  ("not started" — also keeps GUI polling of a just-submitted id race-free).
- `StemModelStore.cpp`: checksum-mismatch message now contains "checksum".
- `src/gui/CMakeLists.txt`: the controller header is listed explicitly so
  AUTOMOC's dependency on `include/StemSplitController.h` is bookkeeping, not
  luck (this is the "missing header" the inherited WIP commit message warned
  about; the file exists, the dependency was implicit).

`210099bb6` — "chore: stop tracking build-ort-present/ and __pycache__":
removed 2727 tracked build-tree files (including a synthetic stub SDK) and
compiled bytecode from the index; `.gitignore` now covers `/build*/` and Python
caches.

## Build directories

Results above come from **`build-nort/`** (no ONNX Runtime) and **`build-ort/`**
(ONNX Runtime present) only. The inherited **`build/`** and
**`build-ort-present/`** directories were **stale pre-fix trees** — ctest in the
inherited `build/` fails `StemJobManagerTest`, `StemModelStoreTest` and
`StemSplitTest` (SEGFAULT) because it predates the fixes — so both were
**deleted** to leave no contradictory tree on disk. To reproduce, configure
`build-nort` and `build-ort` with the commands in "Gate summary".

## Not verified

1. **Real HTDemucs model: quality.** SDR per quality setting is measured on
   four 30 s MUSDB18-HQ test excerpts ("End-to-end §5", Q050-062.3), and
   throughput in "End-to-end §4". Not measured: listening tests, BSSEval v4
   (museval) figures, full-length songs, and the in-process C++ backend's SDR.
   That backend shares the hop formula, but no real-model run of it was made.
2. **GUI interaction.** `StemSplitController` and `SampleClipView` compile and
   their logic is unit-tested at manager/builder level, but no interactive LMMS
   session was run (headless machine); the progress dialog and cancel button
   were never clicked.
3. **In-process ORT backend through the GUI / job manager.**
   `StemSplitController` still selects `ExternalProcessStemSeparator`
   unconditionally (`StemSplitController.cpp:51`); the C++ backend is compiled,
   linked and unit-tested, but not selectable from the GUI, and
   `StemSplitPipelineTest` exercises the external-process backend only.
4. **Cancel from the GUI.** Cancellation is proven in the separator and job
   manager tests, not from the GUI dialog.
5. **Long audio.** Longest input exercised: the 30 s benchmark clip
   ("End-to-end §4"), then 3 s (CLI) and 1 s (job manager). No real song, no
   3-minute file. The benchmark's 11 chunks at the model's fixed 343,980-frame
   segment bound the same overlap-add path for longer input.
6. **WAV path.** `--in-format wav` exists but no test or run here used it; all
   runs used raw interleaved f32.
7. **Platforms.** Linux x64 only; Windows/macOS untested.
8. **Model download path.** `StemModelStore` download/HTTPS/checksum logic is
   unit-tested with synthetic specs; no real model download was performed.
