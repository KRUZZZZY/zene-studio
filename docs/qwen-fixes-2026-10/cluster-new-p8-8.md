# cluster new-p8-8

Verdict: **FIXED**

Chord detection returned clip-local match positions, and the writer added the
clip start without its time offset. A left-trimmed clip therefore wrote events
at the trimmed edge instead of at the notes' effective song origin. The writer
now supplies `startPosition() + startTimeOffset()` as the origin.

## Evidence

- [`src/core/ControlCommandsChordEdit.cpp:239`](../../src/core/ControlCommandsChordEdit.cpp#L239)
  adds the supplied origin to `match.pos`; lines 282-283 now pass the effective
  MIDI note origin.
- [`tests/src/core/ControlChordCommandsTest.cpp:397`](../../tests/src/core/ControlChordCommandsTest.cpp#L397)
  retains the moved-clip regression (events at 384 and 576).
- [`tests/src/core/ControlChordCommandsTest.cpp:419`](../../tests/src/core/ControlChordCommandsTest.cpp#L419)
  trims a clip from 384 to 480, verifies its nonzero offset is -96, then checks
  that detected events remain at 384 and 576.

## Command evidence

The captured exit code is the value printed by `result=$?; echo EXIT=$result`.
The offset regression failed with the production patch removed and passed after
the identical patch was restored.

| Purpose | Exact command | Exit |
|---|---|---:|
| Build app and focused test with old code | `cmake --build build --target zene ControlChordCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r2/logs/build-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| RED: offset regression with old code | `./ControlChordCommandsTest theDetectionAddsTheClipTimeOffset > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r2/logs/test-2.log 2>&1; result=$?; echo EXIT=$result` (from `build/tests`) | 1 |
| Remove production patch for proof | `git diff -- src/core/ControlCommandsChordEdit.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r2/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| Rebuild with patch removed | `cmake --build build --target zene ControlChordCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r2/logs/build-3.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| RED: required fix-removal proof | `./ControlChordCommandsTest theDetectionAddsTheClipTimeOffset > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r2/logs/test-3.log 2>&1; result=$?; echo EXIT=$result` (from `build/tests`) | 1 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r2/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| Rebuild with patch restored | `cmake --build build --target zene ControlChordCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r2/logs/build-4.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| GREEN: same offset regression | `./ControlChordCommandsTest theDetectionAddsTheClipTimeOffset > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r2/logs/test-4.log 2>&1; result=$?; echo EXIT=$result` (from `build/tests`) | 0 |
| Final build of app and all five chord-family targets | `cmake --build build --target zene ChordTrackTest ChordDetectTest ChordProgressionTest ControlChordCommandsTest ControlChordWriteTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r2/logs/build-5.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| Final offset regression | `./ControlChordCommandsTest theDetectionAddsTheClipTimeOffset > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r2/logs/test-5.log 2>&1; result=$?; echo EXIT=$result` (from `build/tests`) | 0 |
| Retained moved-clip regression | `./ControlChordCommandsTest theDetectionAddsTheClipArrangementPosition > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r2/logs/test-6.log 2>&1; result=$?; echo EXIT=$result` (from `build/tests`) | 0 |
| Chord-family tests | `ctest -R 'ChordTrackTest|ChordDetectTest|ChordProgressionTest|ControlChordCommandsTest|ControlChordWriteTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r2/logs/test-7.log 2>&1; result=$?; echo EXIT=$result` (from `build/tests`) | 0; **5/5 test executables passed** |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r2/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r2/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |

## Files in the fix commit

- `src/core/ControlCommandsChordEdit.cpp`
- `tests/src/core/ControlChordCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-8.md`
