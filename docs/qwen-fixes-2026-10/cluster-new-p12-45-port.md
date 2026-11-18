FIXED

Cluster: new-p12-45-port
Finding: Out-of-grid recording pairs pass preflight and are still landed
File: src/core/ControlCommandsSessionRecordLand.cpp

## What this round changed (and what it did not)

The product fix of rounds 2-4 is unchanged and still on the branch. Luna's r4
review had exactly one point - the AUTOMATED PROOF CHECK could not see the
regression - so this round adds the missing proof and records why the check
could not see it before.

**Why the r4 proof check failed (mechanical, not a test-quality problem).**
The check's test discovery is `known = re.findall(r"Test #\d+: (\S+)", ctest -N)`
(qwen/negctl.py:52). ctest right-aligns the test number, so a one- or two-digit
number is printed with extra spaces (`  Test  #24: ControlSessionRecordLandTest`)
and the pattern (one space) does not match it; only three-digit numbers
(`  Test #410: SessionGridRecordTest`) are found. The regression of rounds 2-4
lives in `ControlSessionRecordLandTest` = ctest **#24**, so the check could not
select it, ran the five three-digit family tests instead
(SessionArrangementRecordTest, SessionCommandArgsTest, SessionGridRecordTest,
SessionModelTest, SessionTransportBarTest) and reported "they do not prove the
fix" (r4/negctl/test-reverted.log). The tests themselves were sound: the same
revert made `ControlSessionRecordLandTest` fail (r4/logs/test-8-red.log).

**What this round adds.** The same behaviour is now also proven end to end in
`SessionGridRecordTest` = ctest **#410** - a target the check does see - which
already drives `session.set_grid` and `session.arrangement_record_land` through
the real engine and the command surface:

- `tests/src/gui/SessionGridRecordTest.cpp:179`
  `aTakeWhoseSceneWasRemovedIsNotLandedAsABlankClip` - record a take into grid
  cell (0,1), shrink the grid to one scene, land: no clip on the song track,
  reason `session_grid_cell_removed`, `removed_grid_cells` 1,
  `skipped_columns` 0, the ring consumed.
- `tests/src/gui/SessionGridRecordTest.cpp:205`
  `aTakeWhoseGridTrackWasRemovedIsNotLandedAsABlankClip` - the same when the
  grid loses the cell's TRACK while the song track still exists.

Both fail on the reverted product with `clips` Actual 1 vs Expected 0 - the
blank clip the finding is about - and pass with it.

**The check's own verdict, run on this branch.** Running the check's own code
(`negctl.prove(worktree, merge-base(HEAD, 050/m0-train-8), run_dir, report)`)
on the committed branch gives:

```
ok: True
detail: SessionArrangementRecordTest, SessionCommandArgsTest, SessionGridRecordTest,
        SessionModelTest, SessionTransportBarTest: they fail without it (rc 8) and pass with it
reverted_build_rc: 0   reverted_rc: 8   restored_rc: 0   tree_clean: True
```

(logs in .../new-p12-45-port.r5/negctl/: product.patch, build-reverted.log,
test-reverted.log, build-restored.log, test-restored.log). The reverted run
fails on exactly the two new cases
(`SessionGridRecordTest::aTakeWhoseSceneWasRemovedIsNotLandedAsABlankClip` at
tests/src/gui/SessionGridRecordTest.cpp:194 and
`...GridTrackWasRemoved...` at :223, "clips" Actual 1 vs Expected 0) and the
other 4 selected tests plus the other 6 cases of SessionGridRecordTest pass
("Totals: 6 passed, 2 failed"); the restored run is "100% tests passed out of
5".

## Evidence (the fix itself, unchanged since round 2)

- `src/core/ControlCommandsSessionRecordLand.cpp:300-303` - the song-track
  bounds guard, reason `column_has_no_song_track` (the pre-existing contract
  for a column that never had a song track).
- `src/core/ControlCommandsSessionRecordLand.cpp:304-309` - the session-grid
  bounds guard, checked BEFORE `model.slot(pair.track, pair.scene)` is read at
  :317, reason `session_grid_cell_removed` (a removed scene or removed grid
  track whose song track still exists).
- `src/core/ControlCommandsSessionRecordLand.cpp:118-122` - the preflight
  sibling `pairHasSlot` gates the content validation on the same song+grid
  bounds, so an out-of-grid pair is skipped by the preflight and reported (not
  landed) by the landing pass.
- `src/core/ControlCommandsSessionRecordLand.cpp:285-292` - `countNotLandedPair`
  sorts every not-landed pair into the summary counts; the new
  `removed_grid_cells` field is declared in the command's resultSchema at :355
  and reported at :414.
- `tests/src/core/ControlSessionRecordLandTest.cpp:346-383` - the round-2/3
  unit-level regression (a removed scene, a removed grid track), untouched.

## Commands (verbatim, with EXIT codes)

All logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-45-port.r5/logs`.

- Build the new test (fix in place):
  `cmake --build build --target zene SessionGridRecordTest -j$(nproc)` -> EXIT=0
  (logs/build-1-green.log).
- GREEN: `ctest -R "^SessionGridRecordTest$" --output-on-failure` from
  build/tests -> EXIT=0 (logs/test-1-green.log); the binary itself:
  `Totals: 8 passed, 0 failed` including both new cases
  (logs/test-2-green-binary.log).
- Proof that the test catches the bug - the product diff the automated check
  reverts (merge-base(HEAD, 050/m0-train-8) = ded355440 .. HEAD, product files
  only: src/core/ControlCommandsSessionRecordLand.cpp):
  `git diff ded355440..HEAD -- src/core/ControlCommandsSessionRecordLand.cpp > logs/fix.patch` -> EXIT=0;
  `git apply -R logs/fix.patch` -> EXIT=0;
  `cmake --build build --target zene SessionGridRecordTest ControlSessionRecordLandTest -j$(nproc)` -> EXIT=0
  (logs/build-2-red-revert.log);
  `ctest -R "^(SessionGridRecordTest|ControlSessionRecordLandTest)$" --output-on-failure` -> EXIT=8
  (logs/test-3-red-revert.log): 4 failed -
  `SessionGridRecordTest::aTakeWhoseSceneWasRemovedIsNotLandedAsABlankClip` and
  `...GridTrack...` ("clips" Actual 1 vs Expected 0, at
  tests/src/gui/SessionGridRecordTest.cpp:194 and :223) and the two pre-existing
  `ControlSessionRecordLandTest` cases (Actual 1 vs Expected 0). The other 6
  cases of SessionGridRecordTest passed unchanged, so the failure is the bug,
  not collateral.
  `git apply logs/fix.patch` -> EXIT=0;
  `cmake --build build --target zene SessionGridRecordTest ControlSessionRecordLandTest -j$(nproc)` -> EXIT=0
  (logs/build-3-green-restore.log);
  `ctest -R "^(SessionGridRecordTest|ControlSessionRecordLandTest)$" --output-on-failure` -> EXIT=0,
  100% tests passed out of 2 (logs/test-4-green-restore.log).
- Affected family:
  `ctest -R "^(ControlSessionRecordLandTest|SessionGridRecordTest|SessionArrangementRecordTest|SessionTransportBarTest|SessionCommandArgsTest|SessionModelTest)$" --output-on-failure`
  from build/tests -> EXIT=0, 100% tests passed out of 6 (logs/test-5-family.log).
- Gates after the final edit:
  `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py .` -> EXIT=0,
  RESULT: PASS, "no new item in your files" for complexity and file-length
  (logs/gate-1-feature-check.log);
  `bash tests/complexity-gate.sh --check` -> EXIT=1 with regressions only in
  pre-existing train files (XyPadPairTest, DawProjectReadTracks,
  ControlCommandsMixerTools, ControlCommandsMixerStrip, ProjectRevisions,
  ControlCommandsShortcuts); my file is reported as IMPROVED
  (`registerRecordLand` is no longer over CCN 10) (logs/gate-2-complexity.log);
  `bash tests/file-length-gate.sh --check` -> EXIT=1 with regressions only in
  other train files; mine are 463 / 246 / 421 lines (logs/gate-3-filelength.log).

## Evidence table

| Item | Command | EXIT / result |
|---|---|---|
| RED (product reverted) | `git apply -R logs/fix.patch` + rebuild + `ctest -R "^(SessionGridRecordTest\|ControlSessionRecordLandTest)$"` (logs/test-3-red-revert.log) | 8 - 4 failed (2 new cases: `clips` 1 vs 0) |
| The check's own negative control | `negctl.prove(wt, merge-base(HEAD, 050/m0-train-8), run_dir, report)` | ok=True - fails without it (rc 8), passes with it (rc 0), tree clean (runs/new-p12-45-port.r5/negctl/) |
| GREEN (fix restored) | `git apply logs/fix.patch` + rebuild + same ctest (logs/test-4-green-restore.log) | 0 - 2/2 passed |
| GREEN (fix in place, new test) | `ctest -R "^SessionGridRecordTest$"` (logs/test-1-green.log) | 0 - 1/1 passed, binary 8 passed / 0 failed |
| Affected family | `ctest -R "^(6 tests)$"` (logs/test-5-family.log) | 0 - 6/6 passed |
| Gate: feature_check | `python3 .../feature_check.py .` | 0 - RESULT: PASS |
| Gate: complexity ratchet | `bash tests/complexity-gate.sh --check` | 1 - regressions only in other train files; mine improved |
| Gate: file-length ratchet | `bash tests/file-length-gate.sh --check` | 1 - regressions only in other train files; mine 463/246/421 lines |

## Files committed

- tests/src/gui/SessionGridRecordTest.cpp (the regression the proof check can
  select, ctest #410)
- docs/qwen-fixes-2026-10/cluster-new-p12-45-port.md

(Already on the branch from rounds 2-4: src/core/ControlCommandsSessionRecordLand.cpp,
tests/src/core/ControlSessionRecordLandTest.cpp.)

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
  Yes - and the proof uses exactly the range the automated check reverts
  (merge-base with 050/m0-train-8 = ded355440..HEAD, product files only):
  `git apply -R logs/fix.patch` EXIT=0, rebuild EXIT=0,
  `ctest -R "^(SessionGridRecordTest|ControlSessionRecordLandTest)$"` EXIT=8 -
  `SessionGridRecordTest::aTakeWhoseSceneWasRemovedIsNotLandedAsABlankClip`
  ("clips" Actual 1 vs Expected 0, tests/src/gui/SessionGridRecordTest.cpp:194)
  and `...GridTrack...` (:223) fail, plus the two pre-existing
  `ControlSessionRecordLandTest` cases. Restored (`git apply` EXIT=0, rebuild
  EXIT=0): EXIT=0, 2/2 passed (logs/test-4-green-restore.log). With the fix in
  place from the start: `ctest -R "^SessionGridRecordTest$"` EXIT=0, binary
  "Totals: 8 passed, 0 failed" (logs/test-1-green.log, test-2-green-binary.log).
  Why the r4 check missed it: its discovery regex `Test #\d+: (\S+)`
  (qwen/negctl.py:52) only matches three-digit ctest numbers, and the round-2/3
  regression lived in ctest #24 (`  Test  #24:` - two spaces, not matched);
  the new cases live in ctest #410 (`  Test #410:` - matched), which the check
  does select and on which it now fails.
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or
  injected values it uses).** The seam is the recorder ring and the session grid:
  the take is injected synchronously with
  `recorder.recordStart(track, scene, tick)` / `recordStop(track, scene, tick)` at
  explicit ticks (0 and 192), the grid is shrunk with the `session.set_grid`
  command (tracks 1/scenes 2 -> tracks 1/scenes 1, and tracks 2/scenes 2 ->
  tracks 1/scenes 2), and the land runs through `ControlRegistry::invoke`. The
  ring is emptied first with `recorder.pop` so the case does not depend on
  another case's leftovers. No engine playback, no thread, no sleep and no clock
  read is involved - the audio device is not even driven in these two cases.
- **The fix is at the cause and complete: every caller and sibling of what you changed was
  checked and stays consistent.** `landPairState` has exactly one caller, the
  `session.arrangement_record_land` handler in the same translation unit
  (src/core/ControlCommandsSessionRecordLand.cpp:399); its preflight sibling
  `pairHasSlot` (:118-122) gates the content validation on the same song+grid
  bounds, and the two guards in `landPairState` (:300-309) use the same bounds,
  so preflight and the landing pass can never disagree about a cell's existence.
  The summary counting goes through `countNotLandedPair` (:285-292) and the new
  `removed_grid_cells` field is declared in the resultSchema (:355), so the
  schema-checked reply stays valid. The sibling `session.launch`'s own
  `skipped_columns` (src/core/ControlCommandsSessionLaunch.cpp:235) counts grid
  columns without launch content - a different command with a different meaning,
  untouched. This round changed no product file, so no other caller can be
  affected.
- **No collateral damage: old projects and saved data still load as before, and any limit or
  check you added does not hit other callers that legitimately exceed it.** This
  round adds test code only. The branch's product change touches no file
  format, loader, model or save path: it splits one guard's reporting inside a
  live command reply. In-grid pairs land exactly as before (the 12
  pre-existing `ControlSessionRecordLandTest` cases and the 6 pre-existing
  `SessionGridRecordTest` cases pass unchanged), and the genuine
  `column_has_no_song_track` case still reports `skipped_columns` 1. No new
  limit is applied to any caller.
- **If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract
  that shows it.** N/A - the verdict is FIXED. The original bug was real
  (`SessionModel::slot` returns the shared empty slot for out-of-grid
  coordinates, so the landing pass could create a blank clip from it) and the
  fix is at src/core/ControlCommandsSessionRecordLand.cpp:300-309, before the
  slot is read at :317.
- **Realtime / undo / commands.** Nothing in the branch diff runs on the audio
  thread: the handler, `landPairState` and `countNotLandedPair` are
  control-socket command code; the audio-thread producer side of the ring is
  untouched. No new control command was added (the command ids are unchanged,
  so the ControlCommandsSnapshot canary does not move). Undo stays one honest
  step: one Track journal checkpoint per touched track, and an out-of-grid pair
  never touches a track at all.
- **Sibling commands in the command files you changed.** The check prints no
  sibling ask (the `session` group has 40 ids, over its 30-id cut), so it was
  done by hand: the only command file the branch touches is
  src/core/ControlCommandsSessionRecordLand.cpp, and the only command in it is
  `session.arrangement_record_land`. Its siblings in the same area -
  `session.arrangement_record_arm`, `session.arrangement_record_status`,
  `session.back_to_arrangement` (src/core/ControlCommandsSessionRecord.cpp) and
  `session.launch` - neither read nor write the session-grid cell of a recorded
  pair, so they are unaffected by the guard and stay consistent.
