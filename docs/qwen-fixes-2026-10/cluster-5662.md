ALREADY-FIXED

# Cluster 5662 — Region freeze silences clip tails beyond the frozen region

## Finding

The finding (against an older revision of `src/core/ControlCommandsFreeze.cpp`)
claimed that the region freeze muted every clip starting inside the region *in
full*, so a clip crossing the region's end had its tail silenced even though the
frozen take stops at the region boundary. It asked for the crossing clips to be
split at both edges.

## Decision: ALREADY-FIXED on this branch

The branch already carries commit `a91d7317ce` ("feat(freeze): region freeze
silences the source only inside the region, follows the tempo map, recovers
from a moved take (Q050-008)"), and that commit is an ancestor of the lane's
`FIX-BASE` (`050/m0-train-35` = `fa4a5f2e785c73e32216931e33cdbe140c06647d`):

```
git merge-base --is-ancestor a91d7317ce fa4a5f2e785c73e32216931e33cdbe140c06647d
EXIT=0
```

The old helper the finding quoted (`clipsInside`, which recorded a whole clip's
length for every clip whose start was inside the region) no longer exists. The
current code:

- `src/core/ArrangementEdits.cpp:114-126` — `isolateRegion()` first calls
  `splitUnmutedAt(track, start, ...)` (`:117`) **and**
  `splitUnmutedAt(track, end, ...)` (`:118`), then returns only the pieces whose
  start is in `[start, end)` (`:124`). A clip crossing `end` is cut at `end`, so
  the piece inside is muted and the tail (starting at `end`) is left alone. A
  clip crossing `start` is cut at `start`, so only the inside piece is muted.
- `include/ArrangementEdits.h:60-66` — the contract: "split every unmuted clip
  ... that crosses @a start or @a end at that tick, then return the unmuted
  clips that now lie inside [@a start, @a end)".
- `src/core/ControlCommandsFreeze.cpp:77-92` — `control::isolateFreezeRegion()`
  delegates to `arrangement::isolateRegion()`; `:220` uses it for
  `freeze.region`, and `:224-225` passes the take window
  `[range.start, rendered.range.end)` plus the muted pieces to
  `Track::freezeTo`.
- `src/core/Track.cpp:1392-1398` mutes exactly the recorded pieces;
  `Track::unfreeze()` (`:1424-1429`) unmutes exactly those pieces back.
- The sibling path `render.queue_bounce`'s freeze
  (`src/core/ControlCommandsRenderBounce.cpp:199-201`) calls the same
  `isolateFreezeRegion()` helper, so it has the same fixed behaviour.

There is no whole-clip mute of a clip that starts inside the region any more.

## Regression test

`tests/src/core/FreezeRegionRepairTest.cpp:149-188`
(`overlappingClipIsSilentOnlyInsideTheRegion`) is the regression test added by
the fix. Its fixture clip crosses both region edges; it asserts one cut per edge
(`cut.size() == 2`, `:163`), then renders periods and asserts the source is
audible before the region, silent inside it, and **audible again after it**
(`:187`, `after > 0.01`). It is deterministic: it uses the existing
`AudioEngineWorkerThread::setDeterministicProcessing(true)` seam and
`renderNextPeriod()` tick positions, with no wall-clock timing or sleeps.

I verified in this session that the test actually catches the reported defect by
temporarily removing the end split (`splitUnmutedAt(track, end, split);`) from
the working tree only, then restoring it. Nothing was committed from that
temporary revert.

## Commands and exit codes (this session)

All logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/5662.r1/logs`.

1. Build the existing regression target (unchanged tree):

```
cmake --build build --target FreezeRegionRepairTest -j4 > .../logs/build-1.log 2>&1
EXIT=0
```

2. Run it on the current (fixed) code:

```
cd build/tests && ctest -R '^FreezeRegionRepairTest$' --output-on-failure > .../logs/test-1.log 2>&1
EXIT=0
1/1 Test #164: FreezeRegionRepairTest ...........   Passed    1.48 sec
```

3. RED proof — remove the historical end split from the worktree, build, run:

```
git diff -- src/core/ArrangementEdits.cpp > .../logs/red-revert.patch
EXIT=0
cmake --build build --target FreezeRegionRepairTest -j4 > .../logs/build-2.log 2>&1
EXIT=0
cd build/tests && ctest -R '^FreezeRegionRepairTest$' --output-on-failure > .../logs/test-2-red.log 2>&1
EXIT=8
FAIL!  : FreezeRegionRepairTest::overlappingClipIsSilentOnlyInsideTheRegion() Compared values are not the same
Totals: 12 passed, 1 failed, 0 skipped, 0 blacklisted, 1444ms
```

4. Restore the code and rerun (GREEN):

```
git apply -R .../logs/red-revert.patch
EXIT=0
cmake --build build --target FreezeRegionRepairTest -j4 > .../logs/build-3.log 2>&1
EXIT=0
cd build/tests && ctest -R '^FreezeRegionRepairTest$' --output-on-failure > .../logs/test-3-green.log 2>&1
EXIT=0
1/1 Test #164: FreezeRegionRepairTest ...........   Passed    1.51 sec
```

5. Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py \
  /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-5662 \
  > .../logs/gates-1.log 2>&1
EXIT=0
  PASS  fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace / no-tautology / doc-figures
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

`git status --porcelain` is empty after restoring the temporary edit.

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with
  it (exit codes).** This lane's product code is unchanged; the fix is already in
  the base. I reverted the fix's one discriminating line
  (`splitUnmutedAt(track, end, split);`, `src/core/ArrangementEdits.cpp:118`) in
  the worktree only: `ctest` exited **8** with
  `overlappingClipIsSilentOnlyInsideTheRegion` failing. Restored:
  `ctest` exited **0**. See `test-2-red.log`, `test-3-green.log`,
  `red-revert.patch`.
- **The test is deterministic.** It uses
  `AudioEngineWorkerThread::setDeterministicProcessing(true)` in
  `initTestCase` and drives `AudioEngine::renderNextPeriod()` by tick position;
  there are no sleeps, timers or wall-clock thresholds. The only numeric
  thresholds are RMS levels (`> 0.01` for audible, `< 1e-4` for silence) over
  rendered periods.
- **The fix is at the cause and complete.** `isolateRegion()` splits at both
  region edges and mutes only pieces starting inside; every caller was checked:
  `freeze.region` (`ControlCommandsFreeze.cpp:220-225`) and the
  `render.queue_bounce` freeze path
  (`ControlCommandsRenderBounce.cpp:199-201`) both use the same helper. The
  muted-piece record is what `Track::freezeTo` mutes and `Track::unfreeze`
  unmutes, so undo remains one track checkpoint.
- **No collateral damage.** No product code or data format was changed by this
  lane. A project saved before the fix carries the same `mutedClips` records as
  after it; clips completely inside the region and whole-track freezes behave
  exactly as before (`regionFlagRoundTripsAndWholeTrackTakeIsUnchanged`), and no
  limit or check was added.
- **ALREADY-FIXED evidence.** `src/core/ArrangementEdits.cpp:117-118` (split at
  both edges), `:124` (only pieces starting inside are returned),
  `src/core/ControlCommandsFreeze.cpp:77-92` and `:220-225` (command uses it),
  `tests/src/core/FreezeRegionRepairTest.cpp:149-188` (regression test),
  introduced by `a91d7317ce`, which is an ancestor of the lane base (exit 0).
- **Sibling commands in the command files changed.** No command file was changed
  by this lane (`git status` clean, and the only commit is this report). The
  sibling list printed by `submit_check.py` comes from the train's
  `base..HEAD` merge history, not from an edit here. Within the affected family,
  `freeze.track` mutes nothing by design, `freeze.region` and
  `render.queue_bounce`'s freeze share `isolateFreezeRegion()`, and
  `freeze.unfreeze` restores exactly the recorded muted pieces; all three are
  consistent with the existing split-at-both-edges behaviour.

## Other findings

None.
