# Cluster new-p8-8 — FIXED

The finding is real. `ChordDetect::detectChords()` reports note positions local to the MIDI clip, while the chord track stores song ticks. `writeDetectedMatch()` now adds the clip's arrangement start before storing each event at [ControlCommandsChordEdit.cpp:243](../../src/core/ControlCommandsChordEdit.cpp#L243). The writer passes that start at line 282. The regression at [ControlChordCommandsTest.cpp:398](../../tests/src/core/ControlChordCommandsTest.cpp#L398) moves a two-chord clip to tick 384 and checks for chord events at 384 and 576.

Before the fix, the regression failed with `events[0].pos` actual 0, expected 384. Reversing only the production patch reproduced the same failure; restoring the patch made it pass.

## Evidence

Commands below were run from the worktree root unless marked `build/tests`. Build and test output was redirected to new logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r1/logs`.

| Check | Exact command | Exit |
|---|---|---:|
| Pre-fix build | `cmake --build build --target zene ControlChordCommandsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| RED, run from `build/tests` | `ctest -R '^ControlChordCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 |
| Fixed focused build | `cmake --build build --target zene ControlChordCommandsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| GREEN, run from `build/tests` | `ctest -R '^ControlChordCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 |
| Remove production fix for inversion proof | `git diff -- src/core/ControlCommandsChordEdit.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Build without fix | `cmake --build build --target zene ControlChordCommandsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| RED without fix, run from `build/tests` | `ctest -R '^ControlChordCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 |
| Restore production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild restored fix | `cmake --build build --target zene ControlChordCommandsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after restoring fix, run from `build/tests` | `ctest -R '^ControlChordCommandsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 |
| Build all chord-family targets | `cmake --build build --target zene ChordTrackTest ChordDetectTest ChordProgressionTest ControlChordCommandsTest ControlChordWriteTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| Affected family, run from `build/tests` | `ctest -R '^(ChordTrackTest|ChordDetectTest|ChordProgressionTest|ControlChordCommandsTest|ControlChordWriteTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r1/logs/test-5.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-8.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

The affected chord family ran **5 CTest tests; 5 passed**. Both required gates passed.

## Files committed

- `src/core/ControlCommandsChordEdit.cpp`
- `tests/src/core/ControlChordCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-8.md`
