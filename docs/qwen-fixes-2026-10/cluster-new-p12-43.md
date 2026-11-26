FIXED

Cluster: new-p12-43 (Luna's patrol, "Landing does not preserve session-slot playback settings").

## The finding, and what is actually the defect

`session.arrangement_record_land` creates every landed clip in `sessionrecord::landPairState()`: content copy,
span, auto-resize, name (`src/core/ControlCommandsSessionRecordLand.cpp:375-380` before the fix, the same five statements at `:442-447` after it). It never
touched the clip's playback settings, so a cell launched at -6 dB and looped over a region landed a clip that
played the same audio at unity gain and straight through.

Why that IS a defect and not a matter of taste: a launch renders an audio cell in two layers, and only the first
survives a landing.

- The buffer the launch publishes is already warped and repitched the way the cell asks
  (`src/core/SessionAudioCells.cpp:260` - `prepared.clip = variant(source, original, bakeParamsFor(slot, tempo))`,
  the pitch ratio being `2^((transpose + detune/100)/12)` at `src/core/SessionAudioBake.cpp:46`). The recorder
  captures that very buffer - `src/core/SessionFollow.cpp:518` -
  `m_recorder.recordStart(track, scene, tick, content->patternId, content->clip)` - and the pass copies it, so
  the cell's transpose, detune and tempo warp DO reach the landed clip. The test pins it
  (`aTransposedAudioCellsPitchSurvivesOnTheLandedClip`).
- On top of it the renderer applies the cell's **gain** and its **loop region**
  (`src/core/SessionAudioCells.cpp:261` - `prepared.shape.gain = std::pow(10.0f, slot.gainDb() / 20.0f)`;
  `:263-264` - `loopStartFrames` / `loopLengthFrames` from `slot.loopStart()` / `slot.loopLength()`, folded and
  clamped by `loopRegion()` at `:88-97`). That layer lived only in the published shape, so it was the part a
  landing dropped.

The MIDI half of the finding is NOT a defect, and the fix does not invent it: a MIDI cell's launch publishes the
pattern, its start offset and legato and nothing else (`src/core/SessionPlayback.cpp:225` -
`publishSlotContent(track, scene, patternId, 0, slot.legato(), slot.loopStart(), clip, audio.shape)`), so a
launched MIDI cell renders no gain, no transpose and no loop length. A landed MIDI clip that carried them would
be the clip that sounds different from the launch. `aMidiCellsPlaybackSettingsAreNotInventedOnTheClip` pins the
decision (notes keep key 67, the clip's edits stay neutral).

So: REAL for the audio half (gain and loop region), and the MIDI/list wording of the finding is a
false premise backed by the launch path above.

## The fix

- `src/core/ControlCommandsSessionRecordLand.cpp:333-347` - `withLandingLoop()`: the cell's loop region, in the
  landed clip's own source frames. Measured at the clip's rate (`SampleClip::clipFramesPerTick()`), with the
  start folded into the buffer and the end clamped to what the buffer holds - the launcher's own rule
  (`SessionAudioCells.cpp:88-97`), which is what keeps a loop longer than the audio from being dropped.
- `src/core/ControlCommandsSessionRecordLand.cpp:349-377` - `applySlotPlaybackSettings()`: the gain, through the
  same dB-to-linear conversion the renderer uses and inside the clip's own accepted dB range
  (`MinGainPointDb`/`MaxGainPointDb`, the range `Clip::loadClipEdits` clamps a loaded gain to, so the value
  survives a save and a re-open), and the loop region above - into `Clip::setClipEdits()`, the arrangement's
  non-destructive switchboard for exactly these values. A MIDI clip is skipped, with the reason in the comment.
- `src/core/ControlCommandsSessionRecordLand.cpp:448-449` - `landPairState()` calls it after the name, on the
  cell as the pass finds it (the rule the name and the fallback content already followed), and before
  `dataChanged()`.
- `src/core/ControlCommandsSessionRecordLand.cpp:40-47` - the file's own "WHAT A LANDED CLIP CARRIES" paragraph
  now states what a landed clip carries and why transpose/detune/warp need no transfer.

No new control command, no command id, description or schema change (the `ControlCommandsSnapshot` canary is
unchanged, test #983 passes), no saved-data format change (the clip's gain/loop already round-trip:
`src/core/Clip.cpp:517-519` writes `loopfrom`/`loopframes`, `:713-717` reads them back), and nothing on the audio
thread: all of this runs in the command handler on the model thread. The change is one `ClipEdits` write on a
clip created microseconds earlier inside the pass's own composite journal checkpoint, so the undo stays ONE
honest step (the clip's serialized state carries the edits; `ControlSessionRecordLandUndoTest` still passes).

## The test matrix (and why this file)

New binary `tests/src/core/ControlSessionRecordLandPlaybackTest.cpp` (registered last in
`tests/CMakeLists.txt` -> ctest #759; a two-digit number like `ControlSessionRecordLandTest`'s #45 is padded in
`ctest -N` and invisible to the repo's mechanical negative control, which reads `Test #\d+: (\S+)`).

| Path | Case | Expected |
| --- | --- | --- |
| normal | `anAudioCellsGainAndLoopReachTheLandedClip` | the clip's gain and loop ARE the launcher's own numbers (`sessionaudio::prepare()`'s `shape`, not a constant) |
| failure / clamp | `aLoopLongerThanTheAudioKeepsTheLaunchersClampedRegion` | a loop longer than the audio keeps the launcher's clamped region (`start + frames == sampleSize()`), the pass still lands |
| reconfiguration | `theSettingsAreTheCellsWhenThePassRuns` | the cell as it is at landing time (retuned after the launch) |
| no-op / neutral | `aCellWithNoPlaybackSettingsLandsANeutralClip` | a plain cell lands a neutral clip - no new attribute in a project that never used these |
| completeness (pitch) | `aTransposedAudioCellsPitchSurvivesOnTheLandedClip` | the landed buffer IS the published, repitched one (transpose/detune/warp need no transfer) |
| not-a-defect (MIDI) | `aMidiCellsPlaybackSettingsAreNotInventedOnTheClip` | notes keep their key, edits stay neutral |
| audio thread | none of the above runs on it: the change is in a command handler | - |
| platforms | no new Qt API, no POSIX path arithmetic, no `std::filesystem`; the same sources build on Qt 5/6 | - |

Determinism: the dummy render thread is stopped in `initTestCase()` (the repo's idiom), the scheduler's periods
are driven serially by the test, the tempo and rate are the engine's own, and no sleep, timer or wall clock is
involved. No prerequisite was missing.

## Commands and exit codes (every log is in the run's LOG FOLDER, `.../runs/new-p12-43.r1/logs/`)

RED - the new test against the UNFIXED product code (first run, before the fix existed):

- `cmake --build build --target zene ControlSessionRecordLandPlaybackTest -- -j4 > logs/build-2.log 2>&1` - EXIT=0
- `cd build/tests && ctest -R "^ControlSessionRecordLandPlaybackTest$" --output-on-failure > logs/test-1-red.log 2>&1` - **EXIT=8** - 3 cases fail: gain `1` vs `0.5`, `hasAudioLoop()` false, and the retuned cell.

GREEN - the same test after the fix:

- `cmake --build build --target zene ControlSessionRecordLandPlaybackTest -- -j4 > logs/build-4-green.log 2>&1` - EXIT=0
- `cd build/tests && ctest -R "^ControlSessionRecordLandPlaybackTest$" --output-on-failure > logs/test-2-green.log 2>&1` - **EXIT=0** - 1/1 passed

## Step 4b - the proof that the test catches the bug (FINAL test file, one scripted run)

`logs/step4b-final.log` holds the exact transcript; the commands are the ones in step 4b of the procedure, run
against the final test:

- `git diff -- src/core/ControlCommandsSessionRecordLand.cpp > logs/fix.patch` - **DIFF_EXIT=0** (102 lines; the product change only, no test, no manifest)
- `git apply -R logs/fix.patch` - **REVERT_EXIT=0**
- `cmake --build build --target zene ControlSessionRecordLandPlaybackTest -- -j4 > logs/build-10-red.log 2>&1` - **RED_BUILD_EXIT=0**
- `cd build/tests && ctest -R "^ControlSessionRecordLandPlaybackTest$" --output-on-failure > logs/test-7-red-final.log 2>&1` - **RED_CTEST_EXIT=8** - `Totals: 5 passed, 3 failed`:
  - `anAudioCellsGainAndLoopReachTheLandedClip` - Actual `1`, Expected `0.5` (the clip played the cell's audio at unity gain)
  - `aLoopLongerThanTheAudioKeepsTheLaunchersClampedRegion` - `hasAudioLoop()` returned FALSE (the clip did not loop)
  - `theSettingsAreTheCellsWhenThePassRuns` - Actual `1`, Expected `0.5`
- `git apply logs/fix.patch` - **REAPPLY_EXIT=0**
- `cmake --build build --target zene ControlSessionRecordLandPlaybackTest -- -j4 > logs/build-11-green.log 2>&1` - **GREEN_BUILD_EXIT=0**
- `cd build/tests && ctest -R "^ControlSessionRecordLandPlaybackTest$" --output-on-failure > logs/test-8-green-final.log 2>&1` - **GREEN_CTEST_EXIT=0** - 1/1 passed

The three cases that fail without the product change and pass with it are the regression; the other three pin
behaviour that must not move (neutral cell, repitched buffer, MIDI).

Re-run on the committed tree (same binaries, nothing changed since the green build): `ctest -R
"^ControlSessionRecordLandPlaybackTest$"` from `build/tests` - **CTEST_EXIT=0** (`logs/test-10-postcommit.log`),
and `git status --porcelain` prints nothing.

## The family

- `cmake --build build --target zene ControlSessionRecordLandTest ControlSessionRecordLandUndoTest SessionArrangementRecordTest SessionPlaybackTest SessionAudioCellPlaybackTest ClipEditsTest AudioClipLoopTest ReversibilityContractTest UndoHonestyTest UndoBoundsTest SessionTransitionsTest -- -j4 > logs/build-12-family-final.log 2>&1` - **FAMILY_BUILD_EXIT=0**
- `cd build/tests && ctest -R "^(ControlSessionRecordLandPlaybackTest|ControlSessionRecordLandTest|ControlSessionRecordLandUndoTest|SessionArrangementRecordTest|SessionPlaybackTest|SessionAudioCellPlaybackTest|ClipEditsTest|AudioClipLoopTest|ReversibilityContractTest|UndoHonestyTest|UndoBoundsTest|SessionTransitionsTest|ControlCommandsSnapshot)$" --output-on-failure > logs/test-9-family-final.log 2>&1` - **FAMILY_CTEST_EXIT=0** - 13/13 passed (the new test #759, the land and undo tests #45/#664, the session-engine tests, the clip-edits/loop tests, the undo-honesty tests and the command snapshot #983).

## Gates

`python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/feature-check-1.log 2>&1` - EXIT=0
(6 changed files, before this report was added), and again with the report in the change set -
`... > logs/feature-check-2.log 2>&1` - **EXIT=0** (7 changed files), the same table:

```
feature-check kads-new-p12-43 (base 3436a808b, 6 changed files; logs in .../logs/feature-check)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Files committed

- `src/core/ControlCommandsSessionRecordLand.cpp` (the two helpers, the call site, the file's own paragraph)
- `tests/src/core/ControlSessionRecordLandPlaybackTest.cpp` (NEW - the six cases above)
- `tests/CMakeLists.txt` (the new source registered at the END of `LMMS_TESTS`; declared in `tests/upstream-modifications.txt`)
- `tests/fork-sources.txt`, `tests/all-sources.txt` (the new test registered in both scope manifests; the
  all-sources entry list was re-derived with the manifest's own `Regenerate with` recipe, not hand-edited -
  `bash tests/all-sources-reproduce.sh` prints `REPRODUCES`, EXIT=0)
- `tests/upstream-modifications.txt` (the reason for the `tests/CMakeLists.txt` line)
- `docs/qwen-fixes-2026-10/cluster-new-p12-43.md` (this report)

## Re-verified in this session (the committed tree, fresh logs)

The whole chain was run again in this session, on the committed tree, with the product change reverted and
restored a second time. Every log named below is a NEW numbered file in `.../runs/new-p12-43.r1/logs/`.

- `git diff 3436a808b6 -- src/core/ControlCommandsSessionRecordLand.cpp > logs/fix.patch` - **DIFF_EXIT=0**, 102 lines (the product change only, no test, no manifest).
- `cmake --build build --target zene ControlSessionRecordLandPlaybackTest -- -j4 > logs/build-13-verify.log 2>&1` - **BUILD_EXIT=0**
- `cd build/tests && ctest -R "^ControlSessionRecordLandPlaybackTest$" --output-on-failure > logs/test-11-verify-green.log 2>&1` - **CTEST_EXIT=0** (`1/1 Test #759: ControlSessionRecordLandPlaybackTest ... Passed`)
- `git apply -R logs/fix.patch` - **REVERT_EXIT=0**
- `cmake --build build --target zene ControlSessionRecordLandPlaybackTest -- -j4 > logs/build-14-verify-red.log 2>&1` - **RED_BUILD_EXIT=0**
- `cd build/tests && ctest -R "^ControlSessionRecordLandPlaybackTest$" --output-on-failure > logs/test-12-verify-red.log 2>&1` - **RED_CTEST_EXIT=8**, `Totals: 5 passed, 3 failed`:
  - `anAudioCellsGainAndLoopReachTheLandedClip` - `Compared floats are not the same (fuzzy compare)`
  - `aLoopLongerThanTheAudioKeepsTheLaunchersClampedRegion` - `'copied->clipEdits().hasAudioLoop()' returned FALSE. (the landed clip lost the cell's loop)`
  - `theSettingsAreTheCellsWhenThePassRuns` - `Compared floats are not the same (fuzzy compare)`
- `git apply logs/fix.patch` - **REAPPLY_EXIT=0**, and `git status --porcelain` printed nothing: the tree is the committed one again.
- `cmake --build build --target zene ControlSessionRecordLandPlaybackTest -- -j4 > logs/build-15-verify-green2.log 2>&1` - **GREEN_BUILD_EXIT=0**
- `cd build/tests && ctest -R "^ControlSessionRecordLandPlaybackTest$" --output-on-failure > logs/test-13-verify-green2.log 2>&1` - **GREEN_CTEST_EXIT=0** (`100% tests passed out of 1`)
- family rebuilt and re-run: `cmake --build build --target ControlSessionRecordLandPlaybackTest ControlSessionRecordLandTest ControlSessionRecordLandUndoTest SessionArrangementRecordTest SessionPlaybackTest SessionAudioCellPlaybackTest ClipEditsTest AudioClipLoopTest ReversibilityContractTest UndoHonestyTest UndoBoundsTest SessionTransitionsTest -- -j4 > logs/build-16-family-verify.log 2>&1` - **FAMILY_BUILD_EXIT=0**; the 13-test `ctest -R` of the section above > `logs/test-14-family-verify.log 2>&1` - **FAMILY_CTEST_EXIT=0**, `100% tests passed out of 13`.
- gates, re-run on this tree: `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/feature-check-3.log 2>&1` - **FEATURE_CHECK_EXIT=0**:

```
feature-check kads-new-p12-43 (base 3436a808b, 7 changed files)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

- `submit_check.py` first run (`logs/submit-check-1.log`) - EXIT=1 with one FAIL: `working tree clean - ?? Testing/`. `Testing/` was a ctest scratch directory (`Testing/Temporary/CTestCostData.txt`, `LastTest.log`) left at the worktree root by a test invocation, not a source file; `rm -rf Testing` removed it and `git status --porcelain` is empty.
- after that, `python3 .../submit_check.py <worktree> new-p12-43 > logs/submit-check-3.log 2>&1` - **SUBMIT_CHECK_EXIT=0**, every check PASS (`commits on the branch`, `working tree clean`, `report`, `tests added or changed`, `new test files registered`, `gates`) and `RESULT: PASS - submit`.

Code evidence re-read in this session at the current line numbers:

- `src/core/ControlCommandsSessionRecordLand.cpp:366` `applySlotPlaybackSettings()`, `:333` `withLandingLoop()`, called at `:449` inside `landPairState()` (declared `:424`), whose ONLY caller is `:530` - one landed-clip creation path.
- `src/core/SessionAudioCells.cpp:261` `prepared.shape.gain = std::pow(10.0f, slot.gainDb() / 20.0f);`, `:263-264` the loop start/length in frames, folded and clamped by `loopRegion()` at `:89-97` - the two values the landing used to drop.
- `src/core/SessionAudioBake.h` / `SessionFollow.cpp:518` `m_recorder.recordStart(track, scene, tick, content->patternId, content->clip)` - the pass copies the buffer the launch published, so transpose/detune/warp travel with it.
- `src/core/SessionPlayback.cpp:225` - a MIDI cell publishes a pattern, a start offset and legato, and `loopLength` 0: no gain, no transpose, no loop length to preserve.

## Self-check

- **This session's re-run is the evidence for the first ASK item.** Reverted: `RED_CTEST_EXIT=8`, 3 of 6 cases failed (`logs/test-12-verify-red.log`). Restored and rebuilt: `GREEN_CTEST_EXIT=0`, 1/1 passed (`logs/test-13-verify-green2.log`). The earlier run recorded in the section above gives the same two exit codes, so the result is reproducible, not incidental.
- **The regression test FAILS with the product change reverted and PASSES with it.** Reverted: `RED_CTEST_EXIT=8`, `Totals: 5 passed, 3 failed` with the three messages quoted above (`logs/test-7-red-final.log`). Restored: `GREEN_CTEST_EXIT=0`, 1/1 passed (`logs/test-8-green-final.log`). Both runs are on the final test file, and the 4b transcript with every exit code is `logs/step4b-final.log`.
- **The test is deterministic.** No wall-clock timing, no sleeps, no timers, no threads: `initTestCase()` stops the dummy render thread (`Engine::audioEngine()->audioDev()->stopProcessing()`, the idiom `ControlSessionRecordLandTest` uses), the scheduler's periods are driven serially with an injected `SessionClockContext` (`framesPerTick = 1.0f`, `positionTicks` set by the test), the tempo and the engine rate are the engine's own values, and the expected gain/loop numbers are read back from `sessionaudio::prepare()` - the launcher's own computation, not a hard-coded frame count.
- **The fix is at the cause and complete.** Callers and siblings checked: `landPairState()` has exactly ONE caller (the handler's per-pair loop, `src/core/ControlCommandsSessionRecordLand.cpp:528-530`), so there is no second path that creates a landed clip; `session.arrangement_record_land` is the only command in this file and no other command turns a session cell into an arrangement clip (`ControlCommandsBounceLand.cpp:54`, `ControlCommandsRangeBounce.cpp:70`, `ControlCommandsRecorder.cpp:118` and `ControlCommandsRecordingPlace.cpp:68` create clips from a bounced FILE or a recorder take, with no session slot to read); the wider type the value flows into is the clip's own `ClipEdits`, whose gain range and loop-frame convention the fix obeys (`MinGainPointDb`/`MaxGainPointDb` as `Clip::loadClipEdits` clamps them; source frames as `ClipAudioLoop.h`/`audioloop::engaged()` define them; the end clamped as `SessionAudioCells.cpp` `loopRegion()` clamps it). The finding's transpose/detune/warp half is covered by the copy of the published buffer, and the MIDI half is documented as not-a-defect with the publish call it rests on.
- **No collateral damage.** A cell with no gain and no loop lands a clip whose `ClipEdits` stay neutral, so a project that never used these settings writes no new clip attribute (`aCellWithNoPlaybackSettingsLandsANeutralClip`); old project files are only READ here and the load path is untouched; the gain written is inside the range the clip's own loader accepts, so a save and a re-open give the same value back; the loop is clamped, never refused, so no pair that landed before stops landing (`aLoopLongerThanTheAudioKeepsTheLaunchersClampedRegion`); the MIDI path is untouched (`aMidiCellsPlaybackSettingsAreNotInventedOnTheClip`).
- **Realtime, undo and commands.** Nothing added runs on the audio thread: `applySlotPlaybackSettings()` and `withLandingLoop()` are called from the command handler on the model thread and only read the slot and the clip; no allocation, lock, wait or log was added to any render path (the only allocation is the `ClipEdits` copy in a handler that is already allocating clips). The undo stays ONE honest step: the change lands inside the clip the pass creates under the pass's own composite `ProjectJournal` checkpoint, and `ControlSessionRecordLandUndoTest` (#664) still passes. No control command was added, renamed or re-described, so the `ControlCommandsSnapshot` canary does not move (#983 passes).
- **ALREADY-FIXED / FALSE-POSITIVE does not apply to the audio half.** The code that shows the bug was present on this branch is `src/core/ControlCommandsSessionRecordLand.cpp:375-380` before the fix (create the clip, copy the content, set the span, the auto-resize and the name - and no `setClipEdits`), against the launcher's own contract at `src/core/SessionAudioCells.cpp:261-264`; both were measured RED (`logs/test-7-red-final.log`). The MIDI half IS a false premise, and its evidence is `src/core/SessionPlayback.cpp:225`.
- **Sibling commands in the files I changed.** `src/core/ControlCommandsSessionRecordLand.cpp` registers exactly one command (`session.arrangement_record_land`) - unchanged in id, description, args and result schema; `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt` and `tests/upstream-modifications.txt` are registration only.

## Other findings

- None worth a separate ticket from this work: the two things I noticed while reading (`session.set_slot`'s
  `gain_db` schema is unbounded while a project file's `<clip gain>` and the session cell's own loaded gain are
  held to -60..+24 dB; and a MIDI cell's `loop_start` is a start offset at launch while its `loop_length` is
  ignored) are pre-existing and outside this finding. The gain clamp I added covers the landed clip only, so the
  first one is still open wherever else a slot gain is rendered.
