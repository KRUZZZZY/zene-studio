# Cluster new-p9-37: unchanged rack split adds undo

Verdict: **FIXED**

## Finding and fix

The finding was real. In `src/core/ControlCommandsDeviceTools.cpp:154-162`, `rackSplit` previously called `setSplit`, added an undo step, and marked the song modified even when the requested mode and crossovers already matched the rack. It now compares the full `RackSplit` state and returns the current result before those operations when equal.

The dedicated regression is `tests/src/core/DeviceToolsTest.cpp:284-309`. It sets a bands split with a 1000 Hz crossover, reapplies it, and checks that the undo depth stays unchanged. With the production patch reversed, it failed at `noExtraStep`; with the patch applied, the command undoes the single real change. `DeviceToolsTest.cpp` is already registered in `tests/fork-sources.txt:1944`.

## Evidence

| Check | Command | Exit |
|---|---|---:|
| Rebuild with the production patch reversed | `cmake --build build --target zene DeviceToolsTest` (`build-6.log`) | 0 |
| RED regression | `cd build/tests && ctest -R DeviceToolsTest --output-on-failure` (`test-5.log`) | 8 |
| Rebuild with the production patch restored | `cmake --build build --target zene DeviceToolsTest` (`build-7.log`) | 0 |
| GREEN regression | `cd build/tests && ctest -R DeviceToolsTest --output-on-failure` (`test-6.log`) | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check` (`gate-3.log`) | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check` (`gate-4.log`) | 0 |

The affected family ran one CTest target (`DeviceToolsTest`, 1/1). The RED QtTest output was 5 passed, 1 failed, and 7 skipped; the failing assertion was `noExtraStep` at `DeviceToolsTest.cpp:308`. The GREEN CTest result was 1/1 passed.

The required patch-reversal commands also succeeded: `git diff -- src/core/ControlCommandsDeviceTools.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-37.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-37.r1/logs/fix.patch` (exit 0), followed by `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-37.r1/logs/fix.patch` (exit 0).

Build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-37.r1/logs`. The initial draft build (`build-1.log`, exit 2) attempted to call private `Song::setModified(bool)` from the test; that probe was removed before the final regression. Builds `build-2.log` through `build-7.log` exited 0; test runs `test-1.log`, `test-3.log`, and `test-5.log` exited 8, while `test-2.log`, `test-4.log`, and `test-6.log` exited 0. The earlier gate runs (`gate-1.log` and `gate-2.log`) also exited 0.

The requested `fff grep` tool/executable was unavailable in this session, so the logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Files in this commit

- `src/core/ControlCommandsDeviceTools.cpp`
- `tests/src/core/DeviceToolsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-37.md`
