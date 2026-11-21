FIXED

# Cluster 4935: session.set_slot accepted a pattern id the PatternStore does not hold

## 1. Decision and evidence

- Pre-fix, `applySlotReference` (src/core/ControlCommandsSession.cpp:122-159) stored any integer for `type:'midi'` through `slot.setPatternReference(...)` (about :142-150). It never checked the id.
- The contract is `session.pattern_list`: valid ids are `0..numOfPatterns()-1`. `PatternStore::numOfPatterns()` returns `Engine::getSong()->countTracks(Track::Type::Pattern)` (src/core/PatternStore.cpp:106-108), so the count is the number of Song Pattern tracks.
- The fix refuses ids outside that range at the command boundary. `SessionModel::restoreState` and the direct ClipSlot setters stay permissive, so projects saved before this change still load unchanged.

## 2. The fix

- `checkSlotPatternExists(args, &error)` in src/core/ControlCommandsSession.cpp, called in `registerSessionSetSlot` right after `gridContains(...)` and before `captureSession`, so a refusal happens before any write.
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

Exit codes below are copied from the logs where the log records them. Where the log does not record one, that is stated.

Gates and runs from this session:

- `python3 .../qwen/feature_check.py <worktree> > logs/feature-check-1.log 2>&1; echo EXIT=$?` -> `EXIT=0`. Log: `logs/feature-check-1.log`, RESULT: PASS, 8 gates PASS and two "no new item in your files" lines.
- `cmake --build build --target SessionCommandArgsTest && ctest -R "^SessionCommandArgsTest$"` run via quiet-run `p5-build-green` -> the build succeeded; ctest was refused by the runner from the wrong directory (exit 97, guard message to run from build/tests).
- `ctest -R "^SessionCommandArgsTest$" --output-on-failure` from build/tests via quiet-run `p5-test-green` -> `QR_EXIT=0`, "100% tests passed out of 1", 0.77 sec.

Earlier runs (logs in `logs/`):

- p1 build-0 / test-baseline-0: "100% tests passed out of 10" (10/10). The EXIT line is not in the log file.
- p2 build-1 / test-red-1: `FAIL! : SessionCommandArgsTest::aMidiSlotMustNameAPatternTheStoreHolds() '!midi.ok' returned FALSE`, `Loc: [SessionCommandArgsTest.cpp(344)]`, "7 passed, 1 failed". The log has no EXIT line; the packet records EXIT=8.
- p3 build-2 / test-green-1: "100% tests passed out of 10" (10/10).
- p4 build-3 / test-red-reversed-1: same failure at :344, "7 passed, 1 failed". Packet records EXIT=8; not in log.
- p4 build-4 / test-green-restored-1: "100% tests passed out of 1". Packet records EXIT=0; not in log.
- p4 build-mutant3 / test-mutant-reject-all-3: `Loc: [SessionCommandArgsTest.cpp(365)]` among several refusal-test failures (also :115, :145, :210, :243). Packet records EXIT=8; not in log.
- build-5 / test-green-final-1: "100% tests passed out of 1". Packet records EXIT=0; not in log.
- `cmp` of `git diff -- src/core/ControlCommandsSession.cpp` against logs/fix.patch -> `CMP_EXIT=0` (product diff equals fix.patch).

## Self-check

- **Regression RED/GREEN:** RED: on the unfixed product code the new test fails at SessionCommandArgsTest.cpp:344 (test-red-1.log, test-red-reversed-1.log). GREEN: test-green-1 10/10, test-green-restored-1 and test-green-final-1 pass, and this session's rerun passes (exit 0).
- **Determinism:** The failure reproduces at the same line in both red logs. Green passed in each green log and in this session's rerun. Only one rerun was done this session, so determinism beyond the logs is not separately shown.
- **Cause completeness:** The cause is that the midi path stored any integer. The fix covers both `type:'midi'` and the type-less pattern form. Restored sessions are not checked by design (SessionModel stays permissive). The check does not cover other writers of pattern references; the sibling commands below do not write one.
- **No collateral damage:** Full 10-test baseline and green runs pass with fixtures corrected. The reject-all mutant (test-mutant-reject-all-3.log) fails the refusal tests, which shows the tests catch an over-broad fix. feature_check PASS (EXIT=0).
- **ALREADY-FIXED / FALSE-POSITIVE:** Not applicable. The new test fails on the unfixed code, so the defect is real and not already fixed.
- **Sibling session.* commands:** `set_slot` is the one changed. `launch`, `stop`, `clear`, `get_state`, `set_grid`, `set_scene` and `set_quantisation` do not write a pattern reference. `session.pattern_list` stays the read-only source of valid ids. These claims come from the packet and were not re-checked in this session.

## Other findings

- Python proof scripts and the transcript reference patterns that do not exist, and they pass only because the baseline does not check pattern existence. Sites from logs/p1-audit.md: control-session-m1.py:262 (pattern track+1), control-success-paths.py:51 (pattern 1), session_api_proof_lib.py:222 and :225 (patterns 1 and 2), session_api_proof_rows.py:224, control-session-lifecycle-transcript.py:193, and the historical transcript tests/control-session-m1-transcript.txt:82-88 (ok:true for patterns 1 and 2). These are left as they are; they are out of scope for this change.
- The baseline (10/10) has no test that a passing set_slot names an existing pattern. The new test covers the C++ path only.
- The earlier audit's two statements about applySlotReference (ControlCommandsSession.cpp:142-148) disagree. Not resolved here.
- The packet's EXIT codes for p2, p4 and the final green run are not recorded in the log files on disk (see section 4).
