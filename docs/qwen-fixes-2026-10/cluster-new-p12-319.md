# Cluster new-p12-319

Verdict: **FIXED**

`AudioPulseAudio::run()` created a PulseAudio mainloop, then returned when `pa_context_new()` returned null without releasing the mainloop. The early-return path now calls `pa_mainloop_free(mainLoop)` before returning.

## File and regression evidence

- `src/core/audio/AudioPulseAudio.cpp:177-182` — the null-context branch now frees `mainLoop` before returning.
- `tests/src/core/AudioEngineTeardownTest.cpp:211-227` — the regression interposes PulseAudio mainloop/context creation on Linux, makes context creation return null, and requires one mainloop free.

The pre-fix assertion observed 0 frees instead of 1. Removing only the production patch reproduced that failure; restoring the patch passed the same test.

## Commands and results

The log directory is `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-319.r1/logs`. Build commands ran from the worktree root. All ctest commands ran from `build/tests`.

| Check | Exact command | Exit | Evidence |
|---|---|---:|---|
| Build regression target before fix | `cmake --build build --target AudioEngineTeardownTest` | 0 | `build-1.log` |
| Rebuild app before accepting RED | `cmake --build build --target zene` | 0 | `build-2.log` |
| Original-code RED | `ctest -R AudioEngineTeardownTest --output-on-failure` | 8 | `test-1.log`: actual free count 0, expected 1; 1/1 ctest failed |
| Build after fix | `cmake --build build --target AudioEngineTeardownTest zene` | 0 | `build-3.log` |
| GREEN | `ctest -R AudioEngineTeardownTest --output-on-failure` | 0 | `test-2.log`: 1/1 ctest passed |
| Save production-only patch | `git diff -- src/core/audio/AudioPulseAudio.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-319.r1/logs/fix.patch` | 0 | `fix.patch` |
| Remove fix for proof | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-319.r1/logs/fix.patch` | 0 | — |
| Rebuild without fix | `cmake --build build --target AudioEngineTeardownTest zene` | 0 | `build-4.log` |
| Reversal RED | `ctest -R AudioEngineTeardownTest --output-on-failure` | 8 | `test-3.log`: actual free count 0, expected 1; 1/1 ctest failed |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-319.r1/logs/fix.patch` | 0 | — |
| Rebuild with fix restored | `cmake --build build --target AudioEngineTeardownTest zene` | 0 | `build-5.log` |
| Reversal GREEN | `ctest -R AudioEngineTeardownTest --output-on-failure` | 0 | `test-4.log`: 1/1 ctest passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-3.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-4.log` |

Affected-family count: **1 registered ctest test**, `AudioEngineTeardownTest`, executed on each ctest run. The RED run reports 4 passed and 1 failed Qt test case; the failure is the new regression assertion.

## Files in the commit

- `src/core/audio/AudioPulseAudio.cpp`
- `tests/src/core/AudioEngineTeardownTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p12-319.md`
