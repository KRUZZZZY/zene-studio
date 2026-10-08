# Merge regression: audio cells lost their content-table entry (2026-10-08)

Branch `bugs/m0-fix-audiocell`, starts at `28cc3a0b5` (train `050/m0-train-2`). Fix commit `c2071bdee`.

## Cause

`SessionSlotContentTable::install()` (`src/core/SessionPlayback.cpp`) decided "nothing to render" from
`patternId < 0` alone. An AUDIO cell publishes `patternId = -1` together with its clip
(`publishSessionSlotContent`, `src/core/SessionPlayback.cpp`, the `clip` argument from `sessionaudio::prepare`).
Two commits from the engine lane treated that as an empty cell:

- `390ec761c` (not on train-1): a new cell with `patternId < 0` returned before claiming an entry, so an audio
  cell never got one.
- `9ce2e115d` (rework): an existing cell with `patternId < 0` gave its entry back, so an audio cell that was
  already in the table lost its entry and clip on the next re-publish.

Either alone breaks audio playback. `playbackForColumn()` then finds no content, `patternId` stays below zero,
and the column renders silence.

## Fix

`install()` now uses `hasContent = patternId >= 0 || clip != nullptr`. Only a cell with neither a pattern nor a
clip takes no entry or gives its entry back. The swap-remove and the bounded walk are unchanged, so the audio
thread still does not allocate or lock. `include/SessionPlayback.h` doc comment updated to match.

## Test

`SessionPlaybackTest::anAudioCellWithoutAPatternKeepsItsEntryAndClip` (new): install an audio cell with
`patternId -1` and a clip, check it keeps one entry and its clip, re-publish it in place, then publish it with
no clip and no pattern and check the entry is returned. The test uses a sentinel pointer for the clip because
the table never reads it, and constructing a `SampleBuffer` needs a running Engine (its default member
initialiser calls `Engine::audioEngine()`, which is null in this test binary; the first attempt segfaulted for
that reason).

## Results

| Item | Verdict | Fix commit | Test | Red exit | Green exit | Gate exits |
| --- | --- | --- | --- | --- | --- | --- |
| audiocell (merge regression) | FIXED | c2071bdee | SessionAudioCellPlaybackTest (3 cases), SessionPlaybackTest::anAudioCellWithoutAPatternKeepsItsEntryAndClip | 8 (3 failures at SessionAudioCellPlaybackTest.cpp:212 and :178, as reported); new unit test 8 against the pre-fix SessionPlayback.cpp | 0 (all suites below) | complexity 0, file-length 0, fork-sources 0, no-upstream-regression 0 |

Suites run after the fix (ctest from `build/tests`, one at a time):

- SessionAudioCellPlaybackTest: 0 (was 8, three failing cases; includes theCellRendererDoesNotAllocate)
- SessionPlaybackTest: 0 (includes the 390ec761c regression emptyCellsDoNotConsumeTheContentTable and the
  9ce2e115d regression aCellThatLosesItsPatternGivesItsEntryBack, both passing)
- SessionPlaybackRenderTest: 0
- SessionModelTest: 0
- SessionAudioRecorderTest: 0

`SessionAudioCellsTest` does not exist in this tree (no ctest entry). `SessionAudioRecordTest` was not run.

Not run: the full ctest suite and `tests/run-all-gates.sh`, per the brief. The build used `cmake -S . -B build` then
targeted builds of `zene` and the five suites.

## Caveats

- Red for the three audio cases was measured on the pre-fix tree before any edit. The unit-test red was measured
  by restoring `git show HEAD:src/core/SessionPlayback.cpp` (only the product file), rebuilding, running, then
  restoring the fix and checking with `cmp`.
- No hunk was reverted separately to isolate 390ec761c from 9ce2e115d. Both carry the same `patternId < 0`
  test, and the fix replaces it in both places, so the split does not change the fix.
