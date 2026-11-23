FALSE-POSITIVE

# cluster new-p16-88 - "Frozen takes silence source audio for the whole buffer based only on its start"

Decision: **FALSE-POSITIVE**. No product code was changed.

The finding assumes `Track::play()` receives a buffer that can cross a region take's start or end tick.
It cannot. The arrangement engine calls it once per tick, with at most the frames of that tick, so
`frozenTakeSilencesSource(_start.getTicks())` is evaluated exactly for the frames the pass schedules.

## Code evidence

- `Track::frozenTakeSilencesSource(tick_t position)` is documented as "whether the take, not the source,
  owns **the pass starting at position**" (`include/Track.h:245-248`) and its region test is
  `position >= startTicks && (endTicks <= startTicks || position < endTicks)`
  (`src/core/Track.cpp:1519-1523`). `playFrozenTake()` uses the same half-open window
  (`src/core/Track.cpp:1548-1558`), so the take voice and the source decision agree.
- Take windows are whole ticks: `FrozenTake::startTicks/endTicks` are `tick_t`
  (`include/Track.h:218-219`), parsed as ticks on load (`src/core/Track.cpp:1458-1466`), and a region
  window must end past its start (`Track::isValidRegionWindow`, `include/Track.h:277`).
- The only arrangement caller is `Song::processNextBuffer()`:
  `framesToPlay = min(framesUntilNextPeriod, ceil(framesPerTick - frameOffsetInTick))`
  (`src/core/Song.cpp:632-636`) and `track->play(getPlayPos(), framesToPlay, ...)` is called only
  when `static_cast<f_cnt_t>(frameOffsetInTick) == 0` (`src/core/Song.cpp:648-687`). A pass therefore
  starts at a tick boundary and its frames lie inside that tick (the residual `frameOffsetInTick` is
  < 1 frame, and `ceil(framesPerTick - residual)` never reaches the next tick's first frame), so a
  pass can never overlap a take boundary.
- The other `Track::play` caller, `PatternStore::play()` (`src/core/PatternStore.cpp:48-62`), always
  passes `clipNum >= 0` (pattern editor and session paths, `src/tracks/PatternTrack.cpp:126`,
  `src/core/Song.cpp:682`), and both frozen branches are gated on `_clip_num < 0`
  (`src/tracks/InstrumentTrack.cpp:941`, `src/tracks/SampleTrack.cpp:154`). No other caller exists
  (`rg -n "->play\\(" src include`).
- A region freeze additionally cuts unmuted clips at both edges and mutes the pieces inside
  (`arrangement::isolateRegion`, `src/core/ArrangementEdits.cpp:100-125`), so the source pieces begin
  and end exactly at the take's ticks.
- The engine-level acceptance test for this path passes:
  `FreezeRegionRepairTest::overlappingClipIsSilentOnlyInsideTheRegion` drives real `Song` playback
  through the engine and requires the source silent inside the region and audible on both sides
  (`tests/src/core/FreezeRegionRepairTest.cpp:147-188`).

## What I ran

| Command | EXIT | Log |
| --- | --- | --- |
| `cmake --build build --target FreezeRegionRepairTest -j4` | 0 | `logs/build-1-baseline.log` |
| `cd build/tests && ctest -R '^FreezeRegionRepairTest$' --output-on-failure` | 0 | `logs/test-1-baseline.log` |
| `cmake --build build --target FreezeRegionRepairTest FrozenTakeRoundTwoTest FrozenTakeHandoffTest -j4` | 0 | `logs/build-10-restore.log` |
| `cd build/tests && ctest -R '^(FreezeRegionRepairTest\|FrozenTakeRoundTwoTest\|FrozenTakeHandoffTest)$' --output-on-failure` | 0 | `logs/test-10-family.log` |
| `python3 .../qwen/feature_check.py <worktree>` | 0 | `logs/feature_check.log` |

`test-10-family.log`: `100% tests passed, 0 tests failed out of 3` (FrozenTakeHandoffTest,
FrozenTakeRoundTwoTest, FreezeRegionRepairTest).

## Scratch probe (reverted; not part of the tree)

To check the finding's two symptoms on real playback I added a temporary slot to
`FreezeRegionRepairTest` (a silent take over `[60,120)` at 35 BPM / 393.75 frames per tick, take
length 23625 frames; the source tone is audible outside). The probe diff is saved as
`logs/probe-scratch.diff`; the test file was restored with `git checkout --` afterwards, so the
worktree carries no test change.

Result (`logs/test-8a-probe-frozen.log`, `logs/test-9-probe-70.log`):

- Right edge: the source resumes at stream frame 47475 for `[60,120)` and 51413 for `[70,130)`.
  These are the take-end frames (47250 and 51187.5) plus the engine's constant 225-frame output
  offset measured at the first tone frame (226): exact to within one frame. **Source after the take
  is not missing.**
- Left edge: the silent window starts at 24064 (`[60,120)`) and 27904 (`[70,130)`), 182 / 84 frames
  after the source's cut end. That residual is at most one output period and is *not* the pass
  decision: `SamplePlayHandle::play()` renders a whole chunk and only checks
  `framesDone() >= totalFrames()` before the next one (`src/core/SamplePlayHandle.cpp:544-551`,
  `755-788`), so any clip end (including every unmuted piece before a cut) can spill into the next
  period. It is listed under Other findings and was not fixed here.

The probe's own `abs(silent_run - take_length) <= 2` assertion failed for that reason
(`test-2-probe.log`, EXIT=1, 23442 vs 23625); the probe was scaffolding only and has been removed.

## Why this is not the reported defect

The reported trigger needs a pass that starts before the take and overlaps it. `Song` never issues
one (per-tick pass above); the observed left-edge tail exists with any clip end and would remain if
the suppression were split at take boundaries, because it comes from an already-queued sample handle,
not from `frozenTakeSilencesSource`. The reported second half (source missing after the take end)
does not occur: the take-end pass is not silenced and the source resumes on the tick.

## Self-check

- **Regression test red/green with the product change reverted:** not applicable - claim-only
  closure with no product or test change (the check accepts this for FALSE-POSITIVE). The existing
  real-engine tests were rebuilt and run this session: `logs/test-10-family.log`, EXIT=0.
- **The test is deterministic:** no test was added. The scratch probe used fixed tempo, fixed
  frames and engine-driven periods only; no wall clock, sleep or thread. It is reverted.
- **Fix at the cause and complete (all callers/siblings):** no fix. Every caller of `Track::play`
  was checked: `Song.cpp:687` (per-tick arrangement pass), `PatternStore.cpp:62` (only with
  `clipNum >= 0`, excluded by `_clip_num < 0` in both track types), plus test code. `InstrumentTrack`
  and `SampleTrack` have identical take-window branches (`InstrumentTrack.cpp:941-951`,
  `SampleTrack.cpp:154-160`), and `playFrozenTake` uses the same window (`Track.cpp:1548-1558`).
- **No collateral damage:** no product or data change; old projects are untouched. The three
  freeze/take test executables pass (EXIT=0).
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** the code and contract above
  (`Song.cpp:632-636,648-687`; `Track.h:245-248`; `Track.cpp:1519-1523,1548-1558`;
  `InstrumentTrack.cpp:941-951`; `SampleTrack.cpp:154-160`).
- **Undo/commands/realtime:** nothing changed; no command ids added, no control-command snapshot
  change.

## Other findings

- **Sample-handle end spills into the rest of its buffer.** `SamplePlayHandle::play()` renders the
  whole chunk from the sample and only stops at the next period (`src/core/SamplePlayHandle.cpp:544-551`),
  so up to one buffer of audio can sound past a clip's window end. At a region take's start this is
  audible as a short source tail under the take (measured 84-182 frames at 393.75 frames per tick).
  It is independent of the frozen-take pass decision and affects every clip end; not fixed in this
  cluster.
