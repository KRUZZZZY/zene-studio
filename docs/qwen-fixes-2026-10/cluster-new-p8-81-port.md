ALREADY-FIXED

# Cluster new-p8-81-port: r7 verification of train 37

Round 7, 2026-11-21. The finding is real at base `4eb8d6e8a`, and the accepted fix is already in train 37 (`50b48f5451`, `a109eb75d3`). The merge `c89acade35` brought the train's versions of all six affected files into this branch. This round added no product code: the change is this report.

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/new-p8-81-port.r7/logs`, cited as `logs/<name>`. The build/test/gate log files do not contain EXIT lines; the exit values in the table are the process statuses of those recorded runs, as computed below. The one exit code this round saw printed directly is the p5 re-run, `exit 0` (quiet runner, log under `duet/10-worker/tmp/001-p5-green-undo.log`).

## Finding and provenance

- Base defect, row: `4eb8d6e8a:src/core/ControlReversibilityTableRecording.cpp:69` is `R("track.set_arm_many", RC::Snapshot, true,` (the `grep` of the train patch, `logs/fix-src.patch:84`, shows that removed line).
- Base defect, inverse: `4eb8d6e8a:src/core/ControlCommandsArrangementState.cpp:385` inserted the per-track `__transaction` (`logs/fix-src.patch:59`), which is what made the command look reversible.
- Train 37 commit `50b48f5451f9e7ae6353b4418e851a0a006b60b9` ("Fix cluster new-p8-81-port-port: track.set_arm_many is irreversible"); sibling commit `a109eb75d342a749c76791bd86ce70c865298d50` ("Fix cluster new-p8-81-port-port: track.set_arm is irreversible too").
- Merge commit `c89acade35c4a12673aa9035a4adf91b5ae03f7b` ("Merge 050/m0-train-37 into bugs/qwen-new-p8-81-port (cluster new-p8-81-port self-heal)").
- **Case A: the merge took the train's versions for the six files** (not Case B). `logs/train-match-1.log` records `EXIT=0` for every file against train-37 tip `a74b2c4da9f8169ff4d358e9b223d84f9c5d6b99`:
  `docs/RELEASE-NOTES-v0.3.0-alpha.md`, `src/core/ControlCommandsArrangementState.cpp`, `src/core/ControlReversibilityTableRecording.cpp`, `src/gui/RecordLaunchMenus.cpp`, `tests/src/core/UndoHonestyTest.cpp`, `tools/mcp-zene-control/zene_control/commands_snapshot.json`.

## File:line evidence from p1 (reversibility and takes)

- Reversibility class at HEAD: `src/core/ControlReversibilityTableRecording.cpp:113` is `R("track.set_arm_many", RC::Irreversible, false,`. The sibling is `:95`, `R("track.set_arm", RC::Irreversible, false,`.
- Take handling in that row, `ControlReversibilityTableRecording.cpp:114-120`: "arming the named routes creates WAV take files and journals beside them. Disarming retires the journals, but the take files remain on disk, outside the project state this command can restore"; "no complete inverse exists ... The registry refuses control.undo"; the documented partial action is `record.disarm_track` plus keeping or deleting each take at the reply's path.
- Handler at HEAD: `src/core/ControlCommandsArrangementState.cpp:386` is `one.result.remove(QStringLiteral("__transaction"));` with the comment "the per-track reply carries no transaction payload: neither arm verb records an inverse". No `__transaction` insert remains in that file.
- Regression test: `tests/src/core/UndoHonestyTest.cpp:435` asserts `!undo.ok` with the message "control.undo claimed it could remove the takes from track.set_arm_many"; `:437` asserts the message names `track.set_arm_many`; `:438` asserts `tracksStillArmed && takesRemain`, so the refused undo changed neither the armed tracks nor the take files.
- The train patch that makes these lines current is `logs/fix-src.patch` (commit `50b48f5451`): `:94` adds the `RC::Irreversible, false` row and `:59` removes the `__transaction` insert.

## Verification that the merged tree matches train 37

`logs/train-match-1.log`, one `git diff --quiet HEAD a74b2c4d -- <file>` per file, all `EXIT=0` (listed above). HEAD also contains the later train-38 merge `b74ee24dbd`, but none of the six fix files differs from train-37 tip `a74b2c4d`; the empty per-file diff is the proof.

## Commands and EXIT codes (from the r7 logs)

| Step | Command (as recorded in the log) | EXIT | Log |
| --- | --- | --- | --- |
| build-1 | `cmake --build build --target zene UndoHonestyTest ReversibilityContractTest` (targets from the log; the command line is not echoed) | 0 | `logs/build-1.log` |
| test-1 (baseline) | `ctest` over the three tests in `build/tests` (command line not echoed) | 0 | `logs/test-1.log`: `3/3` passed, `100% tests passed out of 3` |
| build-2 (RED) | `cmake --build build --target UndoHonestyTest ReversibilityContractTest` | 0 | `logs/build-2.log` |
| test-2 (RED) | `./UndoHonestyTest armAllRefusesUndoThatWouldLeaveItsTakesBehind` | 1 | `logs/test-2.log:19` `FAIL! ... '!undo.ok' returned FALSE`; totals `2 passed, 1 failed` |
| build-3 (GREEN) | `cmake --build build --target UndoHonestyTest ReversibilityContractTest` | 0 | `logs/build-3.log` |
| test-3 (GREEN) | `./UndoHonestyTest armAllRefusesUndoThatWouldLeaveItsTakesBehind` | 0 | `logs/test-3.log`: totals `3 passed, 0 failed` |
| gates-1 | `feature-check kads-new-p8-81-port` (base `fa4a5f2e7`, 170 changed files) | 0 | `logs/gates-1.log`: `RESULT: PASS`, all 8 gates `PASS` |
| p5 re-run (GREEN) | quiet runner: `./UndoHonestyTest armAllRefusesUndoThatWouldLeaveItsTakesBehind` | 0 | `duet/10-worker/tmp/001-p5-green-undo.log`: `exit 0`, `3 passed, 0 failed` |

The direct QtTest binary returns the number of failed tests, so the single RED failure is `1`; `test-2.log` records that failure but no EXIT line. Build-1 and test-1 likewise record success but no EXIT line. Ctest over the three tests would report failure code 8 only because a ctest run aggregates differently; this round's RED ran the binary directly.

## Self-check

Answers to the six `?` items in `logs/submit-1.log` (the same six in `logs/submit-pre.log`).

1. **RED and GREEN with exit codes.** RED: `logs/test-2.log:19` `FAIL!` on `!undo.ok`, totals `2 passed, 1 failed`, EXIT 1. GREEN: `logs/test-3.log` totals `3 passed, 0 failed`, EXIT 0; confirmed live this round by the p5 re-run, `exit 0`. Both builds `EXIT 0`.
2. **Deterministic.** The test uses `QTemporaryDir takes;` at `tests/src/core/UndoHonestyTest.cpp:409`, so its take folder is private per run. `logs/determinism-grep.log` is empty (0 lines, `grep` exit 1) for `sleep|QThread|QTimer|chrono|wait\(|thread`, so the file has no sleep, thread, timer or wait seam. The only inputs are the three `track.add` tracks and the routes/take paths read from the arm reply.
3. **Fix at the cause and complete.** The cause is the base row `ControlReversibilityTableRecording.cpp:69` (`RC::Snapshot, true`) and the base per-track `__transaction` at `ControlCommandsArrangementState.cpp:385`. HEAD has the row `RC::Irreversible, false` at `:113`, the sibling `track.set_arm` at `:95`, and no `__transaction` insert (the reply removes the payload at `:386`). The six-file tree matches train 37 (`logs/train-match-1.log`, all `EXIT=0`). The GUI menu caller and test caller were audited in r5; the r5 logs are not on disk, so caller re-audit this round is **unverified**.
4. **No collateral damage.** `logs/test-1.log` passes `ReversibilityContractTest`, `UndoHonestyTest` and `ControlCommandsSnapshot`, 3/3; `logs/gates-1.log` reports `RESULT: PASS` for all eight gates. The fix adds no limit or check: it changes the reversibility class/description, removes the `__transaction` payload, and updates comments/tests. Old project save/load was not exercised by any r7 log, so "old projects still load" is **unverified** this round.
5. **ALREADY-FIXED evidence.** The code at `src/core/ControlReversibilityTableRecording.cpp:113` is `RC::Irreversible, false`, the refusal loop in `tests/src/core/UndoHonestyTest.cpp:435-438` requires `!undo.ok`, the `Irreversible` error kind, the command name, and surviving tracks/takes, and the fix is train 37 (`50b48f5451`, `a109eb75d3`) merged by `c89acade35` with the six files matching train-37 tip (Case A). The base row and `__transaction` cited above show the defect was real before the train fix.
6. **Sibling commands.** `submit_check.py` derives these 15 from the changed command files (`logs/submit-1.log`): `app.about`, `app.ask`, `app.help`, `app.online_help`, `app.task_cancel`, `app.tasks`, `app.tour`, `app.tours`, `app.update_check_get`, `app.update_check_run`, `app.update_check_set`, `app.version`, `app.whats_new`, `arrangement.fit_tracks`, `arrangement.get_state`. None is touched by the fix hunks in `logs/fix-src.patch`, which change only the `track.set_arm_many`/`track.set_arm` reversibility rows, the arm handler's `__transaction` payload, the registration descriptions and the test. The `app.*` commands are help/update/version/task lifecycle commands and the two `arrangement.*` commands do not use the arm rows, so all remain consistent with the fix.

## Other findings

- **EXIT lines are absent from the r7 build/test logs.** The codes in the table are the process statuses of the recorded runs; only the p5 re-run printed `exit 0`. The RED binary's `1` follows from QtTest returning the failure count (`2 passed, 1 failed`).
- **Environment noise in the logs.** `build-2.log`/`build-3.log` contain `hwloc/linux: failed to find sysfs cpu topology directory`; `test-2.log`/`test-3.log` contain repeated `Fontconfig error: No writable cache directories`. Neither changed a test result.
- **gates-1 scope.** `logs/gates-1.log` records 170 changed files against base `fa4a5f2e7`; the complexity and file-length rows say `PASS no new item in your files`. The gate ran and printed `RESULT: PASS`.
- **Wider-family crash not re-run.** Round 6 recorded `ReversibilityUndoDeviceTest` SEGFAULTing in the LADSPA/Effect restore path. No r7 log runs the wider family, so that result is **unverified** this round; the focused three tests are green.
- **submit check.** `logs/submit-pre.log` is `RESULT: PASS - submit` with no FAIL item. The final run is `logs/submit-1.log` at this report commit; any proof artifact is p6's, not claimed here.
