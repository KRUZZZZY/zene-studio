# Stem separation — offline job manager (task #578)

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
say nothing about HTDemucs throughput — see "Not verified".

## Model handling

- Models are never bundled. `StemModelStore` downloads HTTPS-only, refuses
  unpinned specs, and verifies SHA-256 + size before use.
- HTDemucs fp16 (166 MB, MIT) is **intentionally not pinned in v1**: the URL and
  checksum must come from the model card at G3 rather than be guessed
  (`StemModelStore.h`). The model is **not present on this machine** and was
  not used for any result here.
- Tests use `tests/data/stub-4stem-linear.onnx` — 458 B, sha256
  `feda86ca23ddda75dc67c2ccbd73a346e5c4ad33afb14bd9d0f9019848a1976d`, a linear
  4-way split (drums 0.4, bass 0.3, other 0.2, vocals 0.1) built by
  `tools/make_stub_model.py` from the Python standard library. It exercises the
  real ORT execution path, the real model contract (`float32 [1,2,T]` in, four
  `float32 [1,2,T]` outputs named drums/bass/other/vocals) and the real
  overlap-add segmentation, while making correctness exactly checkable (the
  gains sum to 1, so the stems must reconstruct the mix).

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

1. **Real HTDemucs model.** Not present (166 MB, unpinned in v1), so stem
   quality, real-model wall time, memory and RTF are **not measured**. Every
   number here is for the 458 B stub.
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
5. **Long audio.** Longest input exercised: 3 s (CLI) and 1 s (job manager).
   No real song, no 3-minute file.
6. **WAV path.** `--in-format wav` exists but no test or run here used it; all
   runs used raw interleaved f32.
7. **Platforms.** Linux x64 only; Windows/macOS untested.
8. **Model download path.** `StemModelStore` download/HTTPS/checksum logic is
   unit-tested with synthetic specs; no real model download was performed.
