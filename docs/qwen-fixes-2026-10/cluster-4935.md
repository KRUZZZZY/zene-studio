FIXED

# Cluster 4935: session.set_slot accepted a pattern id the PatternStore does not hold

## 1. Decision and evidence

- Pre-fix, `applySlotReference` (src/core/ControlCommandsSession.cpp:122-159) stored any integer for `type:'midi'` through `slot.setPatternReference(...)` (about :142-150). Its only checks were the InvalidArgs for a missing `'pattern'` key; it had no range check on the id.
- The contract is `session.pattern_list`: valid ids are `0..numOfPatterns()-1`. `PatternStore::numOfPatterns()` returns `Engine::getSong()->countTracks(Track::Type::Pattern)` (src/core/PatternStore.cpp:106-108), so the count is the number of Song Pattern tracks.
- The fix refuses ids outside that range at the command boundary. `SessionModel::restoreState` and the direct ClipSlot setters stay permissive, so projects saved before this change still load unchanged.

## 2. The fix

- `checkSlotPatternExists(args, &error)` in src/core/ControlCommandsSession.cpp:166-181 (final form, compacted for the file-length gate: one-line early return and range return, one count expression with a null check on `Engine::patternStore()`, one-line failure message). Behavior is the same as the earlier form. It is called at :524 in the set_slot handler, right after `gridContains(...)` (:523) and before any write, so a refusal leaves the grid as it was.
- It covers `type:'midi'` and the type-less pattern form (the key choice mirrors `applySlotReference`: empty or `midi` reads `pattern`). Other types (audio, empty) return early.
- Refusal: `ControlErrorKind::InvalidArgs`, message names the id, the store count, and points to `session.pattern_list`.
- No schema, command description, or header change. The only product-file addition is `#include "PatternStore.h"` in the .cpp.

## 3. Call-site audit and fixture corrections

Full audit: logs/p1-audit.md (C++ and Python tables with verdicts). Summary:

- The baseline (10/10) passes while several MIDI call sites name pattern indices with no Pattern track behind them. So the suite did not check pattern existence.
- `ControlSessionRecordLandTest::init` (:127-128, pattern 0) is INVALID: `clearProject` (:123) clears the song and creates no Pattern track. The only Pattern track creation on this path is Song.cpp:1901 inside `createNewProject`.
- Fixture corrections, one per changed file:
  - `ControlSessionRecordLandTest.cpp`: init adds a Pattern track (:127) and the grid goes from 3x2 to 4x2. The grid had to grow because the recorder test used column 2, which is now a song track; the test moved to column 3 (:471-472) so it still has no song track, keeping the "grid wider than the song's track list" case. Pattern 1 gets a Pattern track at :214 for the set_slot refusal.
  - `ControlSessionRecordLandUndoTest.cpp` (:102): adds a Pattern track before the MIDI set_slot.
  - `MidiCommandBindingsTest.cpp` (:388, :497): adds a Pattern track in the two MIDI fixtures.
  - `SessionPlaybackRenderTest.cpp` (:196): adds a Pattern track in the MIDI fixture.
  - `SessionCellPlaybackMenuTest.cpp` (:158): adds a Pattern track.
  - `SessionGridRecordTest.cpp` (:245): adds a Pattern track only in its one MIDI test, not in the fixture shared by the other tests.
  - `SessionLibrarySaveTest.cpp` (:177): adds a Pattern track.
  - `SessionCommandArgsTest.cpp` (+90 lines): new test `aMidiSlotMustNameAPatternTheStoreHolds` plus its helper. It creates two Pattern tracks, so pattern 0 and 1 are valid and an id past them is refused. The file's other checks are unchanged apart from the new `PatternTrack.h` include.

## 4. Commands and exit codes

Final attempt, on the compacted product code. Run logs are in `.config/kads/zene/state/runs/4935.r1/logs/`. Each entry below is a command whose log contains its EXIT line, and the values were read in this session.

- `python3 .../qwen/feature_check.py <worktree>` (log `feature-check-3.log`) -> `EXIT=0`. RESULT: PASS. `file-length`: "no new item in your files" (the file is back under the 500-line gate after compaction). `complexity`: "no new item in your files".
- `git diff "$BASE" -- src/core/ControlCommandsSession.cpp > logs/fix.patch` -> `EXIT=0`, 42 lines.
- Reversed RED: `git apply -R logs/fix.patch`, build (`build-9.log`, `BUILD_EXIT=0`), then `ctest -R '^SessionCommandArgsTest$'` (`test-red-reversed-3.log`) -> `EXIT=8`. Failure: `Loc: [SessionCommandArgsTest.cpp(344)]`, "0% tests passed, 1 tests failed out of 1".
- Restored GREEN: `git apply logs/fix.patch`, build (`build-10.log`, `BUILD_EXIT=0`), then `ctest -R '^SessionCommandArgsTest$'` (`test-green-restored-3.log`) -> `EXIT=0`, "100% tests passed out of 1".
- Focused 10-test family (`test-green-3.log`) -> `EXIT=0`, "100% tests passed out of 10".
- Load family `SessionModelTest|ScenesUpconvertTest|ProjectOpenIntegrityTest` (`test-load-2.log`) -> `EXIT=0`, "100% tests passed out of 3".
- `git diff "$BASE" -- src/core/ControlCommandsSession.cpp | cmp - logs/fix.patch` -> `CMP_EXIT=0`.

Earlier runs in this cluster (logs `build-0`, `test-baseline-0`, `test-red-1`, `test-green-1`, `test-red-reversed-1`, `test-mutant-reject-all-3`, and the -1 and -2 series) are not listed here, because their logs do not carry the EXIT line this section relies on. Where they are cited below, that is stated.

## Self-check

- **Regression RED/GREEN:** RED on the final compacted code: `test-red-reversed-3.log`, EXIT=8, failure at SessionCommandArgsTest.cpp:344. GREEN on the final code: `test-green-restored-3.log`, EXIT=0, and the 10-test family `test-green-3.log`, EXIT=0, 10/10.
- **Determinism:** The seam is explicit. The test builds its state with `song->clearProject()` (SessionCommandArgsTest.cpp:319), creates the Pattern track with `Track::create(Track::Type::Pattern, song)` (:358 and :387), and removes a track through the `track.remove` command (:376). It has no sleep, timer, wall-clock or scheduler race. Two reversed-RED runs fail at the same line: `test-red-reversed-2.log` and `test-red-reversed-3.log`, both EXIT=8 at SessionCommandArgsTest.cpp:344. The green result is reproduced by `test-green-restored-3.log` and `test-green-3.log`, both EXIT=0.
- **Cause completeness:** The cause is that the midi path stored any integer. The fix covers both `type:'midi'` and the type-less pattern form. Restored sessions are not checked by design (SessionModel stays permissive).
- **No collateral damage:** The 10-test family passes 10/10 and the load family passes 3/3 on the final code (`test-green-3.log`, `test-load-2.log`, both EXIT=0). The reject-all mutant, rerun on the final code (`test-mutant-reject-all-5.log`, EXIT=8, failure at SessionCommandArgsTest.cpp:365 `QVERIFY2(accepted.ok, ...)`, build `build-mutant-5.log` BUILD_EXIT=0), shows the test catches an over-broad fix. The mutant was a temporary edit to `checkSlotPatternExists` and was restored from `logs/final-fixed.cpp`; `test-green-final-2.log` (EXIT=0, 100% of 1) and `build-final-2.log` (BUILD_EXIT=0) confirm the restored code, and `git diff -- src include` is empty.
- **ALREADY-FIXED / FALSE-POSITIVE:** Not applicable. The new test fails on the unfixed code, so the defect is real and not already fixed.
- **Sibling session.* commands:** Checked with `grep -n setPatternReference` over `src/core/ControlCommandsSession*.cpp`, and by reading the cited ranges. The direct writers are `applySlotReference` (ControlCommandsSession.cpp:132 and :149, the set_slot path, called at :525) and ControlCommandsSessionCapture.cpp:99. The Capture writer creates its own Pattern track first (:81-83), then writes the index of that track, so the id is in range by construction and does not need the check. In ControlCommandsSession.cpp, the handlers for get_state (:258), set_grid (:318), set_quantisation (:353), set_scene (:423), clear_slot (:542) and clear (:580) contain no `setPatternReference` call. ControlCommandsSessionLaunch.cpp has no `setPatternReference` (grep count 0); its registrations at :117, :190, :246 and :284 (session.launch_slot, session.launch_scene, session.stop_slot, session.stop_all) contain none. `session.pattern_list` (ControlCommandsSessionPatterns.cpp:47-67) builds a QJsonArray from `numOfPatterns()` and `findPatternTrack()` and writes nothing; its file has no `setPatternReference` (grep count 0). So "read-only" is confirmed by the body, not only by the grep.
- **Sibling app.* and arrangement.* ids (submit_check's broader list):** app.about, app.ask, app.help, app.online_help, app.task_cancel, app.tasks, app.tour, app.tours, app.update_check_get, app.update_check_run, app.update_check_set, app.version, app.whats_new, arrangement.fit_tracks and arrangement.get_state come from command files changed on the branch base. They are not in the files this fix touched. The three cluster commits (a9da7ff, 1d7324a, 877fe1f) touch only `src/core/ControlCommandsSession.cpp`, the SessionCommandArgs/ControlSession/MidiCommandBindings/SessionPlayback/SessionCellPlayback/SessionGridRecord/SessionLibrarySave tests, and this report. None of them changes an app.* or arrangement.* handler or schema, so these ids are unaffected.

## Other findings

- Python proof scripts and the transcript referenced patterns that did not exist, and they passed only because the baseline did not check pattern existence. Sites from logs/p1-audit.md: control-session-m1.py:262 (pattern track+1), control-success-paths.py:51 (pattern 1), session_api_proof_lib.py:222 and :225 (patterns 1 and 2), session_api_proof_rows.py:224, control-session-lifecycle-transcript.py:193, and the historical transcript tests/control-session-m1-transcript.txt:82-88 (ok:true for patterns 1 and 2). The four live scripts that name them were corrected in the reviewer follow-up below; the historical transcript is a committed record, not a live test, and is left as it is.
- The baseline (10/10) had no test that a passing set_slot names an existing pattern. The new test covers the C++ path, and the reviewer follow-up below adds the missing Python fixtures so the registered socket tests hold the patterns they name.

## Reviewer follow-up

The first review found four registered ctest cases that the product change broke because their fixtures named PatternStore ids that did not exist:

- `ControlSessionLaunch` (tests/control-session-m1.py) and `ControlSessionLifecycleTranscript` (tests/control-session-lifecycle-transcript.py) now create the Pattern tracks their cells name (ids 1 and 2, and id 1, respectively).
- `ControlSessionApiProof` (tests/session_api_proof_rows.py) creates patterns 1 and 2 for the refusal driver and uses pattern 0, which the setUp holds, for the set_slot row.
- `ControlSuccessPaths` (tests/control-success-paths.py) creates the Pattern track for pattern 1.

With the product change in place, `ControlSessionLaunch`, `ControlSessionLifecycleTranscript`, and `ControlSessionApiProof` pass (`ctest -R '^(ControlSessionLaunch|ControlSessionLifecycleTranscript|ControlSessionApiProof)$'`, EXIT=0). `ControlSuccessPaths`'s session checks now pass; its two `project.import`/`project.export_midi` checks still fail on this host for an unrelated missing MIDI filter, exactly as they do on the branch base.
