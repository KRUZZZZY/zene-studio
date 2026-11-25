FIXED

# Cluster 2140 - edit-set changes did not mark the project modified

## Verdict: REAL
- `src/core/ControlCommandsVcaEditSet.cpp` handlers `setPhaseLock`, `addEditTrack`, `removeEditTrack` changed plain group state (`VcaGroup::setPhaseLocked/addEditTrack/removeEditTrack`) and never called `Song::setModified()`. The sibling nesting handler does: `src/core/ControlCommandsVcaNest.cpp:108`.
- Undo/redo half of the finding is already handled: `ProjectJournal::undo` / `redo` call `Engine::getSong()->setModified()` (`src/core/ProjectJournal.cpp:173`, `:225`), and the handlers record their undo steps with `addJournalAction`. The new test asserts this so it cannot regress.

## Fix
`src/core/ControlCommandsVcaEditSet.cpp`: `Engine::getSong()->setModified()` after each successful write in the three handlers (after the refusal returns, so a refused call stays clean). Includes `Engine.h`, `Song.h`. No new command, no audio-thread code, undo step count unchanged.

## Regression test
`tests/src/core/ControlVcaEditGroupsTest.cpp` slot `editSetChangesMarkTheProjectModified`: makes the song clean with `Song::noteProjectSaved`, runs each of the three commands and checks `isModified()`; checks a refused `vca.track_remove` leaves it clean; checks `ProjectJournal::undo()` / `redo()` dirty it. No timing.

## Commands (logs in /home/kruzzzzy/.config/kads/zene/state/runs/2140.r2/logs)
| Step | Command | EXIT |
|---|---|---|
| RED build (test only, no fix) | `cmake --build build --target zene ControlVcaEditGroupsTest -j4 > build-1.log` | 0 |
| RED run | `cd build/tests && ctest -R ControlVcaEditGroupsTest --output-on-failure > test-1.log` | 8 (FAIL at test.cpp:327 "vca.track_add left the project clean") |
| GREEN build | same target, `build-2.log` | 0 |
| GREEN run | ctest -R ControlVcaEditGroupsTest, `test-2.log` | 0 |
| 4b revert | `git diff -- src/core/ControlCommandsVcaEditSet.cpp > fix.patch && git apply -R fix.patch` | 0 |
| 4b rebuild | `build-3.log` | 0 |
| 4b run (fix reverted) | ctest -R ControlVcaEditGroupsTest, `test-3.log` | 8 (same FAIL at :327) |
| 4b re-apply | `git apply fix.patch` | 0 |
| family build | `cmake --build build --target ControlVcaCommandsTest ControlVcaNestTest ControlVcaLockedEditsTest VcaGroupTest VcaRefreshAtomicityTest ControlVcaEditGroupsTest -j4`, `build-4.log` | 0 |
| family run | `ctest -R 'ControlVca\|VcaGroup\|VcaRefresh'`, `test-4.log` | 0 (100% passed, 7 tests) |
| gates | `python3 .../feature_check.py <worktree> > gates-1.log` | 0, RESULT: PASS (all 8 rows PASS) |

Not verified: TSan (does not run locally); the handlers run on the control/GUI thread, not the audio thread.

Delegation: Haiku ran the RED build/test, the green + revert/reapply + family builds and tests, and reported exit codes; I checked the logs and diff.

## Self-check
- **Red/green:** with `ControlCommandsVcaEditSet.cpp` reverted the test fails (ctest EXIT=8, `test-3.log`, FAIL at ControlVcaEditGroupsTest.cpp:327); with the fix it passes (EXIT=0, `test-2.log`). The RED run before any fix was EXIT=8 too (`test-1.log`).
- **Deterministic:** no timers, sleeps or threads. The clean state is made by the public `Song::noteProjectSaved(QString())`; commands run synchronously through `revtest::run`; undo/redo are direct `ProjectJournal::undo()/redo()` calls.
- **Complete at the cause:** the three handlers in this file are the only edit-set writers (`setPhaseLocked`, `addEditTrack`, `removeEditTrack`); each now calls `setModified()` once after the write (`ControlCommandsVcaEditSet.cpp:93,151,213`), same pattern as `ControlCommandsVcaNest.cpp:108`. The refusal paths return earlier and stay clean (asserted). Undo/redo closures call the group setters directly, but `ProjectJournal::undo/redo` set modified themselves (`ProjectJournal.cpp:173,225`), asserted in the test. Other vca handlers (create, remove, rename, assign, unassign, edit_move, set_gain/mute/solo) were not changed - see Other findings.
- **No collateral damage:** only an extra `Song::setModified()` (no-op while a project is loading, `Song.cpp:1019`) after a successful write; save/load format, group state, command registration and the ControlCommands snapshot are untouched (no command added). Family tests ControlVca*, VcaGroup*, VcaRefresh* pass (7/7, `test-4.log`).
- **ALREADY-FIXED/FALSE-POSITIVE:** not applicable; verdict is FIXED.
- **Sibling commands:** the list in the check (app.*, arrangement.*) is the registry, not this file's commands; this file registers only vca.set_phase_lock, vca.track_add, vca.track_remove, all fixed. Other vca.* commands are not affected by this change.

## Other findings
- Not verified, not fixed here: the other `vca.*` handlers (`ControlCommandsVcaEntity.cpp`, `...Mix.cpp`, `...Edit.cpp`) contain no `setModified()` call (fff search of `src/core/ControlCommandsVca*` finds it only in EditSet and Nest). Whether they dirty the song another way (model dataChanged, journal checkpoint) was not checked; vca.create/remove/rename/assign/edit_move are worth auditing.
