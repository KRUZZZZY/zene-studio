FIXED

# Cluster new-p9-199: duplicate source root in chord stacking

## Verdict

FIXED. A MIDI-driven instrument sounded the chord root twice: once from the parent note's MIDI note-on, and again from the zero-interval chord child that the stacking step creates. The guard in `InstrumentFunctionNoteStacking::processNote` now skips that one child when the track's instrument is MIDI-based. Note-handle instruments keep the child, as before.

## Cause

- `src/core/NotePlayHandle.cpp:325-329`: the parent note sends its MidiNoteOn to the instrument (`processOutEventForNote`) before chord stacking runs.
- `src/tracks/InstrumentTrack.cpp:776`: `m_noteStacking.processNote( n )` runs afterward and creates the chord children, including the interval-0 child at the base key.
- For a MIDI-based instrument, that interval-0 child re-sends the root. The root therefore sounds twice.
- `include/Instrument.h:65` defines `IsMidiBased`; `include/Instrument.h:184-186` reads it through `isMidiBased()`.

## Fix

- `src/core/InstrumentFunctions.cpp:257-264`: when `sub_note_key == base_note_key`, the track has a non-null instrument, and `isMidiBased()` is true, the loop `continue`s. No child is created for that interval. The include of `Instrument.h` is added at `:30`.
- Note-handle instruments, and tracks with no instrument, take the same path as before.
- `tests/src/plugins/MpeTestConsumer.cpp:169`: the logging test instrument is constructed with `Flag::IsMidiBased`, so the regression test runs through the guarded path.

## Tests

- `tests/src/core/ArticulationPlaybackTest.cpp:227-236` (`isArticulatedMajorChord`): a group is one articulation-led chord only if it is `p7`, `c11=90`, `k25`, `k60` (the one source root), then exactly `k64` and `k67` in any order. Two root sounds fail this check.
- `tests/src/core/ArticulationPlaybackTest.cpp:563-589`: `everyStackedArpeggioStepFollowsItsArticulation` and `aStackedChordFollowsItsArticulation` assert that the articulation comes before every key and that each step is sent once.
- `tests/src/core/NoteCurvesAcrossTest.cpp:335`: `QCOMPARE(stackedKeys, QList<int>{NumKeys - 1})` requires the interval-0 child to exist for that source. `NoteCurvesAcrossTest.cpp:111` creates the track with `Track::create` and no instrument, so the assertion at `:335` proves that the no-instrument path keeps the interval-0 child. It passed in p2 (see below). Non-MIDI instruments take the same false branch: `isMidiBased()` is false, so the new `continue` does not apply to them. This is covered by the guard condition and by the unchanged NoteCurves run.

## Commands and results

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-199.r3/logs`

Exit codes: the packet's p1-p4 record gives the exit codes below. The log files in this folder contain no `EXIT` lines, so these exit codes are not independently verified from the logs. The test output lines are verified from the logs.

- p1 red (before the fix): `cd build/tests && ctest -R '^ArticulationPlaybackTest$' --output-on-failure`. Log `n199-r3-01-test-red.log`: `Test #333: ArticulationPlaybackTest ***Failed`; `333 - ArticulationPlaybackTest (Failed)`. Exit 8 (from the p1 record).
- p2 green (with the fix), sibling suite: `n199-r3-02-test-green.log`: `1/4 Test #176: MpePlaybackTest Passed`; `2/4 Test #327: NoteCurvesAcrossTest Passed`; `3/4 Test #330: NotePlaybackTest Passed`; `4/4 Test #333: ArticulationPlaybackTest Passed`; `100% tests passed out of 4`.
- p2 direct run: `n199-r3-02-artic-direct.log`: `Totals: 17 passed, 0 failed, 0 skipped`.
- p3 reversed (guard removed, build and test): `n199-r3-03-build-reverted.log` and `n199-r3-03-test-reverted.log`: `Test #333: ArticulationPlaybackTest ***Failed`. Exit 8 (from the p3 record).
- p3 restored: `n199-r3-03-test-restored.log`: `Test #333: ArticulationPlaybackTest Passed`; `100% tests passed out of 1`. Exit 0 (from the p3 record). `cmp` of the restored product file: exit 0 (from the p3 record).
- p4 gates: `n199-r3-04-gates.log`: `PASS fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`, `no-tautology`, `doc-figures`, `complexity`; `RESULT: PASS`. Exit 0 (from the p4 record).
- p4 test matrix: `n199-r3-04-matrix.md`.

### Exact commands and exit codes

These are the literal command lines, in order. Runs were made in the work tree `kads-new-p9-199`. p1 and p2 were run through quiet-run; p3 and p4 were run directly (cmake, ctest, git and feature_check were not wrapped). All exit codes are the worker records, not the logs: the copied logs contain no EXIT lines, so the exit codes are not independently verified from the logs.

p1 (red, before the fix; through quiet-run):
1. `git show 2c1029ec3d135da9f35318c0b0688f5222a78a88 -- tests/src/core/ArticulationPlaybackTest.cpp tests/src/plugins/MpeTestConsumer.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-199.r3/logs/n199-r3-01-test-side.patch` → exit 0
2. `git apply /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-199.r3/logs/n199-r3-01-test-side.patch` → exit 0
3. `cmake --build build --target ArticulationPlaybackTest zene` → exit 0
4. `cd build/tests && ctest -R '^ArticulationPlaybackTest$' --output-on-failure` → exit 8

p2 (green, with the fix, sibling suite; through quiet-run):
5. `cmake --build build --target zene ArticulationPlaybackTest NoteCurvesAcrossTest MpePlaybackTest NotePlaybackTest` → exit 0
6. `cd build/tests && ctest -R '^(ArticulationPlaybackTest|NoteCurvesAcrossTest|MpePlaybackTest|NotePlaybackTest)$' --output-on-failure` → exit 0

p3 (fix reversed, then restored; run directly):
7. `git diff -- src/core/InstrumentFunctions.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-199.r3/logs/fix.patch` → exit 0
8. `git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-199.r3/logs/fix.patch` → exit 0
9. `cmake --build build --target ArticulationPlaybackTest zene` → exit 0
10. `cd build/tests && ctest -R '^ArticulationPlaybackTest$' --output-on-failure` → exit 8
11. `git apply /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-199.r3/logs/fix.patch` → exit 0
12. `cmake --build build --target ArticulationPlaybackTest zene` → exit 0
13. `cd build/tests && ctest -R '^ArticulationPlaybackTest$' --output-on-failure` → exit 0
14. `git diff -- src/core/InstrumentFunctions.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-199.r3/logs/fix-restored.patch` → exit 0
15. `cmp /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-199.r3/logs/fix.patch /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-199.r3/logs/fix-restored.patch` → exit 0

p4 (run directly):
16. `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-199` → exit 0
17. `rg -n 'isMidiBased|IsMidiBased' src include` → exit 0
18. `git diff --name-only` → exit 0

## Not verified

- No audio rendering through a real plugin. The regression check counts events on a logging test instrument, not rendered audio.
- No ThreadSanitizer run. The guard is a pointer check and a flag read, with no allocation, lock, wait, I/O or logging.
- The layer path (`src/tracks/InstrumentTrackNotes.cpp:297`, then `InstrumentLayers::playNote`) is not changed by this fix and was not re-checked here.
- The exit codes listed above, other than the logged test lines, come from the p1-p4 record and are not in the logs.

## Self-check

1. **Regression red/green (the test fails with the change reverted and passes with it).** Red: p1 ArticulationPlaybackTest failed (`n199-r3-01-test-red.log`, Test #333, exit 8 per the p1 record). Reverted: p3 build and test failed again (`n199-r3-03-test-reverted.log`, Test #333, exit 8 per the p3 record). Green: p3 restored test passed (`n199-r3-03-test-restored.log`, Test #333, exit 0 per the p3 record). Sibling suite green 4/4 in p2 (`n199-r3-02-test-green.log`). The exit codes are not in the logs (no EXIT lines), so they are unverified from the logs; the test-status lines are verified.
2. **Determinism / seam.** `ArticulationPlaybackTest.cpp` contains no `sleep`, `qWait`, `msleep`, `usleep`, `chrono`, `QElapsedTimer` or `QTime` (grep: 0 matches). The test accepts the two non-root voices in any order: `isArticulatedMajorChord` sorts them (`:234-235`), because the engine orders handles created in one period by its own rule. The injected seam is `MpeTestConsumer.cpp:169` (`Flag::IsMidiBased`), which routes the test through the guarded path without a real plugin. Not verified: the bodies of `play()` and `heard()` were not read, so engine-level frame determinism is not checked here.
3. **Cause and sibling coverage.** Cause: the parent MIDI note-on is sent at `NotePlayHandle.cpp:325-329` before chord stacking at `InstrumentTrack.cpp:776`, so the interval-0 child re-sends the root. Call sites: a grep of `src` and `include` for `noteStacking.processNote` returns one line, `InstrumentTrack.cpp:776`. Siblings: the chord path (`aStackedChordFollowsItsArticulation`) and the arpeggio path (`everyStackedArpeggioStepFollowsItsArticulation`) are both in the green run. The no-instrument path is covered by NoteCurvesAcrossTest (green in p2; the track is created with no instrument at `NoteCurvesAcrossTest.cpp:111`): its assertion at `NoteCurvesAcrossTest.cpp:335` needs the interval-0 child, and it passed, so that child is kept on that path. Not checked: the other `ChordTable` users (`ChordVocabulary.cpp`, `ControlCommandsChord.cpp`, `ControlCommandsScale.cpp`) read the table and were not traced further.
4. **Collateral / persistence.** The guard changes behavior only when the track has a non-null, MIDI-based instrument. Note-handle and no-instrument tracks create the interval-0 child as before. The no-instrument path is proven by NoteCurvesAcrossTest (green in p2, track created with no instrument at `NoteCurvesAcrossTest.cpp:111`). Non-MIDI instruments take the same false branch, because `isMidiBased()` is false for them. No saved-data format, setting or limit is added. Loading of old projects was not run. `tests/upstream-modifications.txt` gains one line (diff: +1).
5. **Verdict evidence.** The verdict is FIXED, so the ALREADY-FIXED / FALSE-POSITIVE clause does not apply. The verdict rests on the red/green pair (p1 red, p2 and p3 green), the cause references above, and the p4 gates log (`gates` `RESULT: PASS`, `n199-r3-04-gates.log`). The fix is the commit that contains this report, `src/core/InstrumentFunctions.cpp`, the two test files and `tests/upstream-modifications.txt`.
