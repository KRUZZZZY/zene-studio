# Cluster new-p10-153-port

Verdict: **FIXED**

`HardwareInsert::load()` now checks the `ok` result of both latency integer
conversions. A malformed value leaves that setting at its initialized `-1`
sentinel. Valid integer values retain their existing behavior.

## Evidence

- `src/core/HardwareInsert.cpp` parses `latency` and `measured` with
  `toInt(&ok)` and only replaces each default when conversion succeeds.
- `tests/src/core/HardwareIoTest.cpp` verifies malformed `latency`
  keeps the override at `-1` and uses the valid measured figure (512 frames).
- `tests/src/core/HardwareIoTest.cpp` verifies malformed `measured`
  keeps the unknown sentinel and yields zero effective latency.

The affected CTest family has **1 registered test** (`HardwareIoTest`). Each
successful CTest invocation reported 1/1 passed. The RED QtTest run executed
13 result rows: 11 passed and the two new regression cases failed.

## Verification commands and results

Commands below were run from the worktree root unless marked `build/tests`.
For test commands, the reported exit is the captured `ctest` exit code.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| Initial RED build | `cmake --build build --target zene HardwareIoTest > .../logs/build-1.log 2>&1; ...` | 0 |
| Initial RED test (`build/tests`) | `ctest -R '^HardwareIoTest$' --output-on-failure > .../logs/test-1.log 2>&1; ...` | 8 |
| Fixed GREEN build | `cmake --build build --target zene HardwareIoTest > .../logs/build-2.log 2>&1; ...` | 0 |
| Fixed GREEN test (`build/tests`) | `ctest -R '^HardwareIoTest$' --output-on-failure > .../logs/test-2.log 2>&1; ...` | 0 |
| Reverse production patch | `git diff -- src/core/HardwareInsert.cpp > .../logs/fix.patch && git apply -R .../logs/fix.patch; ...` | 0 |
| Reverse-patch build | `cmake --build build --target zene HardwareIoTest > .../logs/build-3.log 2>&1; ...` | 0 |
| Reverse-patch RED test (`build/tests`) | `ctest -R '^HardwareIoTest$' --output-on-failure > .../logs/test-3.log 2>&1; ...` | 8 |
| Restore production patch | `git apply .../logs/fix.patch; ...` | 0 |
| Restored GREEN build | `cmake --build build --target zene HardwareIoTest > .../logs/build-4.log 2>&1; ...` | 0 |
| Restored GREEN test (`build/tests`) | `ctest -R '^HardwareIoTest$' --output-on-failure > .../logs/test-4.log 2>&1; ...` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > .../logs/gate-1.log 2>&1; ...` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check > .../logs/gate-2.log 2>&1; ...` | 1 |

The two ratchet gates exit 1 because of pre-existing regressions on this branch
that are unrelated to this fix (six over-target functions and nineteen over-length
files already present before the change; this change adds no new over-target
function and keeps `HardwareIoTest.cpp` in the same pre-existing over-length set).

## Files in the commit

- `src/core/HardwareInsert.cpp`
- `tests/src/core/HardwareIoTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-153-port.md`
