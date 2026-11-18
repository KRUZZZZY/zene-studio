# Cluster new-p8-278

Verdict: **FIXED**

Empty optional numeric attributes were treated as absent while loading chord events. `optionalTick` and `optionalInt` in `src/core/ChordTrack.cpp:387-405` now use `hasAttribute()` to apply a fallback only when the attribute is absent. A present empty value proceeds through numeric parsing and is rejected, causing `eventFromElement()` and `loadSettings()` to skip that event. The regression at `tests/src/core/ChordTrackTest.cpp:277-293` checks empty `len`, `root`, and `octave`, alongside a valid event with those optional attributes absent.

## Evidence

Commands ran from the worktree root unless the working directory is stated. Build and test output was written to the listed logs. For each ctest command, the printed `EXIT` is the captured ctest exit code.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| Original regression RED | `ctest -R '^ChordTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-278.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` (from `build/tests`) | 8 |
| Fixed regression GREEN | `ctest -R '^ChordTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-278.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` (from `build/tests`) | 0 |
| Revert proof: save and remove product fix | `git diff -- src/core/ChordTrack.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-278.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-278.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit 0` | 0 |
| Revert proof RED | `ctest -R '^ChordTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-278.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` (from `build/tests`) | 8 |
| Revert proof: restore product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-278.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit 0` | 0 |
| Revert proof GREEN | `ctest -R '^ChordTrackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-278.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` (from `build/tests`) | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-278.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-278.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` | 0 |

The affected ctest family executed **1/1 test** (`ChordTrackTest`). Before the fix, the regression failed with `track.size()` equal to 4 instead of 1. It failed again after the product fix was removed and passed after the patch was restored.

## Files committed

- `src/core/ChordTrack.cpp`
- `tests/src/core/ChordTrackTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-278.md`

## Build commands

Each build captured the build process exit code in `rc`.

- `cmake --build build --target zene ChordTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-278.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` — exit 0, before the fix.
- `cmake --build build --target zene ChordTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-278.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` — exit 0, with the fix.
- `cmake --build build --target zene ChordTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-278.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` — exit 0, with the fix reverted.
- `cmake --build build --target zene ChordTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-278.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` — exit 0, after restoring the fix.
