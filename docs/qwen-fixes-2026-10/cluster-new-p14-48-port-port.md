FIXED

## Decision

REAL. Before the fix, the `record.recovery_place` redo in `src/core/ControlCommandsRecordingPlace.cpp` called `landRecoveredTake()` and then unconditionally wrote the journal as `restored`. If placement returned an empty track id, the offer disappeared even though no track was placed. `ProjectJournal::redo()` also recorded that no-op replay as an applied undo step.

Current code checks the id at `src/core/ControlCommandsRecordingPlace.cpp:114-125`: failure writes `offered`, calls `ProjectJournal::declineRedo()`, and returns; `restored` is written only after placement succeeds. `ProjectJournal::redo()` returns a declined action checkpoint to the redo stack without adding an undo entry (`src/core/ProjectJournal.cpp:170-187`), and the existing `control.redo` command returns typed `Refused` (`src/core/ControlCommandsControl.cpp:574-585`).

This is latent in ordinary production runs: `Track::create(Track::Type::Sample, ...)` constructs a `SampleTrack` at `src/core/Track.cpp:493`. The deterministic regression uses the test-only atomic seam in `ControlRecordingSupport.h` / `ControlCommandsRecordingPlace.cpp` to force the failure.

## Integration and semantic difference

I read all six accepted commits (`31467fe86`, `35a892097`, `10b552773`, `7744146cd`, `3b20c9a06`, `e0817f80e`) with `git show`. The recovery behavior and ProjectJournal decline design match the latest accepted patch, including the final exact undo-depth assertions. I did not merge, cherry-pick, or rebase.

The header conflict is resolved by retaining the train's filesystem identity API (`VolumeNaming` and `filesystemIdentityKey`) and adding the recovery test seam after it. The test conflict is resolved by retaining the train's `aFailedQueryIsNotNothingToRecover` test and adding both accepted recovery tests. This train exposes `interruptedTakes(QJsonArray*)`; the regression uses that API to compare the offered-take count, replacing the older no-argument call without changing the assertion. `RecordingRecoveryMenuTest.cpp` was absent from `tests/fork-sources.txt`; I registered it and added it to both recorded pathspecs so the source manifest reproduces. The existing entry in `tests/all-sources.txt` already covers it. Inherited `ProjectJournal` files are recorded in `tests/upstream-modifications.txt`.

The first gate run found that `tests/all-sources.txt` did not reproduce from its own published recipe: it carried 1,148 `plugins/SfzPlayer/sfizz/` vendor paths that the recipe excludes. I re-derived that manifest from the recipe, removing only those 1,148 entries, and recorded the reason in `tests/upstream-modifications.txt`. This is a scope-manifest repair; it changes no product code, gate limit, or baseline.

## Regression matrix and failure paths

| Path | Evidence / behavior |
| --- | --- |
| Successful placement | `aPlaceUndoRedoIsOneStep`: place, undo, redo, undo; checks track count, journal state, and exact undo depth at every boundary. |
| Redo cannot create a track | `aRedoThatCannotPlaceKeepsTheOffer`: injects failure only during redo; checks typed refusal, no track, `in_progress` journal, unchanged offer count and undo depth, and redo remains available. The reverse-patch run fails on depth 2 vs expected 1. |
| Initial placement cannot create a track | Existing caller writes `offered` and returns `Refused` (`ControlCommandsRecordingPlace.cpp:127-132`). It adds the undo action before attempting placement (`:106`), so this separate failure still leaves an empty action step. This sibling was already called out in the accepted report and remains outside the accepted redo design. |
| Journal write failure in action callbacks | Undo/redo callbacks ignore the `write()` result (`:107-125`); this pre-existing filesystem failure case is not injected by this regression. The initial pre-placement write of `restored` is checked (`:93-97`). |
| Other redo callers | `ProjectJournal::redo()` is called by `control.redo` and `MainWindow::redo()` (`src/gui/MainWindow.cpp:1651-1654`). Only the recovery action currently calls `declineRedo`; the GUI path retains the offer and redo checkpoint on failure but, unlike `control.redo`, has no typed command reply. No other `landRecoveredTake()` caller exists. |
| Realtime and persistence | The seam and journal callbacks run on command/undo paths, not audio rendering. The seam performs only an atomic bool load/store. The new decline reason is in-memory `QString` state; it does not change saved projects or journal formats. No command/schema was added. |
| Supported platforms | No OS-specific code was added. The seam uses standard `std::atomic<bool>` and Qt `QString`. This run built Linux with Qt 6; Qt 5.15, MSVC/Windows, and macOS were not available here and still require their normal CI builds. |

`addUndoStep(undo, redo)` records an action checkpoint (`src/core/ControlReversibility.cpp:147-152`), so this recovery step is within `declineRedo()`'s action-only contract. The new decline behavior is reached only when that action explicitly sets a reason; all other redo steps keep the existing path.

## Evidence

`LOG_DIR` is `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs`. All CTest commands ran from `build/tests`. The first preliminary build (`build-1-red.log`) exposed the train API mismatch (`interruptedTakes()` needs an output argument); that attempt was superseded and is not counted as RED evidence. The corrected regression was built before the valid RED run.

| Step | Exact command | Log | EXIT / result |
| --- | --- | --- | --- |
| RED build, fix reverted | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs; cmake --build build --target zene RecordingRecoveryMenuTest -j4 > "$LOG_DIR/build-2-red.log" 2>&1; rc=$?; echo EXIT=$rc` | `build-2-red.log` | EXIT=0 |
| RED test | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs; ctest -R '^RecordingRecoveryMenuTest$' --output-on-failure > "$LOG_DIR/test-1-red.log" 2>&1; rc=$?; echo EXIT=$rc` | `test-1-red.log` | EXIT=8; 10 passed, 1 failed. `undoDepth()` was 2; expected 1. |
| GREEN build | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs; cmake --build build --target zene RecordingRecoveryMenuTest -j4 > "$LOG_DIR/build-3-green.log" 2>&1; rc=$?; echo EXIT=$rc` | `build-3-green.log` | EXIT=0 |
| GREEN focused test | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs; ctest -R '^RecordingRecoveryMenuTest$' --output-on-failure > "$LOG_DIR/test-2-green.log" 2>&1; rc=$?; echo EXIT=$rc` | `test-2-green.log` | EXIT=0; 1/1 CTest passed. |
| 4b save product patch | `git diff HEAD -- include/ControlRecordingSupport.h include/ProjectJournal.h src/core/ControlCommandsControl.cpp src/core/ControlCommandsRecordingPlace.cpp src/core/ProjectJournal.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | `fix.patch` | EXIT=0 |
| 4b reverse product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs/fix.patch; echo EXIT=$?` | — | EXIT=0; kept only the deterministic seam enabled for the test. |
| 4b RED rebuild | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs; cmake --build build --target zene RecordingRecoveryMenuTest -j4 > "$LOG_DIR/build-4b-red.log" 2>&1; rc=$?; echo EXIT=$rc` | `build-4b-red.log` | EXIT=0 |
| 4b RED test | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs; ctest -R '^RecordingRecoveryMenuTest$' --output-on-failure > "$LOG_DIR/test-4b-red.log" 2>&1; rc=$?; echo EXIT=$rc` | `test-4b-red.log` | EXIT=8; same exact-depth failure. |
| 4b reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs/fix.patch; echo EXIT=$?` | — | EXIT=0 |
| 4b GREEN rebuild | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs; cmake --build build --target zene RecordingRecoveryMenuTest -j4 > "$LOG_DIR/build-4b-green.log" 2>&1; rc=$?; echo EXIT=$rc` | `build-4b-green.log` | EXIT=0 |
| 4b GREEN test | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs; ctest -R '^RecordingRecoveryMenuTest$' --output-on-failure > "$LOG_DIR/test-4b-green.log" 2>&1; rc=$?; echo EXIT=$rc` | `test-4b-green.log` | EXIT=0; focused test passed. |
| Rebuild header includers | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs; mapfile -t TARGETS < "$LOG_DIR/project-journal-test-targets.txt"; cmake --build build --target zene "${TARGETS[@]}" -j4 > "$LOG_DIR/build-5-journal-includers.log" 2>&1; rc=$?; echo EXIT=$rc` | `build-5-journal-includers.log` | EXIT=0; all 64 configured direct test includers built. |
| Recovery-family build | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs; cmake --build build --target zene ProjectRecoveryTest RecordingRecoveryMenuTest -j4 > "$LOG_DIR/build-6-family.log" 2>&1; rc=$?; echo EXIT=$rc` | `build-6-family.log` | EXIT=0 |
| Recovery family | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs; ctest -R Recover --output-on-failure > "$LOG_DIR/test-5-recovery-family.log" 2>&1; rc=$?; echo EXIT=$rc` | `test-5-recovery-family.log` | EXIT=0; 3/3 passed (`ProjectRecoveryTest`, `RecordingRecoveryMenuTest`, `ControlRecordingRecovery`). |
| Focused direct binary | `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs; ./RecordingRecoveryMenuTest > "$LOG_DIR/test-6-direct-green.log" 2>&1; rc=$?; echo EXIT=$rc` | `test-6-direct-green.log` | EXIT=0; 11 passed, 0 failed. |

The first `feature_check.py` run exited 1 because the train's all-sources list contained 1,148 excluded vendor entries; `gates-1-feature-check.log` records that result. Directly, `bash tests/all-sources-reproduce.sh` exited 1 before the correction (`gates-2-all-sources.log`); `gates-3-all-sources-diff.log` records the 1,148 vendor-only differences. After re-deriving the list, the same command exited 0 (`gates-4-all-sources.log`) and the final gate command below exited 0.

## Gates

Exact final command: `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs; python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-new-p14-48-aac861e62b > "$LOG_DIR/gates-5-feature-check.log" 2>&1; rc=$?; echo EXIT=$rc` → **EXIT=0**.

| Gate | Result |
| --- | --- |
| fork-sources | PASS, exit 0 |
| all-sources-reproduce | PASS, exit 0 |
| no-upstream-regression | PASS, exit 0 |
| check-namespace | PASS, exit 0 |
| no-tautology | PASS, exit 0 |
| doc-figures | PASS, exit 0 |
| complexity | PASS, no new item in the changed files |
| file-length | PASS, no new item in the changed files |

The initial manifest correction was verified with `LOG_DIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-48-port-port.r1/logs; bash tests/all-sources-reproduce.sh > "$LOG_DIR/gates-4-all-sources.log" 2>&1; rc=$?; echo EXIT=$rc` → **EXIT=0**. Its previous failure diff contained exactly 1,148 removed `plugins/SfzPlayer/sfizz/` entries and no additions.

## Files in this integration

- `include/ControlRecordingSupport.h`
- `include/ProjectJournal.h`
- `src/core/ControlCommandsControl.cpp`
- `src/core/ControlCommandsRecordingPlace.cpp`
- `src/core/ProjectJournal.cpp`
- `tests/all-sources.txt`
- `tests/fork-sources.txt`
- `tests/src/gui/RecordingRecoveryMenuTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p14-48-port-port.md`

## Self-check

- Regression proof: RED after reversing the product patch (`test-4b-red.log`, EXIT=8, undo depth 2 vs 1); GREEN after reapplying and rebuilding (`test-4b-green.log`, EXIT=0). The ordinary first RED/GREEN runs are in `test-1-red.log` and `test-2-green.log`.
- Determinism: the test uses fixed WAV data and sequential control commands. `setRecoveredTakeFailsForTest(true)` is set only around one `control.redo` and reset before assertions; no clock, sleep, or race controls the result.
- Cause and siblings: `landRecoveredTake()` has two callers, initial placement and redo; initial placement already restores the offer and refuses. `ProjectJournal::redo()` callers are the control command and GUI slot; only the recovery action calls `declineRedo()`. I checked each recording sibling: `record.arm_track`, `record.disarm_all`, `record.disarm_track`, `record.get_state`, `record.input_get_state`, `record.input_set`, `record.journal_begin`, `record.journal_finish`, `record.journal_update`, `record.recovery_discard`, `record.recovery_get_state`, `record.recovery_restore`, `record.remaining`, and `record.retro_capture_arm` do not call `landRecoveredTake()` or `declineRedo()` and do not write this placement's restored state. `record.recovery_restore` is resolve-only with no redo action. The initial-placement phantom step and ignored journal-write results are noted above.
- Collateral: the successful path remains one undo step, checked with exact depth. The new journal field is private, in-memory state only; no saved project or journal format changes, limits, or new command ids.
- Realtime/platform: no new audio-thread path, allocation, lock, wait, I/O, or logging. The only host built here is Linux/Qt 6; standard C++/Qt APIs were used and other supported platforms remain for their normal CI matrix.

The first post-commit completion check at `cbc959e1f` passed (`submit-1.log`, EXIT=0). The exact sibling-command list above answers its additional sibling audit question; the required final check will run after committing this report refinement.
