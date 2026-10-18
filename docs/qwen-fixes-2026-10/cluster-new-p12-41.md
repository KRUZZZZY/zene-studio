# Cluster new-p12-41

Verdict: **BLOCKED**. The arrangement-recording bug is fixed and the regression passes with the fix. The required ratchet gates still fail on unrelated, unmodified files in this branch; the gates were not re-anchored.

## Evidence

- [SessionArrangementRecorder.h](../../include/SessionArrangementRecorder.h:96) carries the launched MIDI pattern ID or immutable audio-buffer pointer with each start event. [SessionFollow.cpp](../../src/core/SessionFollow.cpp:510) captures that identity at the actual launch, including Follow Action scene switches.
- [ControlCommandsSessionRecordInternal.h](../../src/core/ControlCommandsSessionRecordInternal.h:70) preserves the identity while pairing start and stop events. [ControlCommandsSessionRecordLand.cpp](../../src/core/ControlCommandsSessionRecordLand.cpp:203) validates and copies the captured content when present, while retaining the existing fallback for events without a content snapshot.
- The regression in [ControlSessionRecordLandTest.cpp](../../tests/src/core/ControlSessionRecordLandTest.cpp:200) launches pattern 0, changes the cell to pattern 1 before stopping, then checks that the landed result still names and copies pattern 0.

## Commands and results

Commands below ran from the worktree root except `ctest` commands, which ran from `build/tests`.

| Check | Command | Exit/result |
|---|---|---|
| Original-code RED build | `cmake --build build --target ControlSessionRecordLandTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-41.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Original-code RED test | `ctest -R ControlSessionRecordLandTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-41.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=8`; the assertion saw pattern 1, expected 0; 12 passed, 1 failed |
| Save final product patch | `git diff -- include/SessionArrangementRecorder.h include/SessionScheduler.h src/core/ControlCommandsSessionRecordInternal.h src/core/ControlCommandsSessionRecordLand.cpp src/core/SessionFollow.cpp src/core/SessionSceneFollow.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-41.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Reverse product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-41.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Reverted-code build | `cmake --build build --target zene ControlSessionRecordLandTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-41.r1/logs/build-8.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Reverted-code RED test | `ctest -R ControlSessionRecordLandTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-41.r1/logs/test-8.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=8`; the same assertion saw pattern 1, expected 0 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-41.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Final build | `cmake --build build --target zene ControlSessionRecordLandTest RecordingPassTest SessionArrangementRecordTest SessionAudioRecordTest SessionFollowTest SessionPerformTest SessionPlaybackRenderTest SessionPlaybackTest SessionSceneFollowTest SessionSchedulerRenderTest SessionSchedulerTest SessionSubSceneTest SessionGridRecordTest SessionTransportBarTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-41.r1/logs/build-9.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| Final GREEN family run | `ctest -R 'ControlSessionRecordLandTest|RecordingPassTest|SessionArrangementRecordTest|SessionAudioRecordTest|SessionFollowTest|SessionPerformTest|SessionPlaybackRenderTest|SessionPlaybackTest|SessionSceneFollowTest|SessionSchedulerRenderTest|SessionSchedulerTest|SessionSubSceneTest|SessionGridRecordTest|SessionTransportBarTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-41.r1/logs/test-9.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0`; 14/14 affected CTest targets passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-41.r1/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=1`; reports regressions in other files including `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, and `ControlCommandsShortcuts.cpp`. No changed function is listed as a regression. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-41.r1/logs/gate-4.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=1`; reports 19 over-limit sources, none changed here. The changed land source is 498 lines. |

An earlier test-only attempt (`ctest -R ControlSessionRecordLandTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-41.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc`, `EXIT=8`) used zero-length notes and failed at the note-count assertion. That setup was corrected before accepting the original-code RED result above.

The final RED/GREEN comparison and the reverse-patch check establish that the regression catches the original behavior. The code and affected-family tests pass, but the gate failures prevent a `FIXED` verdict under the required commit gates.

## Files in this commit

- `include/SessionArrangementRecorder.h`
- `include/SessionScheduler.h`
- `src/core/ControlCommandsSessionRecordInternal.h`
- `src/core/ControlCommandsSessionRecordLand.cpp`
- `src/core/SessionFollow.cpp`
- `src/core/SessionSceneFollow.cpp`
- `tests/src/core/ControlSessionRecordLandTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-41.md`
