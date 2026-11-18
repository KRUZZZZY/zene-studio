# Cluster new-p8-195: conversion can delete an unrelated `.old` file

Verdict: **FIXED**

`src/core/RenderQueue.cpp:103-124` now moves an existing destination to a UUID-named backup path, avoiding an existing path and the conversion source. It removes that job-created backup only after the staged output is in place, and restores it if the staged rename fails. The conversion no longer deletes `out + ".old"`.

`tests/src/core/TaskCenterTest.cpp:213-237` converts `song.wav.old` to `song.wav` through `RenderQueue::convert()` and checks that the source bytes remain unchanged. On the original code the assertion `preservedSource.open(QIODevice::ReadOnly)` failed. Reversing only the production patch reproduced that failure.

## Verification

The affected ctest family contains one registered test, `TaskCenterTest`. The GREEN ctest result reports 1/1 tests passed. The RED run's QtTest output reported 10 passed and 1 failed.

| Stage | Exact command | Exit |
|---|---|---:|
| Initial build, original production code | `cmake --build build --target zene TaskCenterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-195.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| RED on original code | `ctest -R '^TaskCenterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-195.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 |
| Save and reverse only the production patch | `git diff -- src/core/RenderQueue.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-195.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-195.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Rebuild with the production patch reversed | `cmake --build build --target zene TaskCenterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-195.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| RED with the production patch reversed | `ctest -R '^TaskCenterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-195.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 |
| Restore the production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-195.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Rebuild with the fix | `cmake --build build --target zene TaskCenterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-195.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| GREEN | `ctest -R '^TaskCenterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-195.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-195.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-195.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |

The `fff` grep tool was unavailable in this session. The logs were searched with `rg` for `error`, `FAILED`, and `Assertion`; the RED assertion was also located with `rg` for `FAIL!`.

## Files in this commit

- `src/core/RenderQueue.cpp`
- `tests/src/core/TaskCenterTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-195.md`
