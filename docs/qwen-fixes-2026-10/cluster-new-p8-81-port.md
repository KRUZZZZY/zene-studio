# Cluster new-p8-81-port

Verdict: **FIXED** (port of the accepted fix from `bugs/qwen-new-p8-81`, written by hand onto `bugs/qwen-new-p8-81-port`; no merge, cherry-pick or rebase).

Finding: "`track.set_arm_many` is marked reversible although undo leaves its takes behind" (`src/core/ControlReversibilityTableRecording.cpp:80`, historical line numbers).

## Is the finding real on this branch?

Yes. Before this change, the branch's newest line (`4eb8d6e8a`) still had the defect:

- `git show HEAD:src/core/ControlReversibilityTableRecording.cpp` lines 69-78: `R("track.set_arm_many", RC::Snapshot, true, ...)` with the inverse "ONE recorded undo step - this command with armed: false ...".
- `git show HEAD:src/core/ControlCommandsArrangementState.cpp` lines 382-386 (`setArmMany`): on arming, the handler inserted a `__transaction` whose inverse disarms the batch's tracks, so `control.undo` succeeded and disarmed the tracks while the take WAV files stayed on disk.

The RED run below reproduces exactly that: the unfixed product's `control.undo` returns `ok` and the regression fails on it.

## Fix (what was changed, and why this option)

The finding offered two options: mark the row irreversible, or make the inverse remove and redo the takes. I took the first, as the accepted fix did. A take is a file on disk outside the project state that `control.undo` restores, so "remove and redo" would have to delete files a user may already have recorded into; that is a design call for the owner. The honest registry answer is a typed refusal.

| File | Change |
| --- | --- |
| `src/core/ControlReversibilityTableRecording.cpp:69-78` | Row is now `RC::Irreversible, false`; reason and fallback say the take files outlive the captures and the reply's route ids are the way to stop them. |
| `src/core/ControlCommandsArrangementState.cpp:363-366` | Handler comment corrected. The `__transaction` insertion on arm is removed (was lines 382-386). |
| `src/core/ControlCommandsArrangementState.cpp:392-398` | Command description no longer promises a one-step undo; it says `control.undo` refuses the command and names `record.disarm_track`. No command added or removed. |
| `src/gui/RecordLaunchMenus.cpp:65-67` | Comment only: the Arm All menu no longer claims one undo step takes the arm back. |
| `docs/RELEASE-NOTES-v0.3.0-alpha.md:999,1002` | A16 histogram moved to the measured counts: `snapshot=42 irreversible=22` becomes `snapshot=41 irreversible=23` (one row changed class). `ReversibilityContractTest` checks this against the registry. |
| `tests/src/core/UndoHonestyTest.cpp:405-447` | Regression test `armAllRefusesUndoThatWouldLeaveItsTakesBehind` replaces `armingSeveralTracksIsOneStep` (the old test asserted the undo that is now refused). It runs in a private working directory (see "Environment notes"). The header comment is updated to match. |

No new files were created, so no `tests/fork-sources.txt` or `tests/upstream-modifications.txt` entries were needed: every changed file is already registered in `tests/fork-sources.txt` (or, for the release notes, already listed in `tests/upstream-modifications.txt`).

The offline snapshot `tools/mcp-zene-control/zene_control/commands_snapshot.json` was also regenerated (see "Round 1 rework" below): it is a DERIVED file, so it is captured from a live instance with the bridge's own generator rather than hand-edited.

## Round 1 rework (QA verdict REWORK)

The independent QA review of the round-0 commit returned `REWORK` with one instruction: the committed offline command snapshot still told agents that `track.set_arm_many` is one undo step, so the command surface stayed misleading when no instance was running (`tools/mcp-zene-control/zene_control/commands_snapshot.json:29575`). It asked me to regenerate the snapshot from the fixed registry, commit it, and rerun `ctest -R ControlCommandsSnapshot`.

Done. The snapshot was re-captured from a live instance of the fixed binary using the bridge's own generator (`snapshot_commands.py --from-file`), so it is derived, never hand-written:

- `cmake --build build --target zene -j12` -> EXIT 0 (`logs/build-30.log`).
- Live capture: the harness started `build/zene`, asked `control.commands_list`, wrote the reply to `/tmp/opencode/commands-live.json` -> EXIT 0, 828 commands (`logs/dump-1.log`).
- `python3 tools/mcp-zene-control/snapshot_commands.py --from-file /tmp/opencode/commands-live.json --out <snapshot>` -> EXIT 0, `surface: 828 id(s) across 76 group(s), ids_sha256 8e2d5b9a0774e7005e5832777c552b2d4f09efbc56e71322c5fd72e5f3385c6a` (`logs/snapshot-1.log`). That `ids_sha256` is byte-identical to the pre-rework snapshot, so the id surface is unchanged.
- Snapshot diff is exactly three lines: `captured_at`, the recorded `instance.source_file`, and the one `track.set_arm_many` `description` string. The new description reads "...Arming writes take files that remain on disk when captures stop, so control.undo refuses this command; stop each route with record.disarm_track using its route id from 'routes'...". No other command entry changed; the id set is equal and `surface.ids_sha256` is unchanged.
- `ctest -R ControlCommandsSnapshot --output-on-failure` from `build/tests` -> EXIT 0 (`1/1 Test #574 ControlCommandsSnapshot ... Passed`, `logs/test-35.log`).

The regression for the bug itself was re-proved on this tree in the same round (reverse-patch, RED then GREEN): see the first three rows of the evidence table below.

## Evidence table

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-81-port.r1/logs` (named below by file).

| Evidence | Exact command | EXIT / count |
| --- | --- | --- |
| RED, unfixed product, regression before the compaction below | `cmake --build build --target zene UndoHonestyTest -j2 > logs/build-3.log 2>&1` (EXIT 0), then `cd build/tests && ctest -R UndoHonestyTest --output-on-failure > logs/test-4.log 2>&1` (product unfixed, test with the working-directory isolation but before the line compaction) | EXIT **8**; `FAIL! : UndoHonestyTest::armAllRefusesUndoThatWouldLeaveItsTakesBehind() '!undo.ok' returned FALSE. (control.undo claimed it could remove the takes from track.set_arm_many)` at `UndoHonestyTest.cpp(458)`; `Totals: 12 passed, 1 failed, 4 skipped` |
| RED, fix reverse-applied (the proof QA asked for, on the final test) | `git diff -- src/core/ControlReversibilityTableRecording.cpp src/core/ControlCommandsArrangementState.cpp > logs/fix-v2.patch`; `git apply -R --check logs/fix-v2.patch` (EXIT 0); `git apply -R logs/fix-v2.patch` (EXIT 0); `cmake --build build --target zene UndoHonestyTest -j2 > logs/build-8.log 2>&1` (EXIT 0); `cd build/tests && ctest -R UndoHonestyTest -V > logs/test-13.log 2>&1` | apply EXIT 0; build EXIT 0; test EXIT **8**; `FAIL! : ...armAllRefusesUndoThatWouldLeaveItsTakesBehind() '!undo.ok' returned FALSE.` at `UndoHonestyTest.cpp(435)`; `Totals: 12 passed, 1 failed, 4 skipped` |
| GREEN, fix re-applied | `git apply logs/fix-v2.patch` (EXIT 0); `cmake --build build --target zene UndoHonestyTest -j2 > logs/build-9.log 2>&1`; `cd build/tests && ctest -R UndoHonestyTest -V > logs/test-14.log 2>&1` | apply EXIT 0; build EXIT 0; test EXIT **0** |
| GREEN, final tree after the last edit | `cmake --build build --target zene UndoHonestyTest ReversibilityContractTest ControlEditCommandsTest -j2 > logs/build-12.log 2>&1`; `ctest -R UndoHonestyTest -V > logs/test-17.log`; `ctest -R ReversibilityContractTest -V > logs/test-18.log`; `HOME=/tmp/zene-home-p8 ctest -R ControlEditCommandsTest -V > logs/test-19.log` | build EXIT 0; UndoHonestyTest EXIT 0 (`13 passed, 0 failed, 4 skipped`); ReversibilityContractTest EXIT 0 (`9 passed, 0 failed`); ControlEditCommandsTest EXIT 0 (`17 passed, 0 failed`) |
| Affected family, all seven entries | `cd build/tests && ctest -R "ControlEditCommandsTest|ReversibilityContractTest|ReversibilityUndoTest|ReversibilityUndoDeviceTest|UndoHonestyTest|UndoHonestyStateTest|ControlReversibilityTranscript" -V > logs/test-15.log 2>&1` | EXIT 8 (2 of 7 failed, both environmental, see below). ReversibilityContractTest 9 passed; ReversibilityUndoTest 9 passed / 4 skipped; ReversibilityUndoDeviceTest 4 passed / 2 skipped; UndoHonestyTest 13 passed / 4 skipped; UndoHonestyStateTest 6 passed / 2 skipped |
| Family failure 1 (environmental) | same run: ControlEditCommandsTest | EXIT 8 without the temp home: `the take file /home/kruzzzzy/Documents/Zene Studio/zene-take-trk40.wav could not be opened for writing`. Passes under `HOME=/tmp/zene-home-p8` (see Environment notes): 17/17, EXIT 0 |
| Family failure 2 (environmental) | same run: ControlReversibilityTranscript | EXIT 8: `plugin.load` of `ladspaeffect` returns `the LADSPA host plugin 'ladspaeffect' is not in this build` (`tests/control_socket_harness.py` `ok_result`). It fails before any arm or undo step, and `tests/control-reversibility-transcript.py` does not reference `track.set_arm_many`. Not run on the base tree |
| Complexity gate | `bash tests/complexity-gate.sh --check > logs/gate-5.log 2>&1; echo EXIT=$?` | **EXIT 1** (pre-existing; see below) |
| File-length gate | `bash tests/file-length-gate.sh --check > logs/gate-6.log 2>&1; echo EXIT=$?` | **EXIT 1** (pre-existing; see below) |
| Gates on the base commit (untouched export) | `git archive HEAD \| tar -x -C /tmp/zene-base-p8`; in that copy: `bash tests/complexity-gate.sh --check > logs/gate-base-1.log 2>&1` and `bash tests/file-length-gate.sh --check > logs/gate-base-2.log 2>&1` | EXIT 1 and EXIT 1 |
| Violation sets, base vs final | `grep -h "REGRESSION\|FAIL" logs/gate-5.log \| sort \| diff logs/gate-base-1.set -`; same for `gate-6.log` vs `gate-base-2.set` | identical in both gates (no new entry from this change) |
| Registration | `grep` of each changed file in `tests/fork-sources.txt` and `tests/upstream-modifications.txt` | all changed files registered; no new files |

### Round 1 evidence (this commit, on top of the round-0 fix)

The fix hunks are already committed in round 0, so the reverse-patch proof is taken against the pre-fix base `4eb8d6e8a`: `git diff 4eb8d6e8a -- <the three product files> > logs/fix.patch` (88 lines), `git apply -R --check logs/fix.patch` -> EXIT 0, `git apply -R logs/fix.patch` -> EXIT 0.

| Evidence | Exact command | EXIT / count |
| --- | --- | --- |
| RED, fix reverse-applied | `cmake --build build --target zene UndoHonestyTest -j12 > logs/build-33.log 2>&1` -> EXIT 0; `cd build/tests && ctest -R UndoHonestyTest --output-on-failure > logs/test-33.log 2>&1` | test EXIT **8**; `FAIL! : UndoHonestyTest::armAllRefusesUndoThatWouldLeaveItsTakesBehind() '!undo.ok' returned FALSE. (control.undo claimed it could remove the takes from track.set_arm_many)`; `Totals: 12 passed, 1 failed, 4 skipped` |
| GREEN, fix re-applied | `git apply logs/fix.patch` -> EXIT 0; `cmake --build build --target zene UndoHonestyTest -j12 > logs/build-34.log 2>&1` -> EXIT 0; `cd build/tests && ctest -R UndoHonestyTest --output-on-failure > logs/test-34.log 2>&1` | test EXIT **0** (`100% tests passed out of 1`) |
| Snapshot regenerated from a live instance (the QA rework) | `cmake --build build --target zene -j12 > logs/build-30.log 2>&1` -> EXIT 0; live capture -> `logs/dump-1.log` EXIT 0 (828 commands); `python3 tools/mcp-zene-control/snapshot_commands.py --from-file /tmp/opencode/commands-live.json --out tools/mcp-zene-control/zene_control/commands_snapshot.json > logs/snapshot-1.log 2>&1` -> EXIT 0 | snapshot diff = 3 lines (`captured_at`, `instance.source_file`, the one `track.set_arm_many` description); `ids_sha256` unchanged (`8e2d5b9a...`) |
| Snapshot ratchet (the QA rework's required rerun) | `cd build/tests && ctest -R ControlCommandsSnapshot --output-on-failure > logs/test-35.log 2>&1` | EXIT **0** (`1/1 Test #574 ControlCommandsSnapshot ... Passed`) |
| Affected family, all seven entries | `cmake --build build --target UndoHonestyTest ReversibilityContractTest ControlEditCommandsTest -j12 > logs/build-31.log 2>&1` (EXIT 0); `cd build/tests && ctest -R "ControlEditCommandsTest\|ReversibilityContractTest\|ReversibilityUndoTest\|ReversibilityUndoDeviceTest\|UndoHonestyTest\|UndoHonestyStateTest\|ControlReversibilityTranscript" --output-on-failure > logs/test-31.log 2>&1` | EXIT **8** (6 of 7 passed; only the environmental `ControlReversibilityTranscript` failed). `ControlEditCommandsTest` now passes without any `HOME` override (17/17); `ReversibilityContractTest`, `ReversibilityUndoTest`, `ReversibilityUndoDeviceTest`, `UndoHonestyTest`, `UndoHonestyStateTest` all passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > logs/gate-30.log 2>&1; echo EXIT=$?` | **EXIT 1** (pre-existing; 6 `REGRESSION` entries, none in a file this change touches) |
| File-length gate | `bash tests/file-length-gate.sh --check > logs/gate-31.log 2>&1; echo EXIT=$?` | **EXIT 1** (pre-existing; 14 `REGRESSION` entries, none in a file this change touches) |

## Gate status, stated plainly

Both required ratchet gates exit **1** on this branch, and they already did on the base commit: the base export produces the same violation sets. The entries are in files this change does not touch (for example `tests/src/core/ControlEditCommandsTest.cpp` at 511 code lines, `src/core/TakeLane.cpp` at 520, six complexity functions such as `lmms::adjust` in `src/core/ControlCommandsMixerTools.cpp`). This change adds no entry to either gate and does not re-anchor either gate or its baseline. I did not fix those files, because doing so is outside this bug.

One violation was mine and is fixed: the first version of the regression test took `tests/src/core/UndoHonestyTest.cpp` from 494 to 525 code lines, over the 500 limit. It is now 500 (file-length gate reports it no longer). The test was compacted, not weakened: the same four assertions remain (`!undo.ok`, `Irreversible`, the refusal names `track.set_arm_many`, and the tracks and takes are unchanged after the refused undo), plus the `InvalidArgs` check from the original test.

## Environment notes (affect how the commands above were run)

- `build` failed the first time with `ccache: error: failed to create temporary file for /run/user/1000/ccache-tmp/...: Permission denied`. Every build after that used `CCACHE_TEMPDIR=/tmp/ccache-tmp-p8`. No code was involved.
- The first RED run failed before reaching the assertion: `armed.ok` was false because the default take files go beside `recover.mmp` in the owner's `~/Documents/Zene Studio`, which this sandbox could not write (`could not be opened for writing`). The original test had the same dependency. The regression test now uses a `QTemporaryDir` as its working directory (restored by a `qScopeGuard`), as `UndoHonestyStateTest` already does. This is hermetic and changes no assertion.
- `ControlEditCommandsTest` (not edited by this change) has the same default-path dependency and fails in this sandbox for the same reason. It passes with `HOME=/tmp/zene-home-p8` (`/tmp/zene-home-p8/Documents/Zene Studio` created first), which moves the default take folder into `/tmp`.

## Other observation (not changed here: NEEDS-CONTEXT for the owner)

`track.set_arm` (the single-track row, `src/core/ControlReversibilityTableRecording.cpp:55-67`) is still `RC::Snapshot, true`. Its undo also leaves the take file on disk: the row's own fallback says "the take: delete it". The accepted fix treated the batch as irreversible and left the single-track arm reversible. That is inconsistent with the batch decision, and the finding does not cover `track.set_arm`. Deciding whether the single-track row needs the same treatment is an owner call, so I left it alone.

## Files committed

Round 0 (`70a924a8a`):

- `src/core/ControlReversibilityTableRecording.cpp`
- `src/core/ControlCommandsArrangementState.cpp`
- `src/gui/RecordLaunchMenus.cpp` (comment only)
- `tests/src/core/UndoHonestyTest.cpp`
- `docs/RELEASE-NOTES-v0.3.0-alpha.md` (A16 histogram figures only)

Round 1 (this commit, the QA rework):

- `tools/mcp-zene-control/zene_control/commands_snapshot.json` (derived; regenerated from a live instance of the fixed binary)
- `docs/qwen-fixes-2026-10/cluster-new-p8-81-port.md` (this report)
