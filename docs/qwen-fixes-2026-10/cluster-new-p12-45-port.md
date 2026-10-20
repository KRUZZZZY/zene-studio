FIXED

Cluster: new-p12-45-port
Finding: Out-of-grid recording pairs pass preflight and are still landed
File: src/core/ControlCommandsSessionRecordLand.cpp

## Evidence

- Original bug (rounds 1-2): `landPairState` checked song-track bounds but not
  session-grid bounds before `model.slot(pair.track, pair.scene)`; an out-of-range
  cell resolves to the model's shared empty slot, so a blank arrangement clip was
  created. Round 2's guard (src/core/ControlCommandsSessionRecordLand.cpp:296-303)
  stops that before the slot is read - Luna's r2 review verified it fails without
  the production change.
- Luna r2 REWORK (never addressed: round 3 was rate-limited out, rc=1, no commits):
  the guard reported EVERY out-of-grid pair as `column_has_no_song_track` and
  counted it in `skipped_columns`, even when the song track still exists and only
  the session scene or grid track was removed by `session.set_grid`
  (qa/reviews/new-p12-45-port.r2.md:4).
- This round distinguishes the two facts at the same guard
  (src/core/ControlCommandsSessionRecordLand.cpp:296-311): song-track bounds first
  -> `column_has_no_song_track` (reason and `skipped_columns` count preserved for
  genuinely missing song tracks); session-grid bounds with the song track present
  -> truthful `session_grid_cell_removed`, counted in a new summary field
  `removed_grid_cells` (declared in the command's resultSchema at :343, which the
  test suite holds every reply to via ZENE_CONTROL_CHECK_RESULTS). The counting
  loop routes the new reason through `countNotLandedPair` (:283-291); the handler
  itself stays at CCN 9 (the extra branch alone would have pushed
  `registerRecordLand` from CCN 10 to 11 past the ratchet - the stale baseline
  entry of 11 would have masked it, so the helper keeps it honest).
- Regression (tests/src/core/ControlSessionRecordLandTest.cpp:346-385): one test
  for a removed SCENE and one for a removed session-grid TRACK, each while the
  song track still exists - both assert no clip is created, reason
  `session_grid_cell_removed`, `removed_grid_cells` 1, `skipped_columns` 0, ring
  consumed. The pre-existing `aColumnWithoutASongTrackAndAnEmptySpanAreReportedNotLanded`
  still asserts `column_has_no_song_track` + `skipped_columns` 1 for the genuine
  case, unchanged and passing.

## Commands (verbatim, with EXIT codes)

- RED (test written first, product code untouched):
  `cmake --build build --target zene ControlSessionRecordLandTest -j$(nproc)` -> EXIT=0 (logs/build-1-red.log);
  `ctest -R ControlSessionRecordLandTest --output-on-failure` from build/tests -> EXIT=8 (logs/test-1-red.log):
  12 passed, 2 failed - `skipped_columns` Actual 1 vs Expected 0 in both new tests.
- GREEN: `cmake --build build --target zene ControlSessionRecordLandTest -j$(nproc)` -> EXIT=0;
  `ctest -R ControlSessionRecordLandTest --output-on-failure` -> EXIT=0 (logs/test-2-green.log);
  test binary: `Totals: 14 passed, 0 failed` (logs/test-3-green-binary.log).
- Family (after the final edit): `ctest -R "ControlSessionRecordLandTest|SessionArrangementRecordTest|SessionGridRecordTest|SessionTransportBarTest|SessionCommandArgsTest|SessionModelTest" --output-on-failure`
  from build/tests -> EXIT=0, 100% tests passed out of 6 (logs/test-10-final-family.log).
- Proof the test catches the bug (only the product change reverted):
  `git diff -- src/core/ControlCommandsSessionRecordLand.cpp > .../logs/fix.patch` -> EXIT=0;
  `git apply -R .../logs/fix.patch` -> EXIT=0; rebuild -> EXIT=0;
  `ctest -R ControlSessionRecordLandTest --output-on-failure` -> EXIT=8 (logs/test-8-red.log):
  12 passed, 2 failed (same two assertions).
  `git apply .../logs/fix.patch` -> EXIT=0; rebuild -> EXIT=0;
  `ctest -R ControlSessionRecordLandTest --output-on-failure` -> EXIT=0 (logs/test-9-green.log).
- Gates after the final edit:
  `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` -> EXIT=0, RESULT: PASS (logs/gate-4-feature-check-final.log);
  `bash tests/complexity-gate.sh --check` -> EXIT=1 with regressions only in pre-existing train files (XyPadPairTest, DawProjectReadTracks, ControlCommandsMixerTools, ControlCommandsMixerStrip, ProjectRevisions, ControlCommandsShortcuts); none in mine - it reports `registerRecordLand` at CCN 9 as "improved" (logs/gate-5-complexity-final.log);
  `bash tests/file-length-gate.sh --check` -> EXIT=1 with regressions only in other train files; mine are 463 and 421 lines (logs/gate-6-filelength-final.log).

## Evidence table

| Item | Command | EXIT / result |
|---|---|---|
| RED (test first) | ctest -R ControlSessionRecordLandTest (logs/test-1-red.log) | 8 - 12 passed / 2 failed |
| GREEN | ctest -R ControlSessionRecordLandTest (logs/test-2-green.log) | 0 - 14 passed / 0 failed |
| RED proof, product-only revert | git apply -R fix.patch + rebuild + ctest (logs/test-8-red.log) | 8 - 12 passed / 2 failed |
| GREEN proof, restored | git apply fix.patch + rebuild + ctest (logs/test-9-green.log) | 0 - 1/1 test passed |
| Affected family | ctest -R "...6 tests..." (logs/test-10-final-family.log) | 0 - 6/6 passed |
| Gate: feature_check | python3 .../feature_check.py <worktree> | 0 - RESULT: PASS |
| Gate: complexity ratchet | bash tests/complexity-gate.sh --check | 1 - regressions only in other train files; mine clean (registerRecordLand CCN 9) |
| Gate: file-length ratchet | bash tests/file-length-gate.sh --check | 1 - regressions only in other train files; mine 463/421 lines |

## Files committed

- src/core/ControlCommandsSessionRecordLand.cpp
- tests/src/core/ControlSessionRecordLandTest.cpp
- docs/qwen-fixes-2026-10/cluster-new-p12-45-port.md

## Self-check

- **Regression FAILS with the product change reverted and PASSES with it (exit codes).**
  Yes: with only `git diff -- src/core/ControlCommandsSessionRecordLand.cpp` reverted
  (logs/fix.patch, `git apply -R` EXIT=0) and rebuilt, `ctest -R ControlSessionRecordLandTest`
  EXIT=8 with `Totals: 12 passed, 2 failed` - both new tests see `skipped_columns` Actual 1
  vs Expected 0 (logs/test-8-red.log). Restored (`git apply` EXIT=0, rebuild EXIT=0):
  EXIT=0, 14 passed (logs/test-9-green.log). An earlier RED with the same test against the
  untouched tree is logs/test-1-red.log (EXIT=8).
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or
  injected values it uses).** The seam is the recorder ring and the session grid:
  events are injected synchronously with `recorder().recordStart/recordStop(track, scene, tick)`
  at explicit ticks (0, 96), the grid is shrunk with the `session.set_grid` command
  (tracks 3->3/scenes 2->1 for the scene case; 3->1 for the grid-track case), and the
  command runs through `ControlRegistry::invoke`. Ring emptiness is read via
  `recorder().snapshot`; no engine playback, threads, sleeps or clock reads are involved.
- **The fix is at the cause and complete: every caller and sibling of what you changed was
  checked and stays consistent.** `landPairState` has exactly one caller - the
  `session.arrangement_record_land` handler in the same TU (src/core/ControlCommandsSessionRecordLand.cpp:390);
  its preflight sibling `pairHasSlot` (:118-122) already gates content validation on the same
  song+grid bounds and is unchanged, so out-of-grid pairs are still skipped by preflight and
  reported (not landed) by the landing pass. The summary counting goes through the new
  `countNotLandedPair` (:283-291) and the new `removed_grid_cells` field is declared in the
  command's resultSchema (:343), so schema-checked replies stay valid. No other command
  returns these reasons; the session launch command's own `skipped_columns`
  (src/core/ControlCommandsSessionLaunch.cpp:235) counts grid columns without launch content -
  a different command with a different meaning, untouched. (The sibling listing in submit_check
  skips groups over 30 ids - session.* has 40 and clip.* 44 - so it prints no sibling ask; the
  check was done anyway.)
- **No collateral damage: old projects and saved data still load as before, and any limit or
  check you added does not hit other callers that legitimately exceed it.** The change only
  splits one guard's reporting inside a live command reply; no file format, loader, model or
  save path is touched. In-grid pairs land exactly as before (12 pre-existing tests pass
  unchanged, including the genuine `column_has_no_song_track` case with `skipped_columns` 1).
  No new limit is applied to any other caller. Undo is unchanged: one Track journal
  checkpoint per touched track; out-of-grid pairs never touch a track, and the ring-consumption
  contract from round 2 (consume, report not-landed) is preserved.
- **Verdict FIXED evidence (the ALREADY-FIXED / FALSE-POSITIVE clause is N/A).** Verdict is
  FIXED: the original out-of-grid blank-clip bug was real (SessionModel::slot returns the shared
  empty slot out of range, qa/reviews/new-p12-45-port.r2.md:3) and the r2 REWORK misreport is
  fixed at src/core/ControlCommandsSessionRecordLand.cpp:296-311 with the regression above.
- **Realtime / undo / commands.** Nothing in this diff runs on the audio thread: the handler,
  `landPairState` and `countNotLandedPair` are control-socket command code; the audio-thread
  producer side of the ring is untouched. No new control command was added (the
  ControlCommandsSnapshot canary compares command ids, which are unchanged; the committed
  snapshot needs no regeneration). Undo stays one honest step.
