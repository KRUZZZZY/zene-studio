# Cluster new-p8-240 fix report

Verdict: **FIXED**

## Evidence

- `src/core/VideoAudioExtract.cpp:85-100` stages the WAV through `QSaveFile`, with direct-write fallback disabled, and gives libsndfile the staging handle.
- `src/core/VideoAudioExtract.cpp:104-118` closes the WAV and commits the replacement only after decode completion.
- `src/core/VideoAudioExtract.cpp:178-186` returns on cancellation or empty output before commit; the uncommitted temporary output is discarded by `QSaveFile`.
- `include/VideoAudioExtract.h:50-60` documents that failed and cancelled runs preserve the destination.
- `tests/src/core/VideoTrackTest.cpp:322-348` verifies cancellation preserves existing bytes, then success replaces them with a RIFF WAV.

## Regression and verification

Affected family: `VideoTrackTest`, one registered CTest case executed (`1/1` passed GREEN; zero tests did not run).

| Phase | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| RED build (patch reversed) | `cmake --build build --target VideoTrackTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-240.r1/logs/build-5.log 2>&1` | 0 | Rebuilt test and app before the RED run. |
| RED test, run from `build/tests` | `ctest -R '^VideoTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-240.r1/logs/test-5.log 2>&1` | 8 | `preserved.open(QIODevice::ReadOnly)` failed after cancellation. |
| GREEN build | `cmake --build build --target VideoTrackTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-240.r1/logs/build-6.log 2>&1` | 0 | Rebuilt test and app with the fix restored. |
| GREEN test, run from `build/tests` | `ctest -R '^VideoTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-240.r1/logs/test-6.log 2>&1` | 0 | `1/1` CTest case passed. |
| Reverse production patch | `git diff -- src/core/VideoAudioExtract.cpp include/VideoAudioExtract.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-240.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-240.r1/logs/fix.patch` | 0 | Applied to prove the regression fails without the production fix. |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-240.r1/logs/fix.patch` | 0 | Restored before the GREEN build and test. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-240.r1/logs/gate-3.log 2>&1` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-240.r1/logs/gate-4.log 2>&1` | 0 | Passed. |

The final reversal RED and restored GREEN are in `test-5.log` and `test-6.log`. The `fff grep` tool was not exposed in this session, so the log folder was searched with `rg` one term at a time; `FAILED` appeared only in the expected RED logs, with no `error` or `Assertion` matches.

## Files in the commit

- `src/core/VideoAudioExtract.cpp`
- `include/VideoAudioExtract.h`
- `tests/src/core/VideoTrackTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-240.md`

The source and test paths were already registered in `tests/fork-sources.txt`; no new source path was added.
