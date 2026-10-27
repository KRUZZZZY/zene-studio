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

Not measured yet: real-model SDR per quality (Q050-062 stage 3). More overlap
averages more predictions per frame, which is how Demucs's own `--overlap`
works, but no figure here claims it separates better.

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

1. **Real HTDemucs model: quality.** Separation *quality* is not measured —
   only the pipeline sanity in "Model handling" and the throughput in
   "End-to-end §4" (Q050-131's benchmark: RTF ≈ 1.13–2.25 on the pinned model,
   2026-10-17/18/19, depending on box load). No subjective/standard quality
   (SDR etc.) figure exists.
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
