# Cluster new-p8-9

**Verdict: FIXED**

`chord.track_write` treated chord-event song positions as clip-local MIDI note positions. It now subtracts the destination clip's arrangement start before laying out each chord. Chord lengths remain based on chord-track timing.

## Evidence

- [ControlCommandsChordWrite.cpp](../../src/core/ControlCommandsChordWrite.cpp#L106): `writeTrackChords` accepts the clip start and lays notes out at `event.pos - clipStart` (lines 106–116); the caller passes `write.clip->startPosition().getTicks()` (lines 157–160).
- [ControlChordWriteTest.cpp](../../tests/src/core/ControlChordWriteTest.cpp#L240): `theTrackUsesClipLocalPositions` writes a chord at song tick 480 into a clip starting at tick 384 and requires all three notes to have local position 96.
- Before the fix, the assertion failed with actual position 480 and expected 96. Removing the saved product patch and rebuilding reproduced the same failure.

The affected CTest family contains **1 registered test**, `ControlChordWriteTest`. RED output reported 6 Qt test slots passed and 1 failed; GREEN CTest reported 1/1 passed.

## Commands and results

All build and test output was written to `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r1/logs`.

| Check | Exact command | Exit |
|---|---|---:|
| Initial regression build | `mkdir -p /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r1/logs && cmake --build build --target ControlChordWriteTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r1/logs/build-1.log 2>&1; status=$?; echo EXIT=$status` | 0 |
| RED, before production fix | `cd build/tests && ctest -R ControlChordWriteTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r1/logs/test-1.log 2>&1; status=$?; echo EXIT=$status` | 8 |
| Remove production patch for rollback proof | `git diff -- src/core/ControlCommandsChordWrite.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r1/logs/fix.patch; status=$?; echo EXIT=$status` | 0 |
| Rebuild without fix | `cmake --build build --target zene ControlChordWriteTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r1/logs/build-2.log 2>&1; status=$?; echo EXIT=$status` | 0 |
| RED, with production patch removed | `cd build/tests && ctest -R ControlChordWriteTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r1/logs/test-2.log 2>&1; status=$?; echo EXIT=$status` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r1/logs/fix.patch; status=$?; echo EXIT=$status` | 0 |
| Rebuild with fix | `cmake --build build --target zene ControlChordWriteTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r1/logs/build-3.log 2>&1; status=$?; echo EXIT=$status` | 0 |
| GREEN | `cd build/tests && ctest -R ControlChordWriteTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r1/logs/test-3.log 2>&1; status=$?; echo EXIT=$status` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r1/logs/gate-3.log 2>&1; status=$?; echo EXIT=$status` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r1/logs/gate-4.log 2>&1; status=$?; echo EXIT=$status` | 0 |

## Files committed

- `src/core/ControlCommandsChordWrite.cpp`
- `tests/src/core/ControlChordWriteTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-9.md`
