FIXED

# Cluster new-p8-81-port: r2 re-proof

Verdict: **FIXED**. The branch's own commits are the fix: `70a924a8a2` (product fix and regression test), `a6d4c81a4e` (command snapshot regenerated from the fixed registry), `4e72ea61d7` (regression test also asserts that all three take files exist).

Finding: "`track.set_arm_many` is marked reversible although undo leaves its takes behind."

Evidence source: the logs of this round in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-81-port.r2/logs` (cited as `logs/<name>`). No result from an earlier round is used here.

**Exit codes:** the EXIT values below are the codes the commands printed when they were run. The logs themselves do not contain them. Quoted result lines are from the logs.

## Is the finding real (base `4eb8d6e8a`)

- `git show 4eb8d6e8a:src/core/ControlReversibilityTableRecording.cpp`, line 69: `R("track.set_arm_many", RC::Snapshot, true, ...)`.
- `git show 4eb8d6e8a:src/core/ControlCommandsArrangementState.cpp`, line 385: the arm path inserts a `__transaction` into the command's result.
- The RED log shows the defect with the fix reverted. `logs/test-3.log`, line 19: `FAIL!  : UndoHonestyTest::armAllRefusesUndoThatWouldLeaveItsTakesBehind() '!undo.ok' returned FALSE. (control.undo claimed it could remove the takes from track.set_arm_many)`

## The fix at HEAD

The diff against `4eb8d6e8a` changes seven files: six code, test, doc or snapshot files, plus this report.

| File (HEAD) | Change |
| --- | --- |
| `src/core/ControlReversibilityTableRecording.cpp:69` | Row is now `RC::Irreversible, false`. The fallback text says to stop each route with `record.disarm_track` using the route ids in the reply. |
| `src/core/ControlCommandsArrangementState.cpp:363-365` | Doc comment rewritten. |
| `src/core/ControlCommandsArrangementState.cpp:381` | The `if (armed)` block that inserted the `__transaction` (base lines 382-386) is removed. |
| `src/core/ControlCommandsArrangementState.cpp:395-397` | Command description: `control.undo` refuses this command and names `record.disarm_track`. No command added or removed. |
| `src/gui/RecordLaunchMenus.cpp:65-67` | Comment only. |
| `docs/RELEASE-NOTES-v0.3.0-alpha.md:999,1002` | A16 histogram now reads `snapshot=41 irreversible=23`. |
| `tests/src/core/UndoHonestyTest.cpp:406-439` | Regression test `armAllRefusesUndoThatWouldLeaveItsTakesBehind`. It uses a `QTemporaryDir` working directory (line 409). |
| `tools/mcp-zene-control/zene_control/commands_snapshot.json` | Changed in commit `a6d4c81a4e`. Three fields changed (not re-read in this round). |

The option taken is the typed refusal: the row becomes `Irreversible`.

## Evidence

### Reproducing the RED and GREEN runs

The fix patch covers the two product files only (`grep '^diff --git'` on `logs/fix.patch` lists `ControlCommandsArrangementState.cpp` and `ControlReversibilityTableRecording.cpp`). The steps, run from the worktree root with the tree at HEAD:

```
LOG=/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-81-port.r2/logs
# from the worktree root; build-2 is the HEAD run, build-3 the reverted run, build-4 the restored run
cmake --build build --target zene UndoHonestyTest > $LOG/build-2.log 2>&1
cd build/tests && ctest -R UndoHonestyTest --output-on-failure > $LOG/test-2.log 2>&1
cd ../..
git diff 4eb8d6e8a HEAD -- src/core/ControlReversibilityTableRecording.cpp src/core/ControlCommandsArrangementState.cpp > $LOG/fix.patch
git apply -R --check $LOG/fix.patch
git apply -R $LOG/fix.patch
cmake --build build --target zene UndoHonestyTest > $LOG/build-3.log 2>&1
cd build/tests && ctest -R UndoHonestyTest --output-on-failure > $LOG/test-3.log 2>&1
cd ../..
git apply $LOG/fix.patch
cmake --build build --target zene UndoHonestyTest > $LOG/build-4.log 2>&1
cd build/tests && ctest -R UndoHonestyTest --output-on-failure > $LOG/test-4.log 2>&1
cd ../..
```

These commands do not reproduce the mutation runs (build-5 to build-9); the logs do not record the diff applied for each mutation.

### Log to step mapping

The State column comes from the step list, not from the logs. The logs show the build target lines and the test result lines.

| Step | Build log (target lines) | Test log (result) | State | EXIT printed |
| --- | --- | --- | --- | --- |
| build-1 | `[100%] Built target ControlEditCommandsTest` (UndoHonestyTest and ReversibilityContractTest also built) | test-1-extra: `1/3 Test #55: ControlEditCommandsTest ...***Failed`; `67% tests passed, 1 tests failed out of 3`; `Totals: 16 passed, 1 failed, 0 skipped` (see Other findings) | family, real HOME | not recorded |
| (no build step) | — | test-1: `1/1 Test #274: UndoHonestyTest ... Passed`; `100% tests passed out of 1` | not recorded | not recorded |
| build-2 | `[100%] Built target UndoHonestyTest` | test-2: `1/1 Test #274: UndoHonestyTest ... Passed`; `100% tests passed out of 1` | GREEN on HEAD | 0 |
| build-3 | `[100%] Built target UndoHonestyTest` | test-3: `1/1 Test #274: UndoHonestyTest ...***Failed`; `FAIL!  : UndoHonestyTest::armAllRefusesUndoThatWouldLeaveItsTakesBehind() '!undo.ok' returned FALSE.`; `Totals: 12 passed, 1 failed, 4 skipped` | RED: fix reverted (`git apply -R`) | 8 |
| build-4 | `[94%] Linking CXX executable UndoHonestyTest`, then `[100%] Built target UndoHonestyTest` | test-4: `1/1 Test #274: UndoHonestyTest ... Passed`; `100% tests passed out of 1` | GREEN after restoring the patch (`git apply`) | 0 |
| build-5 | `[100%] Built target UndoHonestyTest` | test-5: `100% tests passed out of 1` | row-only mutation (`RC::Snapshot`, true at `ControlReversibilityTableRecording.cpp:69`); test passed | 0 |
| build-6 | `[100%] Built target UndoHonestyTest` | test-6: `100% tests passed out of 1` | handler-only mutation (restored `__transaction` insertion; row kept `Irreversible`); test passed | 0 |
| build-7 | `[100%] Built target UndoHonestyTest` | test-7: `100% tests passed out of 1` | HEAD restored and rerun | 0 |
| build-8 | `[100%] Built target ReversibilityContractTest`, `[100%] Built target UndoHonestyTest` | test-8: `1/1 Test #167: ReversibilityContractTest ...***Failed`; `Totals: 8 passed, 1 failed, 0 skipped` | row-only mutation | 8 |
| build-9 | `[100%] Built target ReversibilityContractTest`, `[100%] Built target UndoHonestyTest` | test-9: `1/1 Test #167: ReversibilityContractTest ... Passed`; `100% tests passed out of 1` | HEAD restored | 0 |
| build-10 | `[100%] Built target ControlEditCommandsTest` (UndoHonestyTest and ReversibilityContractTest also built) | test-10a (private HOME): `1/1 Test #55: ControlEditCommandsTest ... Passed`; `100% tests passed out of 1`. test-10b: `100% tests passed out of 6`: ReversibilityContractTest (#167), ReversibilityUndoTest (#168), ReversibilityUndoDeviceTest (#169), UndoHonestyTest (#274), UndoHonestyStateTest (#275), ControlCommandsSnapshot (#574) | family (test-10a on a private HOME) | not recorded |

### Mutation notes

- test-5 (row-only mutation): the regression test passes, so this test does not catch that mutation.
- test-6 (handler-only mutation): the regression test passes. A reading of the code, not shown by the log: `src/core/ControlRegistry.cpp:563` marks the transaction irreversible when the row is `Irreversible`, and `src/core/ControlCommandsControl.cpp:402-405` returns the typed refusal, so the regression still sees a refusal.
- test-8 (row-only mutation, contract test): `ReversibilityContractTest` fails on the histogram (see Histogram below).
- The mutation descriptions in the table come from this round's step plan, not from the logs.

### Test matrix

| Case | Where | Status |
| --- | --- | --- |
| Normal: three tracks armed, `control.undo` refused, takes remain | `armAllRefusesUndoThatWouldLeaveItsTakesBehind` | Covered. Passes at HEAD (test-2, test-4, test-7); fails with the fix reverted (test-3). |
| Empty `tracks` gives `InvalidArgs` | same test, line 439 | Covered (asserted in the same test). |
| All tracks already armed gives `Busy` | same test, line 426 | Covered (asserted in the same test). |
| Refused as its own outcome | asserted only inside the normal case, lines 435-437 | Not covered as a separate case. |
| Undo with loaded history at the top | none | Not covered by any log here. |

### Gates

- **gate-1.log** (feature_check, file time 00:35:17): eight rows, all `PASS`. The complexity row reads `PASS  complexity  no new item in your files`. `RESULT: PASS`. The log has no lizard line. `lizard-install.log` was written at 00:35:55, after this run, so by timestamp this run had no lizard. The log does not say the complexity check was skipped, so this PASS row is a gate-reporting defect (see Other findings).
- **gate-2.log** (feature_check, file time 00:36:37, after the lizard venv at `lizard-venv` was written at 00:36:17): eight rows, all `PASS`. Complexity and file-length both read `no new item in your files`. `RESULT: PASS`. The log contains no `FAIL` line. The lizard version is not shown in the logs.
- **File-length gate** (`logs/gate-len.log`, file time 00:24:37, and `logs/gate-len-baseline.log`): `file-length-gate: 1790 fork-scope sources measured; 19 exceed 500 code lines`. Each lists 14 `REGRESSION: new file over 500 lines` entries and ends with `FAIL: file-length ratchet regressed` (line 38 in each). The 14 files are `src/core/SmfInterchangeReader.cpp`, `src/core/TakeLane.cpp`, `tests/src/core/CollabTransportTest.cpp`, `tests/src/core/ControlAutomationScriptTest.cpp`, `tests/src/core/ControlDeviceCatalogueTest.cpp`, `tests/src/core/ControlEditCommandsTest.cpp`, `tests/src/core/CustomAccessTest.cpp`, `tests/src/core/DeviceDspTest.cpp`, `tests/src/core/HardwareIoTest.cpp`, `tests/src/core/NotePlaybackTest.cpp`, `tests/src/core/RackMacrosTest.cpp`, `tests/src/core/RevisionTimelineTest.cpp`, `tests/src/core/VideoTrackTest.cpp`, `tests/src/gui/ArrangerTimelineTest.cpp`.
- **Complexity FAIL** (68 functions over CCN 10, 7 REGRESSION entries): this text is not in any log in the logs directory. **Unverified.**
- `logs/regression-files.txt` lists 21 paths. The logs do not record where that list came from. None of the 21 is a file this branch changes: the seven files in `git diff --name-only 4eb8d6e8a HEAD` have no match in the list (checked with `grep -Fx`).
- `UndoHonestyTest.cpp`: its name does not appear in either gate-len log, so it is not among the files the gate reports as over 500. The gate prints no count for it. **Unverified:** the count of that file.

### Histogram

- `docs/RELEASE-NOTES-v0.3.0-alpha.md` line 999: `rows=837 true_inverse=417 snapshot=41 irreversible=23 not_mutating=356` (`DECLARED rows=837 entries=837 duplicates=0`). Line 1002, inside the A16 comment block: `measured: rows=837 true_inverse=417 snapshot=41 irreversible=23 not_mutating=356`. The four counts sum to 837.
- `logs/test-8.log` line 10: the histogram test fails under the row-only mutation. The message says "the table measures 828 rows = 416 true_inverse + 39 snapshot + 22 irreversible + 351 not_mutating" and that the release notes "publish 837 rows = 417 true_inverse + 41 snapshot + 23 irreversible". The four measured counts sum to 828.
- At HEAD (test-9, HEAD restored) the same test passes, so the HEAD measurement matches what the test expects.
- **Unverified:** why the mutated run measures 828. The logs do not show the mutation. A change that only moves one row between classes would keep the total at 837, so the 9-row gap is not explained by the logs.

## Self-check

Answers to the six checks in `logs/submit-1.log`.

1. **RED and GREEN with exit codes.** Reverted: `logs/test-3.log` line 19 (quoted above), `Totals: 12 passed, 1 failed, 4 skipped`; EXIT 8. Fixed: `logs/test-2.log` and `logs/test-4.log`, each `100% tests passed out of 1`; EXIT 0. `logs/test-7.log` (HEAD restored and rerun) is also `100% tests passed out of 1`; EXIT 0.

2. **Deterministic.** `grep -n "sleep\|QThread\|QTimer\|chrono\|wait(\|thread" tests/src/core/UndoHonestyTest.cpp` at HEAD prints no lines. The seams are a `QTemporaryDir` used as the working directory (line 409), three tracks made by `track.add` with type `sample`, and the route ids and take-file paths read from the reply of `track.set_arm_many`. The `run()` helper was not read in this round.

3. **Fix at the cause and complete.** The cause is the row class at `ControlReversibilityTableRecording.cpp:69` (base: `RC::Snapshot, true`) and the arm-time `__transaction` (base line 385). Both are changed at HEAD. Callers checked by grep: `src/gui/RecordLaunchMenus.cpp:71-82` (Arm All; only the comment at 65-67 changed), `src/core/CollabPolicy.cpp:112` (`track.set_arm_many` in a list of ids; the function that holds the list was not identified in this round), and `tests/src/core/ControlEditCommandsTest.cpp:619-632` (see Other findings). Sibling: `track.set_arm` (single track) is `RC::Snapshot, true` at `ControlReversibilityTableRecording.cpp:55`, unchanged, and its arm path inserts a `__transaction` at `ControlCommandsArrangementState.cpp:229`. **Not fixed in this branch.** Whether its undo leaves take files is **unverified**.

4. **No collateral damage.** No log in this round loads or saves a project, so loading of old projects is **unverified**. The changed hunks add no check or limit (see the fix table). The one behaviour change: `control.undo` on an Arm All now refuses instead of disarming, and the refusal message names `track.set_arm_many` (asserted at `UndoHonestyTest.cpp:437`). The loaded-history branch is `ControlCommandsControl.cpp:403` (`undoesLoadedHistory`, defined at line 384), reached from `answerIrreversibleTop` (line 400). No log exercises it. Whether its outcome changes for this command is **unverified**.

5. **Verdict.** FIXED. ALREADY-FIXED and FALSE-POSITIVE do not apply. The defect is shown by `logs/test-3.log` line 19 and by base lines 69 and 385.

6. **Sibling commands.** None of these is touched by this diff: `arrangement.fit_tracks`, `arrangement.get_state`, `arrangement.modes`, `arrangement.modifier_tools`, `arrangement.navigator`, `arrangement.next_transient`, `arrangement.nudge`, `arrangement.ruler_lanes`, `arrangement.scroll`, `arrangement.select_clip`, `arrangement.set_tool`, `arrangement.zoom`, `arrangement.zoom_back`, `comp.assign`, `comp.audition`. A diff line that touches a command would have to contain its name, and no changed code or snapshot line contains any of these names (the only matches are in this report's own text). In the changed command file, `arrangement.get_state` is registered at line 56, outside the changed lines (363-365, 381, 395-397).

7. **Other checks.** Not applicable: the list in `logs/submit-1.log` has six checks, and the six above are all of them.

## Other findings

- **`track.set_arm` (single track) stays reversible.** `ControlReversibilityTableRecording.cpp:55`; not changed by this branch. **Unverified:** that its take file survives an undo. No log exercises it.
- **No test for "refused" as its own outcome.** See the test matrix.
- **Histogram reconciliation.** 837 (published) and 828 (measured under the mutation) are not reconciled by the logs. See Histogram.
- **Real-HOME dependence of `ControlEditCommandsTest`.** `logs/test-1-extra.log` (real HOME, `Totals: 16 passed, 1 failed, 0 skipped`) fails `ControlEditCommandsTest.cpp:634` in `trackSetArmManyReportsOnlyTheTracksItChanged()` with `'armed.ok' returned FALSE` ("no track was armed: route 10 could not be armed: the take file ..."), a take file in the default take folder. `logs/test-10a.log` (private HOME) passes. This branch did not change the test. It is environmental, but it tests the changed command.
- **Gate reporting defect.** `logs/gate-1.log` reports the complexity row as `PASS` (`no new item in your files`) when lizard was not installed at that run (by timestamp; see Gates). A check that did not run should not report PASS.
- **File-length gate FAIL.** `logs/gate-len.log` and `logs/gate-len-baseline.log` report 14 REGRESSION entries; none is a file this branch changes (see Gates).
- **Complexity gate FAIL.** The 68-function complexity result is not in any log here. **Unverified.**
- **Skips.** `logs/test-3.log` has four skips: three say "this build loads no effect" (lines 12, 14, 16) and one says "this build has no amplifier or bass booster module" (line 29). The messages name build content, not this change.
- **`comp.assign`.** Not checked in this round. Its name appears on no changed line (see self-check 6).
- **Submit check.** `logs/submit-1.log` ends with `RESULT: PASS - submit`. The log does not say which commit it checked. It is to be rerun at the final commit (step p6).
- **`logs/test-1.log`** (UndoHonestyTest, `100% tests passed out of 1`) is not assigned to a step in the step list. **Unverified** what it was for.
