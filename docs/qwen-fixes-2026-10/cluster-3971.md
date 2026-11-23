FIXED

# Cluster 3971 - range bounce mutes entire source tracks

## Verdict and cause

The report is REAL. `range.bounce` rendered only the selected ticks but then called
`Track::setMuted(true)` on every source track, so all audio those tracks had before or after the
selection disappeared from the mix.

Before the fix (historical `src/core/ControlCommandsRangeBounce.cpp:71-74`, same code in
`src/core/ControlCommandsBounceLand.cpp:98` and the queued copies at
`src/core/ControlCommandsRenderBounce.cpp:239,301`):

```cpp
for (const int id : l->sources)
{
    Track* source = trackWithId(id);
    if (source != nullptr && l->muteSources) { source->setMuted(true); }
}
```

The bounce file covers `[range.start, range.end)`, so muting the whole track removed audio the
bounce does not replace.

## Fix

One shared helper, `control::muteRangeSources()` (`src/core/ControlBounceSupport.cpp:149`, declared at
`include/ControlBounceSupport.h:68`), mutes only the bounced region:

- `audibleClipsInside()` (`src/core/ControlBounceSupport.cpp:137`) is the safe whole-track case: if every
  audible clip lies inside the range, the bounce replaces the track's entire output and the track is
  muted as a whole (this keeps the existing behaviour and tests).
- Otherwise `arrangement::isolateRegion()` (`src/core/ControlBounceSupport.cpp:168`, the same product
  primitive `freeze.region` uses) cuts the clips that cross the range's edges at those edges, and only
  the pieces starting inside the range are muted (`:169-174`). Audio before and after the selection keeps
  playing.
- A track that carries no audio of its own (automation/folder/neutral) is muted whole exactly as before:
  the bounce holds no automation to replace it with, so nothing about it changes.
- The tracks' pre-edit state is recorded as ONE Track checkpoint before any cut or mute
  (`src/core/ControlBounceSupport.cpp:157`). The registry (sync) and `RenderQueue::closeBounceEdit`
  (async) merge it with the landing action into one undo step. Undo restores the uncut clips and the
  mutes; redo replays the cuts/mutes from the step's captured object state.

Callers and siblings kept consistent:

- `range.bounce` - `src/core/ControlCommandsRangeBounce.cpp:96` (the finding's command).
- `bounce.land` with `start`/`end` - `src/core/ControlCommandsBounceLand.cpp:103` (whole-track
  `bounce.land` still mutes whole at `:105`).
- queued `range_bounce` - `src/core/ControlCommandsRenderBounce.cpp:321`.
- queued `bounce_land` with `start`/`end` - `src/core/ControlCommandsRenderBounce.cpp:246`.

No command was added or removed, so the commands snapshot/canary is unchanged. No serialisation
changes: split clips and clip mutes are already the product's saved state, so old projects load and
render as before. The code runs in command handlers on the control/UI thread only; nothing was added to
an audio-thread function.

## Regression test

`tests/src/core/BounceInPlaceTest.cpp` (the existing bounce test file; the file-length ratchet was not
re-anchored, the tests were placed here to keep `ControlBounceRenderTest.cpp` and
`ControlRenderJobTest.cpp` under their limit):

- `rangeBounceMutesOnlyTheBouncedRegionOfItsSources` (line 355): a track with clips at 0, 192 and 384
  and a clip crossing the range's start at 144; range 192..384. Asserts the tracks are not muted, the
  clip states are `0a,192M,384a,` and `144a,192M,` (a = audible, M = muted), the landed clip is at
  192, ONE undo returns `0a,192a,384a,` / `144a,`, and redo reapplies the same region mutes.
- `bounceLandOfARangeMutesOnlyItsSelection` (line 391): the sync `bounce.land` sibling.
- `queuedRangeBounceMutesOnlyTheBouncedRegion` (line 405): the `render.queue_bounce` sibling, through
  the queue's existing `render.queue_report` seam.
- `queuedBounceLandOfARangeMutesOnlyItsSelection` (line 429): the queued `bounce_land` sibling.

Commands and EXIT codes (logs in `/home/kruzzzzy/.config/kads/zene/state/runs/3971.r1/logs`):

```
git diff -- <product files> > .../logs/fix.patch                 EXIT 0
git apply -R .../logs/fix.patch                                 EXIT 0
cmake --build build --target BounceInPlaceTest -j4              EXIT 0   (build-red-4.log)
./BounceInPlaceTest                                             EXIT 4   (test-red-4.log)
   FAIL! rangeBounceMutesOnlyTheBouncedRegionOfItsSources '!padded->isMuted() && !crossing->isMuted()' returned FALSE
   FAIL! bounceLandOfARangeMutesOnlyItsSelection '!source->isMuted()' returned FALSE
   FAIL! queuedRangeBounceMutesOnlyTheBouncedRegion '!track->isMuted()' returned FALSE
   FAIL! queuedBounceLandOfARangeMutesOnlyItsSelection '!source->isMuted()' returned FALSE
git apply .../logs/fix.patch                                    EXIT 0
cmake --build build --target zene BounceInPlaceTest -j4         EXIT 0   (build-green-4.log)
./BounceInPlaceTest                                             EXIT 0   (test-green-4.log, 14 passed)
ctest -R 'BounceInPlaceTest|ControlBounceRenderTest|ControlRenderJobTest|ControlBounceSupportTest|ReversibilityContractTest'
                                                                EXIT 0   (test-12.log, 5/5 passed)
python3 .../feature_check.py <worktree>                         EXIT 0   (gates-3.log, RESULT: PASS)
```

The manifest's single named test was also replayed alone, from the final tree, with the product
patch reversed and restored:

```
git apply -R .../logs/fix.patch                                 EXIT 0
cmake --build build --target BounceInPlaceTest -j4              EXIT 0   (build-red-5.log)
./BounceInPlaceTest rangeBounceMutesOnlyTheBouncedRegionOfItsSources
                                                                EXIT 1   (test-red-5.log)
git apply .../logs/fix.patch                                    EXIT 0
cmake --build build --target zene BounceInPlaceTest -j4         EXIT 0   (build-green-5.log)
./BounceInPlaceTest rangeBounceMutesOnlyTheBouncedRegionOfItsSources
                                                                EXIT 0   (test-green-5.log)
```

The sync tests are fully deterministic (model assertions, no clock). The queued tests use the existing
`waitForJob` seam (`render.queue_report` polling) that every queued-render test uses; the only
asynchronous part is the fake child renderer, which is the test binary itself answering `render`.

## Self-check

- **Regression test fails with the product change reverted and passes with it:** yes, `EXIT 4` ->
  `EXIT 0` above, from `test-red-4.log` / `test-green-4.log`.
- **Deterministic:** the model assertions inject no timestamps and use no sleeps; the queued legs use
  the established `waitForJob` report-polling seam and the fake render answers the CLI synchronously.
- **Fix at the cause, every caller and sibling checked:** `range.bounce`, `bounce.land` (sync), queued
  `range_bounce` and queued `bounce_land` all go through `muteRangeSources`; whole-track bounces keep
  the whole-track mute. `bounce.in_place`, `freeze.*` and `track.flatten` never mute sources, so they
  are unaffected. Sibling `range.*` commands (set/split/delete/copy/...) do not mute tracks at all;
  `bounce.*`/`render.*` siblings were checked by the grep of `setMuted(true)` in `src/core`, which now
  only shows the intentional whole-track branches.
- **No collateral damage:** no serialisation change; a whole-track range (every audible clip inside)
  still mutes the track, so `ControlSuccessPaths`' `range.bounce mutes the bounced track` check still
  passes (I ran it: `range.bounce` checks `ok`; the test's two unrelated failures are missing MIDI
  filters in this build). No new limit or refusal was added.
- **Realtime:** changed code is in command handlers and a shared helper called only from them; no
  allocation, lock, I/O or signal was added to `process*`/`play*`/render code. No control command was
  added.
- **Undo:** one `control.undo` restores the cuts and the mutes in all four tested paths (asserted).
- **Sibling commands named by the submit check** (`app.about`, `app.ask`, ... `app.whats_new`,
  `arrangement.fit_tracks`, `arrangement.get_state`): the check derives them from the groups its regex
  finds in the changed command files; none of them mutes tracks. The only muting path in this change is
  `control::muteRangeSources`, called by `range.bounce` and `bounce.land` (sync) and their queued
  operations; `arrangement.get_state` only reports and `arrangement.fit_tracks` only moves the view.
  A grep for `setMuted(true)` in `src/core/*.cpp` after the change shows only the intentional
  whole-track branches (`ControlCommandsBounceLand.cpp:100,105`,
  `ControlCommandsRenderBounce.cpp:243,247`) and the helper at `ControlBounceSupport.cpp:165`.
- **Gates and pre-submit check:** `python3 .../feature_check.py <worktree>` EXIT 0 with
  `RESULT: PASS` (`gates-3.log`); `python3 .../submit_check.py <worktree> 3971` EXIT 0 with
  `RESULT: PASS - submit` (`submit-1.log`).
- **ALREADY-FIXED / FALSE-POSITIVE ask:** not applicable - the verdict is FIXED from the real
  whole-track mute in the pre-fix code shown above (`src/core/ControlCommandsRangeBounce.cpp:71-74`),
  and the RED run in `test-red-5.log` fails on exactly that mute.

## Other findings

- A frozen source track's take cannot be muted by region with the clip mechanism used here (the take
  plays instead of the clips). For such a track the region mute leaves the take audible inside the
  range, while keeping audio outside it; a proper fix needs a region-silent take, which this command
  family does not have. Not fixed here.
- `ControlFreezeCommandsTranscript` returns 77 in this environment (no loadable instrument) but is
  registered without `SKIP_RETURN_CODE`, so ctest reports it Failed; unrelated to this change.
- `ControlSuccessPaths` fails in this environment because this build has no MIDI import/export filter
  (`project.import`/`project.export_midi` refused); its `range.bounce` checks pass. Unrelated.
