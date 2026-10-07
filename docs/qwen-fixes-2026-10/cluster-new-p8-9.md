# Cluster new-p8-9

**Verdict: FIXED**

`chord.track_write` now converts each chord event from song time into the destination MIDI clip's note coordinates using the clip's arrangement start plus `startTimeOffset()`. MIDI playback uses that same origin in [InstrumentTrack.cpp](../../src/tracks/InstrumentTrack.cpp#L876).

## Evidence

- [ControlCommandsChordWrite.cpp](../../src/core/ControlCommandsChordWrite.cpp#L106) passes `startPosition + startTimeOffset` as `clipNoteOrigin` (lines 107, 116, 159–160) and subtracts it from each event position.
- [ControlChordWriteTest.cpp](../../tests/src/core/ControlChordWriteTest.cpp#L240) retains the arrangement-start case: event 480, clip start 384, expected local position 96.
- The added slipped-clip case at [ControlChordWriteTest.cpp:265](../../tests/src/core/ControlChordWriteTest.cpp#L265) uses event 600, clip start 384, and `clip.slip` offset 120. It expects all generated notes at local position 96.
- The regression failed before the product change and when only the product patch was reversed: actual 216, expected 96. With the fix restored, the CTest passed.
- The affected CTest family has **1 registered test**, `ControlChordWriteTest`. Each RED run reported 7 QtTest slots passed and 1 failed; the final GREEN run reported 1/1 CTest passed.

Build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r2/logs`. No fff grep tool or CLI was exposed in this session, so saved logs were searched with `rg -n` for `error`, `FAILED`, and `Assertion`.

## Commands and results

Build and gate commands ran from the worktree. CTest commands ran from `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-9/build/tests`.

| Check | Exact command | Exit |
|---|---|---:|
| Build regression before fix | `mkdir -p /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r2/logs; cmake --build build --target zene ControlChordWriteTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r2/logs/build-1.log 2>&1; status=$?; echo EXIT=$status` | 0 |
| RED before product change | `ctest -R ControlChordWriteTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r2/logs/test-1.log 2>&1; status=$?; echo EXIT=$status` | 8 |
| Build with fix | `cmake --build build --target zene ControlChordWriteTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r2/logs/build-2.log 2>&1; status=$?; echo EXIT=$status` | 0 |
| GREEN with fix | `ctest -R ControlChordWriteTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r2/logs/test-2.log 2>&1; status=$?; echo EXIT=$status` | 0 |
| Reverse only product patch | `git diff -- src/core/ControlCommandsChordWrite.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r2/logs/fix.patch; status=$?; echo EXIT=$status` | 0 |
| Rebuild with product patch reversed | `cmake --build build --target zene ControlChordWriteTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r2/logs/build-3.log 2>&1; status=$?; echo EXIT=$status` | 0 |
| RED with product patch reversed | `ctest -R ControlChordWriteTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r2/logs/test-3.log 2>&1; status=$?; echo EXIT=$status` | 8 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r2/logs/fix.patch; status=$?; echo EXIT=$status` | 0 |
| Final rebuild | `cmake --build build --target zene ControlChordWriteTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r2/logs/build-4.log 2>&1; status=$?; echo EXIT=$status` | 0 |
| Final GREEN | `ctest -R ControlChordWriteTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r2/logs/test-4.log 2>&1; status=$?; echo EXIT=$status` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r2/logs/gate-1.log 2>&1; status=$?; echo EXIT=$status` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-9.r2/logs/gate-2.log 2>&1; status=$?; echo EXIT=$status` | 0 |

## Files committed

- `src/core/ControlCommandsChordWrite.cpp`
- `tests/src/core/ControlChordWriteTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-9.md`
