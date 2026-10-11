# Cluster new-p9-571

Verdict: **FIXED**

## Finding

At `plugins/AudioFileProcessor/AudioFileProcessorWaveView.cpp:415`, a negative pixel-derived offset was converted to unsigned `f_cnt_t` before being passed to `slideSamplePointByFrames`, whose parameter is `long`. The calculation now converts directly to `long`, preserving the signed offset through the call.

The regression at `tests/src/core/AudioFileProcessorWaveViewTest.cpp:35` creates an 8,000,000-frame sample and drags its start marker one pixel left through Qt mouse events. The test target compiles the real waveform-view implementation. On GCC/Clang, `float-cast-overflow` instrumentation makes the previous negative-double-to-unsigned conversion fail deterministically; the test also checks the marker model receives the expected negative frame delta. The test is registered as `AudioFileProcessorWaveViewTest` in CMake and in the source manifests.

## Evidence

All build and test output was captured under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-571.r2/logs`. CTest ran from `build/tests`.

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| RED before product fix | `ctest -R '^AudioFileProcessorWaveViewTest$' --output-on-failure` | 8 | `test-2.log`: 1/1 failed at the old unsigned conversion (`-32653.1` out of range). |
| GREEN after product fix | `ctest -R '^AudioFileProcessorWaveViewTest$' --output-on-failure` | 0 | `test-3.log`: 1/1 passed. |
| Reverse product patch | `git diff -- plugins/AudioFileProcessor/AudioFileProcessorWaveView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-571.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-571.r2/logs/fix.patch` | 0 | `patch-1.log`; only the product-file change was reversed. |
| RED with reversed patch | `ctest -R '^AudioFileProcessorWaveViewTest$' --output-on-failure` | 8 | `test-4.log`: 1/1 failed again at the old unsigned conversion. |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-571.r2/logs/fix.patch` | 0 | `patch-2.log`. |
| GREEN after reapply | `ctest -R '^AudioFileProcessorWaveViewTest$' --output-on-failure` | 0 | `test-5.log`: 1/1 passed. |
| Build app for RED/GREEN and reversal runs | `cmake --build build --target zene` | 0 | `build-3.log`, `build-6.log`, `build-8.log`, and `build-10.log`. |
| Build app and plugin | `cmake --build build --target zene audiofileprocessor` | 0 | `build-12.log`. |
| Build test target | `cmake --build . --target AudioFileProcessorWaveViewTest` (from `build/tests`) | 0 | `build-4.log`, `build-5.log`, `build-7.log`, `build-9.log`, and `build-11.log`. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-3.log`. |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-4.log`. |

The affected CTest family contains one registered test; each valid RED/GREEN run executed 1 test. Both required gates passed. `git diff --check` also exited 0.

The initial target lookup `cmake --build build --target zene AudioFileProcessorWaveViewTest` exited 2 because the test target is exposed from `build/tests`; subsequent builds used that tree. The first test attempt (`test-1.log`, exit 8) hit an unrelated paint-time playhead underflow because its viewport began above frame zero. The fixture was adjusted to begin its viewport at zero while keeping the marker draggable; only `test-2.log` is counted as the before-fix RED evidence.

## Round 3 QA rework

Luna's round 2 review found that the test's sanitizer-specific `ENVIRONMENT` property replaced the shared offscreen setting. `tests/CMakeLists.txt` now sets both `QT_QPA_PLATFORM=offscreen` and `UBSAN_OPTIONS=halt_on_error=1` for `AudioFileProcessorWaveViewTest`. Rebuilt the test target and ran CTest with both `DISPLAY` and `QT_QPA_PLATFORM` removed from the caller environment; CTest supplied its own offscreen setting and passed the single registered test.

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Rebuild test target (from `build/tests`) | `cmake --build . --target AudioFileProcessorWaveViewTest` | 0 | `build-1.log`. |
| Headless CTest (from `build/tests`) | `env -u DISPLAY -u QT_QPA_PLATFORM ctest -R '^AudioFileProcessorWaveViewTest$' --output-on-failure` | 0 | `test-1.log`: 1/1 test passed; caller had no `DISPLAY` or `QT_QPA_PLATFORM`. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log`. |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log`. |

All round 3 build, test and gate logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-571.r3/logs`.

## Files from original fix commit

- `plugins/AudioFileProcessor/AudioFileProcessorWaveView.cpp`
- `tests/src/core/AudioFileProcessorWaveViewTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-571.md`

The round 3 QA correction commit contains:

- `tests/CMakeLists.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-571.md`
