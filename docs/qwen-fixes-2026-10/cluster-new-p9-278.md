# Cluster new-p9-278: sndio MIDI handle is never closed

**Verdict: FIXED**

`MidiSndio` opened `m_hdl` but did not release it. The destructor now joins the worker (including after the existing timeout/termination fallback) before calling `mio_close()` for a non-null handle. The regression uses a fake sndio handle and checks that close happens exactly once after the thread stops.

## Evidence

- [MidiSndio.cpp](../../src/core/midi/MidiSndio.cpp#L66): destructor shutdown/join and handle close at line 80.
- [MidiReconnectTest.cpp](../../tests/src/core/MidiReconnectTest.cpp#L186): deterministic fake-handle regression. Before the fix, it failed with actual close count `0`, expected `1`.
- The affected CTest family is `MidiReconnectTest`: **1 test executed**; final run passed 1/1.

| Run | Exact command (working directory) | Exit | Result |
|---|---|---:|---|
| Pre-fix build | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-278.r1/logs; cmake --build build --target MidiReconnectTest > "$LOG_DIR/build-2.log" 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (worktree) | 0 | Regression target rebuilt. |
| RED | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-278.r1/logs; ctest -R '^MidiReconnectTest$' --output-on-failure > "$LOG_DIR/test-2-red.log" 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (`build/tests`) | 8 | 0/1 passed; close count was 0, expected 1. |
| Fixed build | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-278.r1/logs; cmake --build build --target zene MidiReconnectTest > "$LOG_DIR/build-3.log" 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (worktree) | 0 | Rebuilt product and test target. |
| GREEN | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-278.r1/logs; ctest -R '^MidiReconnectTest$' --output-on-failure > "$LOG_DIR/test-3-green.log" 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (`build/tests`) | 0 | 1/1 passed. |
| Remove fix for reversal proof | `git diff -- src/core/midi/MidiSndio.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-278.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-278.r1/logs/fix.patch` (worktree) | 0 | Product-only patch reversed. |
| Reversal build | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-278.r1/logs; cmake --build build --target zene MidiReconnectTest > "$LOG_DIR/build-4-reversal.log" 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (worktree) | 0 | Rebuilt without the fix. |
| Reversal RED | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-278.r1/logs; ctest -R '^MidiReconnectTest$' --output-on-failure > "$LOG_DIR/test-4-reversal-red.log" 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (`build/tests`) | 8 | 0/1 passed; the same close-count assertion failed. |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-278.r1/logs/fix.patch` (worktree) | 0 | Product patch reapplied. |
| Final build | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-278.r1/logs; cmake --build build --target zene MidiReconnectTest > "$LOG_DIR/build-5-final.log" 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (worktree) | 0 | Product and test targets rebuilt. |
| Final GREEN | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-278.r1/logs; ctest -R '^MidiReconnectTest$' --output-on-failure > "$LOG_DIR/test-5-final-green.log" 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (`build/tests`) | 0 | 1/1 passed after reapplying the fix. |
| Complexity gate | `bash tests/complexity-gate.sh --check` (worktree) | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check` (worktree) | 0 | Passed. |

## Files committed

- `src/core/midi/MidiSndio.cpp`
- `tests/src/core/MidiReconnectTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-278.md`
