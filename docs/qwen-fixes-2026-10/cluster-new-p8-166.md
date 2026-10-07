# Cluster new-p8-166: master hardware insert latency

**Verdict: FIXED**

## Evidence

- `src/core/Mixer.cpp:1851` includes `hardwareio::insertLatency(*channel)` when resolving each channel's output latency. The published master total at `src/core/Mixer.cpp:1972-1975` previously added only the master's FX and post-insert chain latency. It now adds the master's hardware insert latency too.
- `tests/src/core/HardwareIoTest.cpp:284` enables a master hardware insert with a deterministic 123-frame override, advances one mixer period, and checks that `totalLatencyFrames()` reports 123; it then undoes the setting and checks the total returns to zero.
- The regression was red against the original implementation: the insert reported 123 frames while the total reported 0. Removing the production patch reproduced that failure; restoring it made the test pass.

## Verification commands

Commands below were run in the worktree unless a different working directory is noted. The `ctest` commands ran from `build/tests`.

| Purpose | Exact command | Exit |
|---|---|---:|
| Rebuild original app and test before the red confirmation | `cmake --build build --target zene HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-166.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| RED on original implementation | `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-166.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 8 |
| Build fixed app and affected test | `cmake --build build --target zene HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-166.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| GREEN with the fix | `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-166.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 0 |
| Save and reverse only the production patch | `git diff -- src/core/Mixer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-166.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-166.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild with the production patch reversed | `cmake --build build --target zene HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-166.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| RED with the production patch reversed | `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-166.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 8 |
| Restore the production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-166.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild restored app and test | `cmake --build build --target zene HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-166.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after restoring the production patch | `ctest -R '^HardwareIoTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-166.r1/logs/test-5.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-166.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-166.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

The affected CTest family executed 1 test (`HardwareIoTest`, 1/1). On the red run, QtTest reported 11 passed and the new regression failed; both green CTest runs reported 100% passed.

## Files committed

- `src/core/Mixer.cpp`
- `tests/src/core/HardwareIoTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-166.md`
