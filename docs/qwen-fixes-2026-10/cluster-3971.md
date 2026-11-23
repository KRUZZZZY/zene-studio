FIXED

# Cluster 3971 - range bounce mutes entire source tracks (round 3: the render side)

## Verdict

REAL. Rounds 1 and 2 already stopped the whole-track mute (clips are cut at the range edges and only the
in-range pieces are muted; automation lanes are region-muted; a frozen take is rewritten with only the overlap
silent - `src/core/ControlBounceSupport.cpp:140-333`, unchanged this round per the diagnostic checkpoint).
What was still wrong was the render: it was the whole track trimmed to `[start,end)`. Once the in-range pieces
are muted, the song loses (a) the sound of those pieces that rings on past `end` (a note sustaining past `end`,
a release tail of a whole-muted track whose clip ends at `end`), and (b) a piece before `start` whose own
ring-out sounds in the range stays audible in the song, so it must not also be baked into the bounce.

## The fix (render side only)

`BounceInPlace::Range::replacesRegion` (`include/BounceInPlace.h`) marks a range bounce that mutes its sources
afterwards. It is set by the four paths that call `muteRangeSources`:
`range.bounce` (`ControlCommandsRangeBounce.cpp`, only with `mute_sources`), `bounce.land` with start/end
(`ControlCommandsBounceLand.cpp`), and the queued `range_bounce` / `bounce_land` (`prepareBounceJob` in
`ControlCommandsRenderBounce.cpp`). `bounce.in_place`, `freeze.*`, `track.flatten` and a whole-track bounce keep
`replacesRegion == false` and render exactly as before.

When set, the shared preparation (`BounceInPlace::prepareRenderTracks`, used by the sync and the queued path):

1. **Cuts and mutes in the serialised session** (`bouncefix::isolateRegionInTrackElement`, new
   `src/core/BounceRegion.cpp`): every unmuted clip of an audio source is split at `start` and `end` the way
   `splitClipAlone` does (right piece keeps its content through `off`), then every clip starting outside
   `[start,end)` is muted, so the render holds only the pieces that `muteRangeSources` will mute. Frozen sources and
   automation lanes are not touched (a frozen source plays its take; automation outside the range must keep
   running in the render, as it does in the song).
2. **Tail** (`tailTicksPastRange`, `audibleFramesIn`): the trim window extends past `end` by up to one bar
   (`MaxTailTicks`), and no further than the next unmuted source clip that starts at or after the range end
   (read from the intact model before any cut, so a clip crossing `end` is not mistaken for the "next" clip). A
   frozen source gets no tail (its take already holds it). The trailing silence of that tail is trimmed
   (`finishPreparedRender`, still free of Engine/Track access) and the longer file lands; `Result.range.end`
   follows the file.
3. The live cuts and mutes stay in `muteRangeSources`, inside its single undo checkpoint, run after the render.

**Deviation from the checkpoint, and why:** the checkpoint asked for `isolateRegion` to run on the live model
before rendering. I applied the same cut to the serialised copy instead. The queued paths snapshot the session at
queue time and re-check a fingerprint of the live tracks when the job completes (`targetStillMatches`); cutting
the live model at queue time would edit the model of a job that may still be cancelled or fail, and would make
the fingerprint check refuse its own job. Cutting the copy gives the render the same pieces, one code path serves
sync and queued, and a failed render leaves nothing to roll back. Undo stays one step (unchanged).

Realtime: nothing here runs on the audio thread (XML edit and WAV scan on the control/job path; the queued scan
runs in `finishPreparedRender` which already runs off the model).

## Tests

New `tests/src/core/RangeBounceRenderSideTest.cpp` (registered in `tests/CMakeLists.txt` and the manifests). The
render is a child process, so the test is its own stand-in `zene render`: it reads the serialised session and draws
0.2 for every unmuted clip of every unmuted audio track plus a 0.1 release tail after each clip end
(`ZENE_FAKE_TAIL_TICKS`). What it proves is therefore the session and the window the product prepares, not the real
engine's voices (unverified here: no real-engine render was run).

- `rangeBounceLandsTheTailOfASoundSustainingPastEnd` (sustains past `end`; one-step undo)
- `rangeBounceDoesNotBounceTheTailOfAPieceBeforeItsStart` (note crossing `start`)
- `rangeBounceOfAWholeMutedSourceLandsItsReleaseTail` (whole-track mute, clip ends at `end`; one-step undo)
- `rangeBounceTailStopsAtTheNextClipAndAtTheCap`
- `rangeBounceWithoutMutingKeepsThePlainWindow`
- `bounceLandOfARangeLandsTheTail` (sync bounce.land)
- `queuedRangeBounceLandsTheTail`, `queuedRangeBounceOfAWholeMutedSourceLandsItsReleaseTail`,
  `queuedBounceLandOfARangeLandsTheTail` (queued paths; undo checked in the first)

**Honest note on coverage:** `rangeBounceDoesNotBounceTheTailOfAPieceBeforeItsStart` PASSES without the fix (the
whole clip is not cut there, so nothing doubles); it guards the combination "cuts without outside-mutes" (which
would give 0.3, not 0.2) but is not one of the tests that fail without the fix. `rangeBounceWithoutMutingKeepsThePlainWindow`
likewise passes without the fix (it guards that the plain window is unchanged).

**One existing test edited, deliberately:** `ControlBounceRenderTest::bounceLandOfARangeLandsAtItsStartAndTrimsTheRender`
asserted `frames < 2 s` for a range of 96 ticks. Its stand-in render is a constant 4 s tone that never decays, so the
whole one-bar tail is audible and the new contract lands 96 + 192 ticks (113400 frames, 2.57 s). I replaced the loose
bound with an exact one (`|frames - round(framesPerTick * (96 + MaxTailTicks))| <= 3` and still `< 4 s`). It is
stricter, but it is an edit to an existing assertion, forced by the requested behaviour change; a reviewer should
look at it.

## Commands and EXIT codes (logs in `/home/kruzzzzy/.config/kads/zene/state/runs/3971.r3/logs`)

```
git diff -- <7 product files + BounceRegion.{h,cpp}> > logs/fix.patch      EXIT 0
git apply -R logs/fix.patch                                              EXIT 0
cmake --build build --target zene RangeBounceRenderSideTest -j4          EXIT 0   (build-red-3.log)
ctest -R RangeBounceRenderSideTest (build/tests)                         EXIT 8   (test-red-3.log)
    Totals: 4 passed, 7 failed; the 7 failures are the tail/replace tests above, e.g.
    rangeBounceLandsTheTailOfASoundSustainingPastEnd 'std::abs(framesIn(path) - framesOf(192)) <= 3' returned FALSE. (37800)
git apply logs/fix.patch                                                 EXIT 0
cmake --build build --target zene RangeBounceRenderSideTest BounceInPlaceTest ControlBounceRenderTest
    ControlRenderJobTest ControlBounceSupportTest FreezeRegionRepairTest -j4   EXIT 0   (build-green-3.log)
ctest -R "^(RangeBounceRenderSideTest|BounceInPlaceTest|ControlBounceRenderTest|ControlRenderJobTest|
    ControlBounceSupportTest|FreezeRegionRepairTest)$" (build/tests)     EXIT 0   (test-green-4.log, 6/6 passed)
```

An earlier GREEN run (`test-green-2.log`, EXIT 8) showed the existing-test failure described above, before I edited
it. A broader filter (`test-green-3.log`, EXIT 8) also listed `BatchFreezeTest`, `MixerBusFreezeTest` and
`MixerBusFreezeMenuTest` as "Not Run" (never built: I did not build them) and `ControlFreezeCommandsTranscript` as
Failed; its output shows only "no audible instrument in this build: the audibility checks cannot run" with no checks
executed. It drives `freeze.*`, which never touches the new path; I did not run it on a base build, so I cannot
state it fails there too.

Gates: see the Self-check section.

## Other findings

- `ControlFreezeCommandsTranscript` exits "Failed" in this build when it has no audible instrument and runs zero
  checks, while its two siblings report "Skipped" (not investigated).

Delegation: Haiku ran every build and ctest run (build-green-1/2/3, build-red-3, test-green-2/3/4, test-red-3) and
reported exit codes and failing assertions; I read the failing assertion lines and the ctest tails, wrote all code
and tests, and ran the patch revert/apply and the manifest edits myself.

## Self-check

- **Fails reverted, passes with the fix:** RED `ctest -R RangeBounceRenderSideTest` with `fix.patch` reverted: EXIT 8, 7 of 11
  failed (`test-red-3.log`); GREEN, patch re-applied, the six-test family: EXIT 0, 6/6 passed (`test-green-4.log`). Two of
  the nine new tests pass without the fix by design (see "Honest note on coverage").
- **Deterministic:** no sleeps or timing as an oracle. The render is the test binary's own `fakeRender` (a pure function of
  the serialised session and `ZENE_FAKE_TAIL_TICKS`, set per test in `init()`); expected sizes are `framesOf(ticks)` from
  `Engine::framesPerTick` with a 3-frame tolerance for rounding. The queued tests poll the existing `render.queue_report`
  seam (`waitForJob`, same as `BounceInPlaceTest`); the 5 ms `qWait` there only yields to the event loop between polls and
  a 15 s limit is a failure bound, not a synchronisation.
- **Cause and completeness:** the cause is that the render ignored which pieces get muted. All four paths into
  `muteRangeSources` set `replacesRegion` (`ControlCommandsRangeBounce.cpp`, `ControlCommandsBounceLand.cpp`,
  `ControlCommandsRenderBounce.cpp` `prepareBounceJob`); the only other callers of `renderTracks`/`renderTrack`/
  `prepareRenderTracks` (`bounce.in_place`, `freeze.track`/`freeze.region` sync and queued/batch, `track.flatten`,
  `FreezeRegionRepairTest`) leave it false and are unchanged. Frozen and automation handling were not touched.
- **No collateral damage:** nothing is saved or loaded differently (the XML edit is on a scratch copy; the project file and
  the model are untouched by the render). `Range` gained a defaulted field; `{start, end}` initialisation still compiles
  (whole build green). The tail is bounded by `MaxTailTicks` and the next unmuted source clip, and the existing
  `trimWav` clamp to the render's own length still applies, so no caller can ask for more than the render has. One
  existing assertion was changed (see Tests).
- **Not ALREADY-FIXED / FALSE-POSITIVE:** verdict is FIXED.
- **Sibling commands:** the changed command files register `range.bounce`, `bounce.land` and `render.queue_bounce`
  (`range_bounce`/`bounce_land` operations); all three are covered above. No command, argument or schema was added or
  changed (the ControlCommandsSnapshot canary is untouched), and the other commands named by the check (`app.*`,
  `arrangement.*`, ...) share a file with these only by registration order and never reach `BounceInPlace`, so they are
  not affected. I did not enumerate the check's truncated list command by command.

## Gate results

`feature_check.py` on the committed tree: EXIT 0, all eight rows PASS (`gates-2.log`). `submit_check.py`: EXIT 0
(`submit-1.log`) before this section was added.
