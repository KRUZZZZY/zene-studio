# Cluster new-p9-569: PulseAudio stream creation failure

Verdict: **FIXED**

`src/core/audio/AudioPulseAudio.cpp:129-135` now checks the `pa_stream_new()` result before registering either stream callback. A null result logs the failure, calls `signalConnected(false)` to release the connection wait as failed, and exits the READY case. The regression at `tests/src/core/AudioEngineTeardownTest.cpp:117-136` checks that the null guard, failure signal, and early exit precede callback registration. It reads the source so it does not depend on a running PulseAudio server.

## Evidence

| Check | Exact command(s) and result | Log |
| --- | --- | --- |
| Initial RED | `cmake --build build --target AudioEngineTeardownTest` — EXIT=0; `ctest -R AudioEngineTeardownTest --output-on-failure` — EXIT=8. The assertion failed because the null check was absent; QtTest reported 4 passed, 1 failed. | `build-2.log`, `test-2.log` |
| GREEN | `cmake --build build --target zene AudioEngineTeardownTest` — EXIT=0; `ctest -R AudioEngineTeardownTest --output-on-failure` — EXIT=0 (1/1 CTest passed). | `build-3.log`, `test-3.log` |
| Reverse-patch proof | `git diff -- src/core/audio/AudioPulseAudio.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-569.r1/logs/fix.patch` — EXIT=0; `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-569.r1/logs/fix.patch` — EXIT=0; `cmake --build build --target zene AudioEngineTeardownTest` — EXIT=0; `ctest -R AudioEngineTeardownTest --output-on-failure` — EXIT=8 with the same missing-null-check assertion. | `fix.patch`, `build-4.log`, `test-4.log` |
| Reapplied-patch proof | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-569.r1/logs/fix.patch` — EXIT=0; `cmake --build build --target zene AudioEngineTeardownTest` — EXIT=0; `ctest -R AudioEngineTeardownTest --output-on-failure` — EXIT=0 (1/1 CTest passed). | `build-5.log`, `test-5.log` |
| Affected family count | `AudioEngineTeardownTest`: 1 CTest case executed. | `test-2.log`, `test-3.log`, `test-4.log`, `test-5.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` — EXIT=0. | `gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` — EXIT=0. | `gate-2.log` |

The first attempted RED run (`build-1.log` / `test-1.log`, EXIT=0 / EXIT=8) used an incorrect source fixture path and failed before reaching the regression assertion. The corrected RED run above is the reported result.

The fff grep tool was not exposed in this session; the numbered logs were searched with `rg -n` for errors, failures, assertions, totals, and exit codes.

## Files committed

- `src/core/audio/AudioPulseAudio.cpp`
- `tests/src/core/AudioEngineTeardownTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-569.md`
