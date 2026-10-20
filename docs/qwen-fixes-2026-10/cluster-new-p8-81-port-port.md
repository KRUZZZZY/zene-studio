# Cluster new-p8-81-port-port

Verdict: **FIXED** (port of the accepted fix from `bugs/qwen-new-p8-81-port`, written by hand onto this branch off the newest line `050/m0-train-8`; no merge, cherry-pick or rebase. Round 2 answered the QA REWORK: the single-track sibling `track.set_arm` is now consistent with the batch).

Finding: "`track.set_arm_many` is marked reversible although undo leaves its takes behind" (`src/core/ControlReversibilityTableRecording.cpp:80`, historical line numbers).

## Is the finding real on this branch?

Yes. The newest line still carried the defect when this work started:

- `src/core/ControlReversibilityTableRecording.cpp:69-78` (base `2da2ed356`): `R("track.set_arm_many", RC::Snapshot, true, ...)` with the inverse "ONE recorded undo step - this command with armed: false over exactly the tracks the call armed ...".
- `src/core/ControlCommandsArrangementState.cpp:401-405` (base `2da2ed356`, handler `setArmMany`): on arming, the handler inserted a `__transaction` whose inverse disarmed the batch's tracks, so `control.undo` succeeded and disarmed the tracks while the take WAV files stayed on disk.
- The command's own description (`src/core/ControlCommandsArrangementState.cpp:414-415`, base) promised "Arming is ONE undo step: control.undo disarms exactly the tracks this call armed (their takes stay on disk)" - the same contradiction the finding names.

`git merge-base --is-ancestor 70a924a8a HEAD` -> exit 1 (the accepted fix is not in this tree), so the port was needed.

The RED run below reproduces exactly that: on the unfixed product `control.undo` returns `ok` and the regression fails on it.

## Round 2 (the QA REWORK: the sibling command was left inconsistent)

The round-1 review returned **REWORK**: the batch fix was correct, but `track.set_arm` - the matching single-track command - was still marked reversible while its recorded inverse only disarmed the route and left the take WAV behind. That is the same transaction defect on the sibling, so the two rows now say the same thing.

What changed on top of round 1:

| File | Change |
| --- | --- |
| `src/core/ControlReversibilityTableRecording.cpp:86-101` | The `track.set_arm` row is now `RC::Irreversible, false` and moved into the file's `irreversible` section (the `snapshot` section is empty now, so its header is gone). Reason: the take file stays on disk once the capture stops, outside the project state a checkpoint could restore. Mechanism: no complete inverse exists, so `control.undo` refuses. Fallback: stop the capture with `track.set_arm` `armed: false` (or `record.disarm_track`); keep or delete the take where the reply says it is. |
| `src/core/ControlCommandsArrangementState.cpp:176-226` | `armTrackArm` no longer records the partial-disarm inverse: the `__transaction` insertion (the `before`/`inverseArgs` pair and `control::commandTransaction`) is removed, and the function's comment says why. The registry then writes the honest record ("this command recorded no inverse or snapshot") and `control.undo` refuses the command typed. |
| `src/core/ControlCommandsArrangementState.cpp:289-292` | The `track.set_arm` description no longer claims "Arming is reversible"; it says the take file stays on disk, so `control.undo` refuses the command, and names the way back (`armed: false`, or `record.disarm_track`). |
| `src/core/ControlCommandsArrangementState.cpp:361` | The per-track `__transaction` strip comment now says what it is for: the per-track reply carries no transaction payload, because neither arm verb records an inverse. |
| `include/ControlReversibility.h:238-241` | Comment only: the recording group file's description no longer says the reclassified `track.set_arm` is "a real arm verb with a command inverse"; it says the take file outlives the capture, so it records no inverse. |
| `docs/RELEASE-NOTES-v0.3.0-alpha.md:1023, 1026, 2202-2210, 2219-2225` | The A16 histogram moves the second row (`snapshot=41 irreversible=23` -> `snapshot=40 irreversible=24`), in the published block and in the measurement line right above it; the two prose mentions of the rows follow the table. `ReversibilityContractTest` re-measures the table and compares it with the published figure on every run. |
| `docs/RECORD-INPUTS.md:74, 104-109` | The command table's class for `track.set_arm` is `irreversible` (it was `snapshot`), and the proof line says the undo is refused because the take stays on disk. |
| `docs/A16-REVERSIBILITY.md:502-514` | A dated note (2026-10-20, this cluster) records the second move of both rows, why the recorded inverse was not an inverse, and what the refusal says. The 2026-09-15 measurement above it is that lane's record and is kept as written. |
| `tools/mcp-zene-control/zene_control/commands_snapshot.json` | DERIVED file, regenerated from a live instance of the fixed binary with the bridge's own generator (`tools/mcp-zene-control/snapshot_commands.py --socket ...`), never hand-written: the one `track.set_arm` description plus `captured_at` and the instance provenance. The id surface is unchanged (`ids_sha256 8995baf1...`, 833 ids / 76 groups). |
| `tests/src/core/ControlArmReversibilityTest.cpp` (NEW) | The single-track regression: `armingOneTrackRefusesUndoThatWouldLeaveItsTakeBehind` - arm one track, then prove that `control.undo` refuses it typed (`irreversible`, the message names `track.set_arm`) while the route stays armed and the take file stays on disk. |
| `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt` | The new test is registered in `LMMS_TESTS` (which is what creates its executable and its ctest), in the fork-sources manifest, and in the whole-tree manifest - re-derived from that file's own recipe, never hand-edited. |
| `tests/control-record-inputs.py` | The live-binary transcript now proves the NEW contract for the single arm: `control.undo` is refused typed, the route and the take survive the refusal, the disarm stops the capture and retires the journal and the take stays, and the `control.transactions` record is `irreversible` with no command inverse. |
| `tests/upstream-modifications.txt` | Four lines for the inherited `tests/CMakeLists.txt` change and for the three files this repo already declares there (`docs/RELEASE-NOTES-v0.3.0-alpha.md`, `include/ControlReversibility.h`, `tests/all-sources.txt`). |

No control command was added or removed (the id surface is byte-identical), no new class of check was introduced, and the audio path is untouched: the change is a table row, a description, a comment, and the removal of one JSON object built in the control handler.

## Round 1 (the port of the accepted fix): what was changed, and why this option

The finding offered two options: mark the row irreversible, or make the inverse remove and redo the takes. I took the first, as the accepted fix did. A take is a file on disk outside the project state that `control.undo` restores, so "remove and redo" would have to delete files a user may already have recorded into; that is a design call for the owner. The honest registry answer is a typed refusal.

| File | Change |
| --- | --- |
| `src/core/ControlReversibilityTableRecording.cpp:104-111` | The `track.set_arm_many` row is now `RC::Irreversible, false`; the reason says the take files outlive the captures and are outside the project state, the mechanism says no complete inverse exists, the fallback names `record.disarm_track` and the route's take path. |
| `src/core/ControlCommandsArrangementState.cpp:378-380` | Handler comment corrected (no more "ONE undo step"). |
| `src/core/ControlCommandsArrangementState.cpp:399-401` | The `__transaction` insertion on arm is removed: the batch records no inverse. The registry then writes the honest record and `control.undo` refuses it typed. |
| `src/core/ControlCommandsArrangementState.cpp:414-416` | The command description no longer promises a one-step undo; it says `control.undo` refuses the command and names `record.disarm_track`. No command added or removed. |
| `src/gui/RecordLaunchMenus.cpp:65-67` | Comment only: the Arm All menu no longer claims one undo step takes the arm back. |
| `docs/RELEASE-NOTES-v0.3.0-alpha.md:946-948` | The wave-1 line follows the table (`track.set_arm_many` is `irreversible`). |
| `tests/src/core/UndoHonestyTest.cpp:405-440` | Regression test `armAllRefusesUndoThatWouldLeaveItsTakesBehind` replaces `armingSeveralTracksIsOneStep` (the old test asserted the undo that is now refused). It runs in a private working directory, so its takes do not depend on `~/Documents`. The header comment is updated to match. |

### Why the refusal is honest, and what the sibling rows do

- The contract class is the single source of truth: `ControlRegistry::stampContract` (`src/core/ControlRegistry.cpp:543-571`) drops a handler's reversible claim when the table says `irreversible`, and `ControlCommandsControl.cpp:400-406` turns a non-reversible top record into the typed refusal `ControlErrorKind::Irreversible` whose message names the command, its class and the row's fallback.
- The single-track sibling `track.set_arm` is now `irreversible` too (round 2): same reason, same fallback, same typed refusal as the batch. There is no contract distinction left to document - the two rows differ only in how many routes they arm.
- `record.disarm_track` and `record.disarm_all` were already `irreversible` and are unchanged.
- Nothing new was added to the audio thread: the change is a table row, comments, and the removal of one JSON object built in the control handler (not on the audio path). `control.undo` stays ONE honest step: it now refuses instead of half-undoing.

## Evidence table

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-81-port-port.r1/logs` (named below by file).

| Evidence | Exact command | EXIT / count |
| --- | --- | --- |
| RED, round 1: the ported batch regression on the UNFIXED product | `cmake --build build --target UndoHonestyTest -j8 > logs/build-1.log 2>&1`; `cd build/tests && ctest -R UndoHonestyTest --output-on-failure > logs/test-1.log 2>&1` | build EXIT **0**; test EXIT **8**; `FAIL! : UndoHonestyTest::armAllRefusesUndoThatWouldLeaveItsTakesBehind() '!undo.ok' returned FALSE. (control.undo claimed it could remove the takes from track.set_arm_many)` at `UndoHonestyTest.cpp(435)`; `Totals: 12 passed, 1 failed, 4 skipped` |
| GREEN, round 1 | `cmake --build build --target zene UndoHonestyTest ReversibilityContractTest -j8 > logs/build-2.log 2>&1`; `cd build/tests && ctest -R "UndoHonestyTest|ReversibilityContractTest" --output-on-failure > logs/test-2.log 2>&1` | build EXIT **0**; test EXIT **0** (`100% tests passed out of 2`) |
| Round 1 step 4b, RED with the fix reverse-applied | `git diff -- src/core/ControlReversibilityTableRecording.cpp src/core/ControlCommandsArrangementState.cpp src/gui/RecordLaunchMenus.cpp docs/RELEASE-NOTES-v0.3.0-alpha.md > logs/fix.patch` (EXIT 0, 130 lines); `git apply -R --check logs/fix.patch` (EXIT **0**); `git apply -R logs/fix.patch` (EXIT **0**); `cmake --build build --target zene UndoHonestyTest -j8 > logs/build-4.log 2>&1` (EXIT **0**); `cd build/tests && ctest -R UndoHonestyTest --output-on-failure > logs/test-5.log 2>&1` | apply EXIT 0; build EXIT 0; test EXIT **8**; the same `FAIL! : ... '!undo.ok' returned FALSE.`; `Totals: 12 passed, 1 failed, 4 skipped` |
| Round 1 step 4b, GREEN with the fix re-applied | `git apply logs/fix.patch` (EXIT **0**); `cmake --build build --target zene UndoHonestyTest -j8 > logs/build-5.log 2>&1` (EXIT **0**); `cd build/tests && ctest -R UndoHonestyTest --output-on-failure > logs/test-6.log 2>&1` | apply EXIT 0; build EXIT 0; test EXIT **0** (`100% tests passed out of 1`) |
| GREEN, round 2: the new single-track regression with the sibling fix in place | `cmake --build build --target ControlArmReversibilityTest -j8 > logs/build-13.log 2>&1` (EXIT **0**); `cd build/tests && ctest -R "ControlArmReversibilityTest" --output-on-failure > logs/test-15.log 2>&1` | build EXIT **0**; test EXIT **0** (`1/1 Test #25 ControlArmReversibilityTest ... Passed`) |
| Round 2 step 4b, RED with the sibling fix reverse-applied | `git diff -- src/core/ControlReversibilityTableRecording.cpp src/core/ControlCommandsArrangementState.cpp include/ControlReversibility.h > logs/fix2.patch` (EXIT 0, 145 lines); `git apply -R --check logs/fix2.patch` (EXIT **0**); `git apply -R logs/fix2.patch` (EXIT **0**); `cmake --build build --target zene ControlArmReversibilityTest UndoHonestyTest -j8 > logs/build-14.log 2>&1` (EXIT **0**); `cd build/tests && ctest -R "ControlArmReversibilityTest|UndoHonestyTest" --output-on-failure > logs/test-16.log 2>&1` | apply EXIT 0; build EXIT 0; test EXIT **8**; `FAIL! : ControlArmReversibilityTest::armingOneTrackRefusesUndoThatWouldLeaveItsTakeBehind() '!undo.ok' returned FALSE. (control.undo claimed it could remove the take from track.set_arm)`; `Totals: 2 passed, 1 failed` (the batch regression still passes: the batch fix is committed, not reversed) |
| Round 2 step 4b, GREEN with the sibling fix re-applied | `git apply logs/fix2.patch` (EXIT **0**); `cmake --build build --target zene ControlArmReversibilityTest UndoHonestyTest ReversibilityContractTest -j8 > logs/build-15.log 2>&1` (EXIT **0**); `cd build/tests && ctest -R "ControlArmReversibilityTest|UndoHonestyTest|ReversibilityContractTest" --output-on-failure > logs/test-17.log 2>&1` | apply EXIT 0; build EXIT 0; test EXIT **0** (`100% tests passed out of 3`) |
| Affected family (13 ctests, round 2 tree) | `cmake --build build --target zene ControlArmReversibilityTest UndoHonestyTest ReversibilityContractTest ReversibilityUndoTest ReversibilityUndoDeviceTest UndoHonestyStateTest ControlEditCommandsTest CommandPaletteTest CommandFormTest ControlRecordingRoutesTest RecordToolsTest ControlSessionRecordLandTest -j20 > logs/build-17.log 2>&1` (EXIT **0**); `cd build/tests && ctest -R "ControlArmReversibilityTest|UndoHonestyTest|ReversibilityContractTest|ReversibilityUndoTest|ReversibilityUndoDeviceTest|UndoHonestyStateTest|ControlEditCommandsTest|CommandPaletteTest|CommandFormTest|ControlRecordingRoutesTest|RecordToolsTest|ControlSessionRecordLandTest|ControlCommandsSnapshot" --output-on-failure > logs/test-18.log 2>&1` | EXIT **0**, `100% tests passed out of 13` (13 test binaries, 0 failed) |
| The live-binary recording transcript (the python ctest, round 2 tree) | `cd build/tests && ctest -R "ControlArmReversibilityTest|UndoHonestyTest|ReversibilityContractTest|ReversibilityUndoTest|ReversibilityUndoDeviceTest|UndoHonestyStateTest|ControlEditCommandsTest|CommandPaletteTest|CommandFormTest|ControlRecordingRoutesTest|RecordToolsTest|ControlSessionRecordLandTest|ControlCommandsSnapshot|ControlRecordInputs|ControlRecordingRecovery|ControlUndoStructuralTranscript" --output-on-failure > logs/test-21.log 2>&1` | EXIT **0**, `100% tests passed out of 16` - including `ControlRecordInputs` (the real binary over `--control-socket`: the typed refusal, the route and the take surviving it, and the `irreversible` record) |
| Offline command surface regenerated from a live instance (round 2) | `python3 /tmp/opencode/dump_snapshot.py build/zene /tmp/opencode/snapshot-new2.json /tmp/opencode/commands-live2.json > logs/snapshot-2.log 2>&1` (the script starts the real binary with `--control-socket`, asks it for `control.commands_list`, then runs `tools/mcp-zene-control/snapshot_commands.py --socket ... --out ...`) | EXIT **0**; `live capture: 833 command(s)`; generator exit 0; `surface: 833 id(s) across 76 group(s), ids_sha256 8995baf17d1f3e2c27b9575378bdf5c3bd6f6df2af22259511d9ea318017dbb5` - byte-identical to the committed snapshot's `ids_sha256`; against the pre-round-1 snapshot only `track.set_arm` and `track.set_arm_many` differ (the two descriptions) |
| Snapshot ratchet | `cd build/tests && ctest -R "ControlCommandsSnapshot" --output-on-failure > logs/test-7.log 2>&1` (round 1) and inside the 13-test family run above (round 2) | EXIT **0** both times (`ControlCommandsSnapshot ... Passed`) |
| Gates (the brief's gate, round 2 tree) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-81-port-port > logs/gate-9.log 2>&1` | EXIT **0** - `PASS` on all 8 rows (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity "no new item in your files", file-length "no new item in your files") |
| Complexity ratchet (raw, round 2 tree) | `bash tests/complexity-gate.sh --check > logs/gate-7.log 2>&1` | EXIT **1** (pre-existing, 6 `REGRESSION` entries, none in a file this change touches) |
| File-length ratchet (raw, round 2 tree) | `bash tests/file-length-gate.sh --check > logs/gate-8.log 2>&1` | EXIT **1** (pre-existing, 15 `REGRESSION` entries, none in a file this change touches) |
| Violation sets, base vs final | `diff <(grep REGRESSION /tmp/cx-base.log \| sort) <(grep REGRESSION logs/gate-7.log \| sort)`; the same for the file-length gate | identical in both gates - this change adds no entry and re-anchors no baseline |
| The new test file's length | `/tmp/codelines.sh tests/src/core/ControlArmReversibilityTest.cpp` (the file-length gate's own counting) | **63** code lines, well under the 500 limit |
| Registration | `rg "^<file>$" tests/fork-sources.txt` for the new source; `bash tests/fork-sources-gate.sh` | `PASS: every tracked source in scope is registered (1854 fork-NEW, 1212 inherited, 48 tooling)`; the new file is in `tests/fork-sources.txt`, and `tests/all-sources.txt` was re-derived from its own recipe (one line added) |

## Environmental failures (not caused by this change)

`ControlReversibilityTranscript` fails in this sandbox with `section E could not run: plugin.list offered no 'amplifier' device`: the catalogue in this build offers only the 7 LADSPA devices, so the parameter sweep has no device to drive. Proof that it is pre-existing: with the fix reverse-applied (`git apply -R logs/fix.patch`, `logs/build-8.log` EXIT 0) the same test fails with the same message (`logs/test-11.log`, EXIT 8). The transcript script never mentions `track.set_arm_many` or `track.set_arm` (`rg set_arm tests/control-reversibility-transcript.py` -> no match), and sections A-D of it (the transaction/undo sections) pass in both runs.

Before `cmake --build build --target ladspaeffect` was run, the same two transcripts failed earlier still, at `plugin.load` of the LADSPA host plugin (`the LADSPA host plugin 'ladspaeffect' is not in this build`), because that plugin target was not built in this tree. Building it (EXIT 0, `logs/build-7.log`) fixed `ControlUndoStructuralTranscript`; the section-E device choice is the one failure left.

## Gate status, stated plainly

`feature_check.py` PASSES (EXIT 0) on this tree. The two raw ratchets it wraps - `tests/complexity-gate.sh --check` and `tests/file-length-gate.sh --check` - exit **1** on this branch, and they already did on the base commit `2da2ed356`: the violation sets are byte-identical before and after this change (see the diff rows above). The entries are in files this change does not touch (for example `src/core/TakeLane.cpp` at 575 code lines, `tests/src/core/ControlEditCommandsTest.cpp` at 542, and six complexity functions such as `lmms::adjust` in `src/core/ControlCommandsMixerTools.cpp`). This change adds no entry to either gate and does not re-anchor either gate or its baseline.

The one file this change touches that the file-length ratchet measures at its limit is `tests/src/core/UndoHonestyTest.cpp`: 494 code lines before, **500** code lines after (the ratchet's limit is 500, and the gate reports it as no new violation). The ported test is the accepted one, compacted exactly as the accepted fix compacted it: the same four assertions remain (`!undo.ok`, the `Irreversible` kind, the refusal naming `track.set_arm_many`, and the tracks and takes unchanged after the refused undo), plus the `InvalidArgs` check from the original test. The round-2 single-track regression is a NEW file (63 code lines), not an addition to that one.

## Self-check

Every ASK item of `submit_check.py`, answered with the evidence above.

1. **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** RED: `git apply -R logs/fix2.patch` (EXIT 0) -> `cmake --build build --target zene ControlArmReversibilityTest UndoHonestyTest -j8` (EXIT 0) -> `ctest -R "ControlArmReversibilityTest|UndoHonestyTest"` (EXIT **8**, `FAIL! : ... '!undo.ok' returned FALSE.`). GREEN: `git apply logs/fix2.patch` (EXIT 0) -> same build (EXIT 0) -> `ctest -R "ControlArmReversibilityTest|UndoHonestyTest|ReversibilityContractTest"` (EXIT **0**). The round-1 batch regression has the same RED/GREEN pair (`logs/fix.patch`, `logs/test-5.log`, `logs/test-6.log`).
2. **The test is deterministic: no wall-clock timing, sleeps or races.** The new test drives three control commands and reads the recorder's own state; there is no timer, no sleep and no thread in it. The take file it creates is made hermetic by a test seam instead of by timing: `ConfigManager::inst()->setWorkingDir(takes.path())` points the default take path (`control::defaultRecoveryDir()` = the directory of `recoveryFile()`) at a `QTemporaryDir`, restored by a `qScopeGuard` when the case ends. The armed state is read back from the recorder (`control::trackRecordArmed`) and the take is checked with `QFile::exists` on the path the reply itself returned. The same seam is used by the batch regression in `UndoHonestyTest.cpp`.
3. **The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.** The cause is the contract row plus the handler's inverse: `src/core/ControlReversibilityTableRecording.cpp:86-101` (the `track.set_arm` row) and `src/core/ControlCommandsArrangementState.cpp:176-226` (the handler, whose `__transaction` made the undo claim to reverse the arming). The registry seam that turns the row into a typed refusal was read and is unchanged (`src/core/ControlRegistry.cpp:543-571`, `src/core/ControlCommandsControl.cpp:400-406`). Callers of `track.set_arm` in the tree: the GUI arm button (`src/gui/tracks/TrackOperationsWidget.cpp:123-139` - it invokes the command and reads the arm flag back; it never relied on an undo), the batch handler `armEach` (`src/core/ControlCommandsArrangementState.cpp:345-370`, which now gets a reply with no transaction payload), and the tests. Siblings in the same recording group: `track.set_arm_many` (the batch, `irreversible` since round 1), `record.arm_track`, `record.disarm_track` and `record.disarm_all` (already `irreversible`), `record.get_state` / `record.input_get_state` (`not_mutating`), `record.input_set` (`true_inverse`). The command descriptions, the GUI comments, the release notes, `docs/RECORD-INPUTS.md`, `docs/A16-REVERSIBILITY.md` and the offline command snapshot all follow the table.
4. **No collateral damage: old projects and saved data still load as before, and no added limit hits other callers.** Nothing about the project's serialization changes: the arming never wrote to the track's element (the row's own reason says so), and the fix only removes a recorded undo step and changes two table rows' class. No file format, no saved data and no project file is touched, so old projects load exactly as before. The change is a class in the contract table, which is per command id, not a limit: `track.set_arm` and `track.set_arm_many` are the only rows it touches, and the only rule involved (`ReversibilityContractTest`'s "an irreversible row declares `reversible=false` and a fallback") is satisfied by both rows and applies to no other caller. The refusal itself is the registry's existing typed path, already used by `record.disarm_track` and `project.save`.
5. **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.** Not applicable - the verdict is FIXED. The defect was on this branch at `src/core/ControlReversibilityTableRecording.cpp:69-78` and `src/core/ControlCommandsArrangementState.cpp:401-405` (base `2da2ed356`), and `git merge-base --is-ancestor 70a924a8a HEAD` returned exit 1, so the accepted fix was not in this tree.
6. **Sibling commands in the command files I changed.** The checker lists the ids of the groups whose names appear in the two command files the diff touched - `arrangement.*` (13), `comp.*` (14), `meter.*` (5), `record.*` (18) and `transport.*` (26), 76 ids in all - plus the two commands registered in the file I changed, `arrangement.get_state` (a read) and the two arming verbs. Every one of them is unaffected, and each for a stated reason:
   - The change is TWO rows of the contract table (`track.set_arm`, `track.set_arm_many`) and ONE handler each. No other row's class, reason, mechanism or fallback changed, and no other handler's code changed; `arrangement.get_state` (a read) keeps its own row and handler.
   - The recording group is the one that shares the recorder with the fix, and its siblings keep their own classes: `record.arm_track`, `record.disarm_track` and `record.disarm_all` were already `irreversible` before this change and are unchanged; `record.get_state` and `record.input_get_state` are `not_mutating`; `record.input_set` is `true_inverse`. The arming verbs' own disarm path (`armed: false`) still records no inverse, exactly as before.
   - The `arrangement.*`, `comp.*`, `meter.*` and `transport.*` ids share nothing with the recorder: they are arrangement, comping, metering and transport verbs, none of which the fix touches, and the fix adds no command (the id surface is unchanged: `ids_sha256 8995baf1...` before and after, 833 ids / 76 groups).
   - The proof that the whole table still agrees with itself is `ReversibilityContractTest` (EXIT 0, `logs/test-17.log` and `logs/test-18.log`): it re-measures the table, compares it with the published A16 figure, and asserts that every irreversible row declares `reversible=false` and a fallback - both rows this change adds pass all three.

## Files committed

Round 0 (the port, `50b48f545`):

- `src/core/ControlReversibilityTableRecording.cpp`
- `src/core/ControlCommandsArrangementState.cpp`
- `src/gui/RecordLaunchMenus.cpp` (comment only)
- `tests/src/core/UndoHonestyTest.cpp`
- `docs/RELEASE-NOTES-v0.3.0-alpha.md`
- `tools/mcp-zene-control/zene_control/commands_snapshot.json` (derived; regenerated from a live instance of the fixed binary)
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-81-port-port.md` (this report)

Round 1 (the report's self-check, `785574737`):

- `docs/qwen-fixes-2026-10/cluster-new-p8-81-port-port.md`

Round 2 (this rework, the sibling command):

- `src/core/ControlReversibilityTableRecording.cpp` (`track.set_arm` row)
- `src/core/ControlCommandsArrangementState.cpp` (the single arm's inverse, the description, the strip comment)
- `include/ControlReversibility.h` (comment only)
- `docs/RELEASE-NOTES-v0.3.0-alpha.md` (A16 histogram and the two prose mentions)
- `docs/RECORD-INPUTS.md` (the command table's class and the proof line)
- `docs/A16-REVERSIBILITY.md` (the dated note)
- `tools/mcp-zene-control/zene_control/commands_snapshot.json` (derived; regenerated from a live instance of the fixed binary)
- `tests/src/core/ControlArmReversibilityTest.cpp` (NEW: the single-track regression)
- `tests/CMakeLists.txt` (registers the new test)
- `tests/fork-sources.txt` (registers the new source)
- `tests/all-sources.txt` (re-derived from its own recipe)
- `tests/control-record-inputs.py` (the live-binary transcript proves the new contract)
- `tests/upstream-modifications.txt` (four lines)
- `docs/qwen-fixes-2026-10/cluster-new-p8-81-port-port.md` (this report)
