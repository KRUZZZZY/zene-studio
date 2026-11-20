FIXED

# Cluster new-p8-81-port: r2 re-proof

Verdict: **FIXED**. The branch's own commits are the fix: `70a924a8a2` (product fix and regression test), `a6d4c81a4e` (command snapshot regenerated from the fixed registry), `4e72ea61d7` (regression test also asserts that all three take files exist).

Finding: "`track.set_arm_many` is marked reversible although undo leaves its takes behind."

Evidence source: this session's logs in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-81-port.r2/logs` (cited as `logs/<name>`). No result from an earlier round is used here.

**Exit codes:** the log files do not record exit codes. Where an EXIT value appears below it comes from the run record in the packet, and is marked as such. The result lines quoted are from the logs.

## Is the finding real (base `4eb8d6e8a`)

- `git show 4eb8d6e8a:src/core/ControlReversibilityTableRecording.cpp`, line 69: `R("track.set_arm_many", RC::Snapshot, true, ...)`. Its recorded inverse is described as "ONE recorded undo step".
- `git show 4eb8d6e8a:src/core/ControlCommandsArrangementState.cpp`, line 385: on arming, `setArmMany` inserts a `__transaction` whose inverse disarms the batch's tracks. So `control.undo` could claim to remove the batch while the takes stayed on disk.
- The RED run shows that claim. With the fix reverted, `logs/test-3.log` contains: `FAIL! : UndoHonestyTest::armAllRefusesUndoThatWouldLeaveItsTakesBehind() '!undo.ok' returned FALSE. (control.undo claimed it could remove the takes from track.set_arm_many)`.

## The fix at HEAD

The branch diff against `4eb8d6e8a` changes these files:

| File (HEAD) | Change |
| --- | --- |
| `src/core/ControlReversibilityTableRecording.cpp:69-78` | Row is now `RC::Irreversible, false`. The reason says take files outlive the captures. The fallback says to stop each route with `record.disarm_track` using the route ids in the reply. |
| `src/core/ControlCommandsArrangementState.cpp:363-366` | Doc comment rewritten. The `if (armed)` block that inserted the `__transaction` (base line 385) is removed. |
| `src/core/ControlCommandsArrangementState.cpp:388-398` | Command description: `control.undo` refuses this command and names `record.disarm_track`. No command added or removed. |
| `src/gui/RecordLaunchMenus.cpp:65-67` | Comment only. |
| `docs/RELEASE-NOTES-v0.3.0-alpha.md:999,1002` | A16 histogram now reads `snapshot=41 irreversible=23`. No HEAD run of `ReversibilityContractTest` is in these logs (see Other findings). |
| `tests/src/core/UndoHonestyTest.cpp:406-439` | Regression test `armAllRefusesUndoThatWouldLeaveItsTakesBehind`. It uses a `QTemporaryDir` working directory (line 409). It asserts `routes.size()==3` (line 434) and `!undo.ok` with an `Irreversible` kind whose message names `track.set_arm_many` (lines 435-436). `git grep` finds no `armingSeveralTracksIsOneStep` at HEAD. |
| `tools/mcp-zene-control/zene_control/commands_snapshot.json` | Three fields changed: `captured_at`, `source_file`, and the `track.set_arm_many` description (commit `a6d4c81a4e`). |

The option taken is the typed refusal (the row becomes Irreversible), which is the first option in the finding.

## Evidence

| Step | Log | Result |
| --- | --- | --- |
| Fix patch | `logs/fix.patch` | 71 lines (the patch's generating command is not in the logs) |
| build-2 (fixed tree) | `logs/build-2.log` | ends `[100%] Built target UndoHonestyTest` |
| test-2 (GREEN) | `logs/test-2.log` | `100% tests passed, 0 tests failed out of 1`. Run record: EXIT 0 |
| build-4 (RED build) | `logs/build-4.log` | ends `Linking CXX executable UndoHonestyTest`, then `Built target UndoHonestyTest`. The revert step itself is not in a log (build-3 not read) |
| test-3 (RED, fix reverted) | `logs/test-3.log` | `1/1 Test #274: UndoHonestyTest ...Failed`. `FAIL!` line quoted in the finding section. `Totals: 12 passed, 1 failed, 4 skipped`. Run record: EXIT 8 |
| test-4 (GREEN) | `logs/test-4.log` | `100% tests passed, 0 tests failed out of 1`. Run record: EXIT 0 |

### Mutation runs (mutation definitions come from the packet, not the logs)

| Run | Log | Result |
| --- | --- | --- |
| test-5, row-only mutation | `logs/test-5.log` | UndoHonestyTest passes (`100% tests passed out of 1`): the mutation survived this test |
| test-8, same mutation, contract test | `logs/test-8.log` | `ReversibilityContractTest ...Failed`. `FAIL! : ReversibilityContractTest::theTableHistogramIsTheDocumentedOne()`. `Totals: 8 passed, 1 failed`. Run record: EXIT 8 |
| test-6, handler-only mutation | `logs/test-6.log` | UndoHonestyTest passes (`100% tests passed out of 1`): survived. Why: `src/core/ControlRegistry.cpp:563` stamps the transaction irreversible when the row is Irreversible, and `src/core/ControlCommandsControl.cpp:402-405` (`answerIrreversibleTop`) returns the typed refusal, so the regression still sees a refusal |
| test-7, test-9 | `logs/test-7.log`, `logs/test-9.log` | Both `100% tests passed out of 1`. What each mutation was: not stated in the packet, unverified |

### Family and environment

| Run | Log | Result |
| --- | --- | --- |
| test-1-extra (family, real HOME) | `logs/test-1-extra.log` | `ControlEditCommandsTest.cpp(634)` fails: `trackSetArmManyReportsOnlyTheTracksItChanged() 'armed.ok' returned FALSE`, with `the take file /home/kruzzzzy/Documents/Zene Studio/zene...` not openable for writing. `Totals: 16 passed, 1 failed`. The sandbox cannot write the owner's default take folder. Environmental. |
| test-10a (private HOME) | `logs/test-10a.log` | `100% tests passed out of 1`. Run record: the private HOME is set for this run. The log does not show the environment. |
| test-10b | `logs/test-10b.log` | `100% tests passed out of 6` (names not in the log) |
| build-10 | `logs/build-10.log` | Build log; the family build (no summary line in the log) |

### Gates

| Gate | Log | Result |
| --- | --- | --- |
| feature_check (run 1) | `logs/gate-1.log` | `RESULT: PASS`, base `4eb8d6e8a`, 7 changed files, every check PASS. Run record: exit status not in log |
| feature_check (run 2) | `logs/gate-2.log` | `RESULT: PASS`, same base |
| file-length ratchet | `logs/gate-len.log`, `logs/gate-len-baseline.log` | `FAIL: file-length ratchet regressed` in both, with 14 `REGRESSION` entries (for example `src/core/TakeLane.cpp (520)`, `tests/src/core/ControlEditCommandsTest.cpp (511)`). None of the 14 is a file this branch changes. See Other findings. |

### Test matrix (from the packet's p1 matrix, not re-derived)

| Case | Covered | Gap |
| --- | --- | --- |
| Arm All arms, refuses undo, takes remain | Yes: regression test | |
| Refusal named as a separate outcome | Asserted inside the regression test only | No test row for "refused" on its own |
| Undo path with loaded history at the top | No | See Self-check 4 |

## Self-check

1. **RED and GREEN with exit codes.** Reverted: `logs/test-3.log` shows the `FAIL!` line and `1 failed` (quoted above). Run record: EXIT 8. Fixed: `logs/test-2.log` and `logs/test-4.log` show `100% tests passed out of 1`. Run record: EXIT 0. The exit codes are not in the logs.
2. **Deterministic.** A grep of `tests/src/core/UndoHonestyTest.cpp` for `sleep`, `QThread`, `QTimer`, `chrono`, `wait(` and `thread` matches only the test's own name line. No timing or sleep is used. The seams are: a `QTemporaryDir` working directory (line 409), three tracks created with `track.add`, and the route ids and take-file paths read from the reply of `track.set_arm_many`. I did not read the `run()` helper.
3. **Fix at the cause, siblings and callers.** The cause is the row's class and the arm-time `__transaction` (base line 385). Both are changed at HEAD. Callers: `src/gui/RecordLaunchMenus.cpp:71-82` (the Arm All menu; comment changed only) and `src/core/CollabPolicy.cpp:112`, where `track.set_arm_many` sits in `hostDeniedIds()`, so a host never runs it for a peer. The single-track sibling `track.set_arm` is in the same file. It still records a `__transaction` (line 229, inverse `armed: false`) and is still `RC::Snapshot, true` at `ControlReversibilityTableRecording.cpp:55-67`. Its row fallback says to delete the take, so it has the same defect class. **It is not fixed.** That was the packet's decision, and it stays open (see Other findings).
4. **No collateral damage.** The diff adds no check, no limit and no load or save path. None of the seven changed files is a save or load file. The one visible change: `control.undo` on an Arm All now refuses, where it used to disarm. The refusal names the route ids for `record.disarm_track`. The loaded-history branch `undoesLoadedHistory` (`ControlCommandsControl.cpp:388` predicate, `:402` branch) was not analysed beyond the regression's refusal. **Unverified**: whether a loaded-history top can reach a different outcome for this command.
5. **Verdict.** FIXED, not ALREADY-FIXED or FALSE-POSITIVE. The defect is shown at base lines 69 and 385 (see "Is the finding real").
6. **Sibling commands in the changed command file.** The changed file is `src/core/ControlCommandsArrangementState.cpp`. Of the named ids, only `arrangement.get_state` is registered there, and the diff does not touch its code (the hunks are the `setArmMany` comment, the arm branch and the description). The other named ids are registered elsewhere. `arrangement.fit_tracks`, `modes`, `zoom`, `nudge`, `scroll`, `set_tool` and `select_clip` occur in `src/core/ControlCommandsRange.cpp`, `ControlReversibilityTableArranger.cpp` and `ControlReversibilityTableCustomisation.cpp`; I did not check each one. `comp.audition` appears in `src/gui/TakeLanesPanel.cpp`. **Unverified**: `arrangement.zoom_back`, `arrangement.next_transient`, `arrangement.ruler_lanes`, `arrangement.modifier_tools`, `comp.assign`, and the per-id consistency of the others.
7. **Other ASK items.** No audio-thread file is in the diff. The changes are a command handler, a table row, a GUI comment and tests. No command is added: `ids_sha256` is `8e2d5b9a0774e7005e5832777c552b2d4f09efbc56e71322c5fd72e5f3385c6a` at both `4eb8d6e8a` and HEAD, and the snapshot diff is the three fields named above.

## Other findings

- **`track.set_arm` (single track) stays reversible, and its take file likely remains.** Its row fallback says "the take: delete it" (`ControlReversibilityTableRecording.cpp:55-67`). It is pinned by `tests/control-record-inputs.py:483-487` (arm recorded as snapshot with `track.set_arm` as inverse) and `tests/src/core/ControlEditCommandsTest.cpp:453-486` (arm, re-arm, disarm, missing). **Unverified**: that the take file survives an undo of `track.set_arm`. No log in this session exercises it.
- **No test for "refused" as its own outcome** (the gap in the matrix above).
- **Docs histogram, unverified.** `docs/RELEASE-NOTES-v0.3.0-alpha.md:999` and `:1002` say `rows=837`. The contract test's failure under mutation (`logs/test-8.log`, line 10) prints `828 rows = 416 true_inverse` and `837 rows = 417 true_inverse`. The log does not say which side is measured, nor why the count differs under the mutation. Unverified.
- **Real-HOME dependence of `ControlEditCommandsTest`.** Its track.set_arm_many test fails on the real HOME in this sandbox (`logs/test-1-extra.log`, line 634) and passes under a private HOME (`logs/test-10a.log`). The branch did not change that test. It is environmental, but it is a test of the changed command.
- **Gate FAILs.** The file-length ratchet fails (`logs/gate-len.log`, `logs/gate-len-baseline.log`) with 14 entries. None is in a file this branch changes: the changed files are `ControlCommandsArrangementState.cpp`, `ControlReversibilityTableRecording.cpp`, `RecordLaunchMenus.cpp`, `UndoHonestyTest.cpp` and the release notes. The packet says p4 classified these as pre-existing. I did not see p4's classification or a base-commit log, so **unverified** as pre-existing by me.
- **`comp.assign`** was not located in the code. **Unverified** that it is consistent.
- **Skips.** `UndoHonestyTest` skips four tests with "this build loads no effect" (`logs/test-3.log`). This is the build's effect configuration. It is unrelated to this change.
