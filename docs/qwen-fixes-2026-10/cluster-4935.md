FIXED

# Cluster 4935: session.set_slot accepted a pattern id the PatternStore does not hold

Status of this report (r2): the product fix is in place, the focused C++ tests pass, and the load family passes. ControlSuccessPaths does NOT pass as a whole: its two MIDI filter checks (project.import, project.export_midi) fail on this host, as they do on the branch base. Those two failures are unrelated to this cluster and are not fixed here.

Log paths below are under `.config/kads/zene/state/runs/4935.r2/logs/`. Logs from r1 are named as r1 and are not used as exit-code evidence.

## 1. Decision and evidence

- Pre-fix, `applySlotReference` (src/core/ControlCommandsSession.cpp, starts at :123) stored any integer for `type:'midi'` through `slot.setPatternReference(...)` (:132 and :149). Its only check was the InvalidArgs for a missing `'pattern'` key; it had no range check on the id.
- The contract is `session.pattern_list`: valid ids are `0..numOfPatterns()-1`. `PatternStore::numOfPatterns()` returns `Engine::getSong()->countTracks(Track::Type::Pattern)` (src/core/PatternStore.cpp:106-108), so the count is the number of Song Pattern tracks.
- The fix refuses ids outside that range at the command boundary. `SessionModel::restoreState` and the direct ClipSlot setters stay permissive, so projects saved before this change still load unchanged.

## 2. The fix

- `checkSlotPatternExists(args, &error)` is at src/core/ControlCommandsSession.cpp:166-175. It is called at :518, right after `gridContains(...)` (:517) and before `applySlotReference` (:525), so a refusal leaves the grid as it was.
- It covers `type:'midi'` and the type-less pattern form (the key choice mirrors `applySlotReference`: empty or `midi` reads `pattern`). Other types return early.
- Refusal: `ControlErrorKind::InvalidArgs`. The message names the id, the store count, and points to `session.pattern_list`.
- Wording change in this commit (r2): the refusal message at :173 changes "add a PatternTrack" to "add a pattern track". Nothing else in the product code changes.
- No schema, command description, or header change.

## 3. Fixture corrections

### 3a. QA socket fixtures, commit 8d5eb70c6a (four script changes)

That commit changes four Python socket fixtures and this report (5 files, 35 insertions, 10 deletions). Each change creates the Pattern track that the fixture's cells name:

- `tests/control-session-m1.py`: Pattern tracks for ids 1 and 2 (ControlSessionLaunch).
- `tests/control-session-lifecycle-transcript.py`: Pattern track for id 1 (ControlSessionLifecycleTranscript).
- `tests/session_api_proof_rows.py`: patterns 1 and 2 for the refusal driver; pattern 0, held by setUp, for the set_slot row (ControlSessionApiProof).
- `tests/control-success-paths.py`: Pattern track for pattern 1 (ControlSuccessPaths).

### 3b. C++ fixtures (earlier cluster commits; r1 readings, not re-run in r2)

- `ControlSessionRecordLandTest.cpp`: init adds a Pattern track; the grid grows from 3x2 to 4x2, and the recorder test moves to column 3 so it still has no song track. Pattern 1 gets a Pattern track for the set_slot refusal.
- `ControlSessionRecordLandUndoTest.cpp`, `MidiCommandBindingsTest.cpp` (two fixtures), `SessionPlaybackRenderTest.cpp`, `SessionCellPlaybackMenuTest.cpp`, `SessionGridRecordTest.cpp` (its one MIDI test only), `SessionLibrarySaveTest.cpp`: each adds the Pattern track its MIDI set_slot names.
- `SessionCommandArgsTest.cpp`: new test `aMidiSlotMustNameAPatternTheStoreHolds` plus helper. It creates two Pattern tracks, so ids 0 and 1 are valid and an id past them is refused.

The call-site audit was an r1 artifact (`logs/p1-audit.md` under `4935.r1`). It is not r2 evidence and is not cited for any exit code here.

## 4. Commands and exit codes (r2)

- Socket run, four registered ctest cases: `test-socket-2.log` -> `EXIT=8`. "75% tests passed, 1 tests failed out of 4".
  - Passed: ControlSessionLaunch (#840), ControlSessionLifecycleTranscript (#841), ControlSessionApiProof (#843).
  - Failed: ControlSuccessPaths (#892) only.
- ControlSuccessPaths checks in `test-socket-2.log`: the session checks are `ok`, including "a slot is built to perform", "Arrangement Record arms", "the land pass writes the one clip the launch/stop pair describes", the audible_diff checks, and the flatten, bounce and grid checks that follow. Two checks FAILED:
  - `project.import adds the MIDI file's track`: "no import filter in this build takes '...one-note.mid'".
  - `project.export_midi writes a .mid`: "no MIDI file was written ... (the midiexport filter ...)".
  These two are unrelated, pre-existing MIDI filter failures on this host. They are not investigated or fixed here.
- Base comparison: `test-socket-base.log` -> `EXIT=8`. ControlSuccessPaths alone, "0% tests passed, 1 tests failed out of 1". The same two checks FAILED on the branch base, so they predate this fix.
- C++ family: `test-cxx-2.log` -> `EXIT=0`. "100% tests passed out of 10".
- RED: `test-red-1.log` -> `EXIT=8`. SessionCommandArgsTest: 7 passed, 1 failed. `Loc: [SessionCommandArgsTest.cpp(344)]`: "'!midi.ok' returned FALSE (a midi slot named a pattern the PatternStore does not hold)". So the new test fails when a midi slot names a missing pattern and the command accepts it.
- GREEN: `test-green-1.log` -> `EXIT=0`. "100% tests passed out of 1".
- Feature check: `feature-check-1.log` -> `EXIT=0`. "RESULT: PASS"; all gates PASS, including file-length and complexity.
- Load family (old-project load, r2): `build-load-r2.log` -> `BUILD_EXIT=0`; `test-load-r2.log` -> `EXIT=0`. "100% tests passed out of 3": SessionModelTest, ScenesUpconvertTest, ProjectOpenIntegrityTest.
- RED/GREEN reverse sequence. `logs/fix.patch` is the one-file diff of `src/core/ControlCommandsSession.cpp` against the pre-fix blob `2b00b7f1e3`, which `git rev-parse 1587cea2a8:src/core/ControlCommandsSession.cpp` returns. Commands, in order:

```
BASE=1587cea2a8    # commit whose ControlCommandsSession.cpp blob is 2b00b7f1e3
git diff $BASE -- src/core/ControlCommandsSession.cpp > logs/fix.patch
git apply -R logs/fix.patch        # reverse the fix
# build-2.log          : build of the reversed tree
# test-red-1.log       : SessionCommandArgsTest -> EXIT=8
git apply logs/fix.patch           # re-apply the fix
# build-3.log          : build of the fixed tree
# test-green-1.log     : SessionCommandArgsTest -> EXIT=0
```

  The packet's `BASE=merge-base` was not used. The merge-base of HEAD and master (`4e677cb6c6`) has no `ControlCommandsSession.cpp`, so a diff against it is a whole-file new-file patch and cannot reverse the fix. The exact build and ctest flags for the four steps are not recorded in their logs; the block above is the sequence, not a transcript. It was not re-run in this session.

Carried from r1 and not counted here: reversed-RED repeats and a reject-all mutant run that failed at :365. Those r1 logs are not cited for exit codes in r2.

## Self-check

- **Regression RED/GREEN:** RED `test-red-1.log` `EXIT=8` at SessionCommandArgsTest.cpp(344). GREEN `test-green-1.log` `EXIT=0`. C++ family `test-cxx-2.log` `EXIT=0`, 10/10. The reverse and re-apply sequence is in section 4; `logs/fix.patch` is the reverse input.
- **Determinism (seam):** The lines cited in the test show no sleep, timer, wall-clock or scheduler wait. The test builds state with `song->clearProject()` (tests/src/core/SessionCommandArgsTest.cpp:319), creates Pattern tracks with `Track::create(Track::Type::Pattern, song)` (:358 and :387), and removes a track through `track.remove` (:376). Read in this session. The rest of the test body was not re-read in r2. r2 has one RED run, so repeat stability is not shown in r2.
- **Old-project load evidence:** Run in r2. `build-load-r2.log` `BUILD_EXIT=0`; `test-load-r2.log` `EXIT=0`, 3/3 passed (SessionModelTest, ScenesUpconvertTest, ProjectOpenIntegrityTest). Old projects load through `SessionModel::restoreState`. `logs/fix.patch` covers only `ControlCommandsSession.cpp`, so `restoreState` is outside the changed file. This run is the acceptance evidence for the claim that old-project load is unchanged.
- **Cause completeness:** The cause is that the midi path stored any integer. The fix covers `type:'midi'` and the type-less pattern form. Restored sessions are not checked by design (SessionModel stays permissive).
- **No collateral damage:** The three socket cases that used to fail (ControlSessionLaunch, ControlSessionLifecycleTranscript, ControlSessionApiProof) pass in `test-socket-2.log`. The 10-test C++ family passes (`test-cxx-2.log`, EXIT=0). ControlSuccessPaths's session checks pass, and its two MIDI filter checks fail the same way on the base.
- **ALREADY-FIXED / FALSE-POSITIVE:** Not applicable. The new test fails on the unfixed code (`test-red-1.log`), so the defect is real.
- **Sibling session.* commands:** Current grep of `src/` for `setPatternReference`, run in r2. Control-argument writers: `applySlotReference` at ControlCommandsSession.cpp:132 and :149, called at :525. The capture writer is ControlCommandsSessionCapture.cpp:99. It runs after it creates its own Pattern track at :81-83, so its id is `patternTrack->patternIndex()`. No other `src/` file in the grep output is a control handler. The get_state, set_grid, set_quantisation, set_scene, clear_slot and clear handlers and the launch/stop and `session.pattern_list` handlers do not call `setPatternReference`. Other callers exist only in SessionTransitions.cpp:160, :174, :187 and :188 (internal transitions; not read in r2). The definition is SessionClip.cpp:123.
- **Sibling app.* and arrangement.* ids:** app.about, app.ask, app.help, app.online_help, app.task_cancel, app.tasks, app.tour, app.tours, app.update_check_get, app.update_check_run, app.update_check_set, app.version, app.whats_new, arrangement.fit_tracks and arrangement.get_state. None of these handlers is in the r2 diff, which changes only one string at :173. Listed from r1; the r2 diff confirms they are not touched.

## Not run / not verified

- The full ctest suite was not run in r2. The focused runs were: the four-case socket ctest, the ControlSuccessPaths base run, the 10-test C++ family, SessionCommandArgsTest RED and GREEN, the feature check, and the load family (`test-load-r2.log`, 3/3).
- The two ControlSuccessPaths MIDI filter failures (project.import, project.export_midi) were not investigated and are not fixed here.

## Other findings

- Python proof scripts and the transcript named patterns that did not exist. They passed only because the baseline did not check pattern existence. The four live scripts were corrected in 8d5eb70c6a (section 3a). The historical transcript is not a live test and was left unchanged.
- The baseline (10/10) had no test that a passing set_slot names an existing pattern. The new C++ test covers that path.
