# Cluster new-p10-240: MicroTimer elapsed overflow

Verdict: **FIXED**

`src/core/MicroTimer.cpp:27-30` now measures in `std::chrono::microseconds` and clamps values
above `std::numeric_limits<int>::max()` before converting to the existing `int` API.
`tests/src/core/ScriptClockTest.cpp:81,125-134` adds `elapsedMicrosecondsSaturateAtIntMax`: it
sets the standard-layout timer's start point 37 minutes in the past and expects `INT_MAX` without
waiting. The original code returned `-2074967296`. The test source is registered at
`tests/fork-sources.txt:2026`; the inherited production change is declared at
`tests/upstream-modifications.txt:1054`.

## Verification

All build and CTest output is in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-240.r1/logs`.
The affected CTest family contains one test executable; each reported run executed 1/1 tests.

| Purpose | Exact command | Exit |
|---|---|---:|
| Initial standalone-target attempt (`MicroTimerTest` was not configured) | `cmake --build build --target zene MicroTimerTest -j12` | 2 (`build-1.log`: no rule for target) |
| Pre-fix build | `cmake --build build --target zene ScriptClockTest -j12` | 0 (`build-2.log`) |
| RED on original code | `cd build/tests && ctest -R '^ScriptClockTest$' --output-on-failure` | 8 (`test-1.log`; 1/1 failed, actual `-2074967296`, expected `2147483647`) |
| Fixed build | `cmake --build build --target zene ScriptClockTest -j12` | 0 (`build-3.log`) |
| GREEN after fix | `cd build/tests && ctest -R '^ScriptClockTest$' --output-on-failure` | 0 (`test-2.log`; 1/1 passed) |
| Reverse the production patch | `git diff -- src/core/MicroTimer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-240.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-240.r1/logs/fix.patch` | 0 |
| Rebuild without the fix | `cmake --build build --target zene ScriptClockTest -j12` | 0 (`build-4.log`) |
| Reverse-patch RED | `cd build/tests && ctest -R '^ScriptClockTest$' --output-on-failure` | 8 (`test-3.log`; same failing values, 1/1 failed) |
| Reapply the production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-240.r1/logs/fix.patch` | 0 |
| Rebuild with the fix restored | `cmake --build build --target zene ScriptClockTest -j12` | 0 (`build-5.log`) |
| Restored GREEN | `cd build/tests && ctest -R '^ScriptClockTest$' --output-on-failure` | 0 (`test-4.log`; 1/1 passed) |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | 0 (`gate-1.log`) |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | 0 (`gate-2.log`) |

`fff` was not exposed as a tool or CLI in this session, so I searched the log folder with `rg` for
`error`, `FAILED`, and `Assertion` instead. The only matching failures are the two intentional RED
runs.

## Files in this commit

- `src/core/MicroTimer.cpp`
- `tests/src/core/ScriptClockTest.cpp`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-240.md`
