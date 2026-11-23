FIXED

# Cluster 3971 - range bounce mutes entire source tracks (round 2)

## Verdict and the two review points

REAL, and both round-1 review findings are now fixed:

1. **Selected automation tracks were muted as a whole**, so their automation outside the bounced
   interval stopped (`ControlBounceSupport.cpp` old whole-mute branch; `ArrangementEdits.cpp:522-528`
   offers every track type to a range, and `TrackContainer.cpp:528-545` skips a muted automation
   track). `muteRangeSources()` no longer mutes automation or hidden-automation tracks as a whole: it
   cuts their clips at the range edges and mutes only the pieces inside
   (`src/core/ControlBounceSupport.cpp:270,321-327`), exactly like audio clips, so automation outside
   `[start,end)` keeps running.
2. **Frozen-take playback ignored clip muting.** A frozen track plays its take instead of its clips
   (`src/tracks/InstrumentTrack.cpp:941-950`, `src/tracks/SampleTrack.cpp:154-160`), so a range bounce
   could leave the take audible over the new bounce, or (when every clip was inside the range) mute the
   track for its whole take window. A frozen source is now handled by rewriting its take:
   `prepareFrozenGap()` (`src/core/ControlBounceSupport.cpp:202`) writes a copy of the take with exactly
   the frames of the range's overlap zeroed (`writeTakeWithSilentWindow()`, `:153`), using the same
   tempo/take-start mapping playback uses (`Track::frozenTakeSourceFrame`, `:227-230`), and
   `installFrozenGap()` (`:257`) installs it with the SAME take window, muted-clip record and
   whole/region flag. The source keeps playing its take outside the range and is silent only inside it.
   Whole takes and region takes are both covered; a region take is gapped only over its intersection
   with the range.

The original cause (whole `Track::setMuted(true)` after rendering only a range) is still fixed by the
round-1 change: audio tracks are muted whole only when every audible clip lies inside the range
(`audibleClipsInside`, `ControlBounceSupport.cpp:141`); otherwise clips are cut at the edges and only
the inside pieces are muted (`muteClipsInRange`, `:270`). Folder and neutral rows keep their whole mute
(no audio of their own), and whole-track bounces (`bounce.land` without ticks) keep the whole-source
mute.

## Callers and siblings

All four range-muting paths call the shared helper and now propagate its typed refusal:

- `range.bounce` - `src/core/ControlCommandsRangeBounce.cpp:96`
- `bounce.land` with `start`/`end` - `src/core/ControlCommandsBounceLand.cpp:87`
- queued `render.queue_bounce` operation `range_bounce` - `src/core/ControlCommandsRenderBounce.cpp:328`
- queued `bounce_land` with `start`/`end` - `src/core/ControlCommandsRenderBounce.cpp:223`

The fallible part (copying a frozen take) runs BEFORE the Track checkpoint and before any model edit, so
a take that cannot be read/written refuses without a landed track or a mute. `bounce.in_place`,
`freeze.*` and `track.flatten` never mute sources and are unchanged.

## Regression tests

`tests/src/core/BounceInPlaceTest.cpp` (the existing bounce test file; it stays under the file-length
ratchet at 490 code lines):

- `rangeBounceKeepsAutomationOutsideTheRangeRunning` (line 497): an automation lane with a clip crossing
  the range start and one after it; asserts the lane is not muted, clip states are
  `96a,192M,384a,` after the bounce, `96a,384a,` after ONE undo, and the redo re-mutes only the inside
  piece.
- `rangeBounceSilencesOnlyItsTicksInAWholeFrozenTake` (line 522): whole take 0..768, range 0..192; asserts
  the source is not muted, the take path changed, its window/whole flag are unchanged, the take WAV is
  digital silence over ticks [0,192) and still non-silent over [192,384), one undo restores the original
  take path and removes the landed track, redo silences it again.
- `rangeBounceSilencesOnlyItsOverlapInARegionFrozenTake` (line 554): region take 0..192, range 96..288;
  asserts only the take's [96,192) overlap is silent, [0,96) still sounds, the region window is
  unchanged, and one undo restores the original take.
- `queuedRangeBounceSilencesOnlyTheSelectedPartOfAFrozenTake` (line 579): the async `range_bounce` path
  through the existing `render.queue_report` seam; take path changes, [192,384) silent, [0,192) audible.
- Round-1 tests still present and passing: `rangeBounceMutesOnlyTheBouncedRegionOfItsSources` (408),
  `bounceLandOfARangeMutesOnlyItsSelection` (444), `queuedRangeBounceMutesOnlyTheBouncedRegion` (458),
  `queuedBounceLandOfARangeMutesOnlyItsSelection` (482).

Commands and EXIT codes (logs in `/home/kruzzzzy/.config/kads/zene/state/runs/3971.r2/logs`):

```
git diff -- <product files> > .../logs/fix.patch                       EXIT 0
git apply -R .../logs/fix.patch                                       EXIT 0
cmake --build build --target BounceInPlaceTest -j4                    EXIT 0  (build-red-2.log)
./BounceInPlaceTest rangeBounceKeepsAutomationOutsideTheRangeRunning \
    rangeBounceSilencesOnlyItsTicksInAWholeFrozenTake \
    rangeBounceSilencesOnlyItsOverlapInARegionFrozenTake \
    queuedRangeBounceSilencesOnlyTheSelectedPartOfAFrozenTake          EXIT 4  (test-red-2.log)
      FAIL rangeBounceKeepsAutomationOutsideTheRangeRunning '!lane->isMuted()' returned FALSE
      FAIL rangeBounceSilencesOnlyItsTicksInAWholeFrozenTake '!source->isMuted()' returned FALSE
      FAIL rangeBounceSilencesOnlyItsOverlapInARegionFrozenTake 'source->frozenTake().path != original' returned FALSE
      FAIL queuedRangeBounceSilencesOnlyTheSelectedPartOfAFrozenTake 'source->frozenTake().path != original' returned FALSE
git apply .../logs/fix.patch                                          EXIT 0
cmake --build build --target zene BounceInPlaceTest -j4               EXIT 0  (build-green-2.log)
./BounceInPlaceTest                                                   EXIT 0  (test-green-2.log, 18 passed)
ctest -R 'BounceInPlaceTest|ControlBounceRenderTest|ControlRenderJobTest|ControlBounceSupportTest|ReversibilityContractTest'
                                                                      EXIT 0  (test-7.log, 5/5 passed)
python3 .../feature_check.py <worktree>                               EXIT 0  (gates-1.log, RESULT: PASS)
```

Round-1 evidence (same helper, ordinary clips) remains in
`/home/kruzzzzy/.config/kads/zene/state/runs/3971.r1/logs`: RED `test-red-5.log` EXIT 1, GREEN
`test-green-5.log` EXIT 0.

## Self-check

- **Regression tests fail with the product change reverted and pass with it:** yes for all four new
  tests - `EXIT 4` with the product hunks reversed (`test-red-2.log`), `EXIT 0` restored
  (`test-green-2.log`); round-1 tests likewise (`test-red-5.log` EXIT 1, `test-green-5.log` EXIT 0).
- **Deterministic:** the sync tests assert model state (clip state strings, muted flags, take window)
  and read exact WAV frame windows; no sleeps or clocks. The queued test uses the existing
  `waitForJob` report-polling seam that every queued-render test uses, with the test binary's own
  synchronous fake child renderer.
- **Fix at the cause and complete:** automation/hidden-automation lanes now use clip-level region
  muting; frozen whole and region takes are gapped with the same mapping playback uses; folder/neutral
  rows and whole-track bounces keep their prior behaviour. All four sync/queued callers were updated
  and checked; `bounce.in_place`/`freeze`/`flatten` are unaffected.
- **No collateral damage:** no serialisation format or model field changed (the take path is existing
  project state; the gapped WAV is a normal take file beside the original), old projects load and
  render as before, and audio tracks whose whole output is inside the range are still muted whole (the
  round-1 tests and `ControlSuccessPaths`' `range.bounce mutes the bounced track` check still pass).
  The only new refusal is a frozen take that cannot be copied - a take that cannot be read cannot be
  region-silenced. No existing limit was tightened.
- **Realtime:** all new code runs in command handlers / apply callbacks on the control or UI thread
  (file copy, `freezeTo` under its existing render-period guard). Nothing was added to `process*`,
  `play*`, `renderNextPeriod` or any per-period path; no control command was added.
- **Undo:** every new test performs exactly one `control.undo` and asserts both the take/clip and the
  landed track are restored; redo replays the same state. A failed take copy happens before the
  checkpoint.
- **ALREADY-FIXED / FALSE-POSITIVE ask:** not applicable; the verdict is FIXED and the pre-fix
  whole-track mute and unmuted take playback are shown by the RED runs above.
- **Sibling commands named by the submit check** (`app.*`, `arrangement.*`, `app.whats_new`,
  `arrangement.fit_tracks`, `arrangement.get_state`): derived by the check's group regex from the
  changed command files; none mutes tracks. Only `control::muteRangeSources` mutes anything, and it is
  called only by `range.bounce` / `bounce.land` (sync and queued); `arrangement.get_state` reports and
  `arrangement.fit_tracks` moves the view. Other `range.*` commands do not mute sources.
- **Gates and completion check:** `feature_check.py` EXIT 0 `RESULT: PASS` (`gates-1.log`);
  `submit_check.py ... 3971` result recorded in `submit-*.log` at submission time.

## Other findings

- `ControlSuccessPaths` fails in this environment only because this build has no MIDI import/export
  filter (`project.import` / `project.export_midi` refused); all `range.bounce`/`bounce.land` checks in
  it pass. `ControlFreezeCommandsTranscript` returns 77 (no loadable instrument) but is registered
  without `SKIP_RETURN_CODE`, so ctest reports it Failed. Both are unrelated to this change.
- A gapped frozen take is a new derived WAV beside the original take. It is referenced by the project
  like any take; repeated bounces of the same range reuse the same file name. Nothing prunes files of
  ranges no longer in use.
