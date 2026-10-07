# Cluster new-p8-124

Verdict: **FIXED**

## Evidence

- `src/core/HardwareIo.cpp:206-210`: `listenSparesMain()` now counts a following cue only when it is routed and its master level is positive. `mixOutputs()` skips cues with `target.level <= 0` at `src/core/HardwareIo.cpp:235-240`, so a zero-level cue cannot receive the listened signal.
- `tests/src/core/HardwareIoTest.cpp:228-238`: the regression routes cue A, leaves it following listen, sets its level to zero, and verifies that the main output carries channel 2's post-fader listened signal. The test fails at line 238 with the production fix removed and passes with it restored.

## Build and test evidence

Commands below ran from the worktree root unless marked `build/tests`. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-124.r1/logs`.

| Phase | Command | Exit | Evidence |
| --- | --- | ---: | --- |
| Original RED build | `cmake --build build --target HardwareIoTest` | 0 | `build-1.log` |
| Original RED test (`build/tests`) | `ctest -R '^HardwareIoTest$' --output-on-failure` | 8 | `test-1-red.log`; `mainCarriesListen` failed at the regression assertion |
| Fixed build | `cmake --build build --target zene HardwareIoTest` | 0 | `build-2.log` |
| Fixed test (`build/tests`) | `ctest -R '^HardwareIoTest$' --output-on-failure` | 0 | `test-2-green.log`; 1/1 test passed |
| Reverse-patch command | `git diff -- src/core/HardwareIo.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-124.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-124.r1/logs/fix.patch` | 0 | `proof-1-reverse.log` |
| Rebuilt without fix | `cmake --build build --target zene HardwareIoTest` | 0 | `build-3-proof-red.log` |
| Reverse-patch RED (`build/tests`) | `ctest -R '^HardwareIoTest$' --output-on-failure` | 8 | `test-3-proof-red.log`; same `mainCarriesListen` assertion failed |
| Restore-patch command | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-124.r1/logs/fix.patch` | 0 | `proof-2-restore.log` |
| Rebuilt with fix | `cmake --build build --target zene HardwareIoTest` | 0 | `build-4-proof-green.log` |
| Restored GREEN (`build/tests`) | `ctest -R '^HardwareIoTest$' --output-on-failure` | 0 | `test-4-proof-green.log`; 1/1 test passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1-complexity.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2-file-length.log` |

Affected-family count: **1 test executed, 1 passed in the fixed run**.

## Files committed

- `src/core/HardwareIo.cpp`
- `tests/src/core/HardwareIoTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-124.md`
