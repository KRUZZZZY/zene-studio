# Cluster new-p9-443

Verdict: **FIXED**

`StereoDelay::setSampleRate()` now recomputes the ring capacity, scales the selected delay length to retain its duration, clamps it to the new capacity, and resets the write cursor after reallocation. The focused regression checks the 48 kHz to 22.05 kHz transition and advances `tick()` through a complete ring wrap.

## Evidence

| Location | Evidence |
| --- | --- |
| `plugins/Delay/StereoDelay.cpp:77` | `setSampleRate()` updates the capacity, length, and write cursor before allocating the new buffer. |
| `tests/src/core/StereoDelay.cpp:21` | Regression changes from 48 kHz to 22.05 kHz, checks duration and cursor state, rejects an out-of-range length, and advances through the new ring. |
| `tests/CMakeLists.txt:709,978` | Registers ctest `StereoDelay` and compiles the production implementation into that test target. |

The RED assertion was `delay.m_maxLength`: **Actual 48000**, expected 22050. The same final test failed with the production patch removed and passed after restoring it.

## Verification commands

All ctest commands below ran with the working directory set to `build/tests`; other commands ran from the worktree root. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs`.

| Purpose | Command | Exit |
| --- | --- | --- |
| Initial regression target build (`build-4.log`) | `cmake --build build/tests --target StereoDelay --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| Initial RED before the production fix (`test-1.log`) | `ctest -R '^StereoDelay$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 |
| Rebuild product with the fix (`build-5.log`) | `cmake --build build --target zene --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| Rebuild regression target with the fix (`build-6.log`) | `cmake --build build/tests --target StereoDelay --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/build-6.log 2>&1; echo EXIT=$?` | 0 |
| Initial GREEN with the fix (`test-2.log`) | `ctest -R '^StereoDelay$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 |
| Remove production patch for the final-test proof | `git diff -- plugins/Delay/StereoDelay.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild product with patch removed (`build-12.log`) | `cmake --build build --target zene --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/build-12.log 2>&1; echo EXIT=$?` | 0 |
| Rebuild regression target with patch removed (`build-13.log`) | `cmake --build build/tests --target StereoDelay --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/build-13.log 2>&1; echo EXIT=$?` | 0 |
| RED with the final test and patch removed (`test-6.log`) | `ctest -R '^StereoDelay$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/test-6.log 2>&1; echo EXIT=$?` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| GREEN with the identical final test (`test-7.log`) | `ctest -R '^StereoDelay$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/test-7.log 2>&1; echo EXIT=$?` | 0 |
| Rebuild product (`build-14.log`) | `cmake --build build --target zene --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/build-14.log 2>&1; echo EXIT=$?` | 0 |
| Rebuild affected test (`build-15.log`) | `cmake --build build/tests --target StereoDelay --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/build-15.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate (`gate-3.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/gate-3.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate (`gate-4.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/gate-4.log 2>&1; echo EXIT=$?` | 0 |

The affected ctest family is `^StereoDelay$`: **1 test executed**. The final GREEN run reported `100% tests passed, 0 tests failed out of 1`.

The initial root-level target lookup was `cmake --build build --target StereoDelay --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/build-1.log 2>&1; echo EXIT=$?` (**EXIT=2**). This Makefile build exposes the test target under `build/tests`; subsequent test-target builds used that directory and passed.

## Evidence summary

| Evidence | Command or result |
| --- | --- |
| RED, final test with fix removed | `ctest -R '^StereoDelay$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/test-6.log 2>&1; echo EXIT=$?` — **EXIT=8** |
| GREEN, same test after restoring fix | `ctest -R '^StereoDelay$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/test-7.log 2>&1; echo EXIT=$?` — **EXIT=0** |
| Affected-family test count | `^StereoDelay$` — **1 test executed** |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/gate-3.log 2>&1; echo EXIT=$?` — **EXIT=0** |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-443.r1/logs/gate-4.log 2>&1; echo EXIT=$?` — **EXIT=0** |
| Files committed | `plugins/Delay/StereoDelay.cpp`; `tests/src/core/StereoDelay.cpp`; `tests/CMakeLists.txt`; `tests/fork-sources.txt`; `tests/upstream-modifications.txt`; `docs/qwen-fixes-2026-10/cluster-new-p9-443.md` |

## Files committed

- `plugins/Delay/StereoDelay.cpp`
- `tests/src/core/StereoDelay.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-443.md`
