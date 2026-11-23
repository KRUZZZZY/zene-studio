ALREADY-FIXED

# cluster review-1e1ccd541 - Snapshot pre-fader write races audio read

## Verdict and evidence

`MixerSnapshots::applySends()` does NOT write a send's `preFader` outside the model-change
boundary on this branch. The boundary is present, and it is present on the review base itself
(not only in this branch):

- review base `fa4a5f2e7` (`git show fa4a5f2e785c73e32216931e33cdbe140c06647d:src/core/MixerSnapshots.cpp`):
  line 89 `const auto guard = Engine::audioEngine()->requestChangesGuard();`, line 95
  `(*route)->setPreFader(send.preFader);` - the guard is scoped to the send's write block.
- working tree today, `src/core/MixerSnapshots.cpp:96` (guard) and `:105` (tap write), same scope.
- the fix landed in `2ef8d233ce fix(rework 1e1ccd541): a snapshot recall wrote a send's fader tap
  outside the change boundary`, which `git merge-base --is-ancestor` reports as an ancestor of the
  review base, so it is not even part of this branch's diff.

Both paths the finding names funnel into that one function and are therefore covered:
recall is `src/core/ControlCommandsMixerSnapshots.cpp:161`, and undo/redo of a recall is the pair of
lambdas at `src/core/ControlCommandsMixerSnapshots.cpp:162-163` - both call
`MixerSnapshots::apply()`, which reaches `applySends()`. `ControlRegistry` takes no boundary of its
own, which is why the boundary belongs in `applySends()`; that is exactly where it is.

So the product defect in the finding is closed. What was NOT closed is its proof: the regression test
that shipped with the fix (`MixerConcurrencyTest::recallingASnapshotWaitsForTheRenderPeriodToEnd`)
PASSES with the guard removed. I measured that this session - see "Why the shipped test could not see
the bug" below. This session therefore keeps the product boundary, adds a test seam at the exact
write, and rebuilds the regression test so it fails without the boundary and passes with it.

## Why the shipped test could not see the bug

The shipped test held a render period on the main thread, ran `MixerSnapshots::apply()` on a second
thread, and asked whether the recall got through anyway. Measured, with only the guard hunk reverted
(`logs/existing-fix.patch` applied in reverse, rebuild `EXIT=0`, `logs/test-3-red.log`):

    PASS   : MixerConcurrencyTest::recallingASnapshotWaitsForTheRenderPeriodToEnd()

A diagnostic probe (removed again; `logs/test-4-probe.log`) shows why:

    QDEBUG : registered volume true amount true enabled true
    QDEBUG : volumeEscapes false
    QDEBUG : amountEscapes false
    QDEBUG : preFaderEscapes true

Every model of a mixer channel and of a send is registered with the audio engine, so each
`setValue()` in `applySends()` takes the change lock itself (`AutomatableModel::setValue` ->
`modelChangeGuard`, `src/core/AutomatableModel.cpp:497`). The recall therefore parks at the *first*
model write, long before the tap write, and the held-period observation can never see the tap. The
one write that had no guard of its own (`preFaderEscapes true`) escapes only in the gap between the
last model write's unlock and `setPreFader()` - a gap no timeout-based test can pin down.

## What this session changed

1. `include/MixerSnapshots.h` + `src/core/MixerSnapshots.cpp`: a test-builds-only seam,
   `MixerSnapshots::setTapWriteProbeForTest()`, called on the recalling thread immediately before
   `(*route)->setPreFader(...)`. It is `#ifdef LMMS_TESTING`, a plain function-pointer test plus a
   null check in `applySends()` - it allocates nothing, locks nothing, frees nothing, logs nothing,
   and does not exist in the product build. It is the same shape as the existing
   `AutomatableModel::setValueBufferRampReadProbeForTest` seam.
2. `tests/src/core/MixerConcurrencyTest.cpp`: `recallingASnapshotWaitsForTheRenderPeriodToEnd()`
   now asks, at the seam, whether the recalling thread still holds the change boundary
   (`AudioEngine::isChangeInModelHeld()`, `src/core/AudioEngine.cpp:479`), in addition to the
   held-period observation it already made. That is an ordering fact reported by the writing thread
   itself, not a race and not a deadline.

No behaviour change in the product build: the seam is compiled out, and the guard that the test
targets is untouched.

## Test matrix

| Path | Covered by | Result |
|---|---|---|
| Recall with the render period held (normal path) | `MixerConcurrencyTest::recallingASnapshotWaitsForTheRenderPeriodToEnd` | PASS |
| The tap write itself is inside the change boundary (the defect) | same test, seam assertion | PASS (FAIL without the guard, exit 1) |
| Recall through the real command family (`mixer.snapshot_store` / `recall` / `remove`) and the tap point surviving save+load | `ProjectStateTest::aSnapshotRestoresASendsFaderTap` | PASS |
| Missing channels / missing sends / full store / duplicate names / non-finite floats in a saved snapshot | `ProjectStateTest` snapshot slots | PASS |
| Undo/redo of a recall | same `MixerSnapshots::apply()` (`ControlCommandsMixerSnapshots.cpp:162-163`), reached by the tested function | PASS (covered by construction; no separate slot) |
| Audio-thread constraints | nothing added runs on the audio thread: the seam runs on the control thread and is compiled out of the product | by inspection, see Self-check |
| Old projects | a `<send>` without `prefader` still loads as post-fader (`MixerSnapshots.cpp:163-165`), unchanged this session; `ProjectStateTest` passes | PASS |
| Supported platforms | the seam uses C++20 + a function pointer + `#ifdef LMMS_TESTING`, no Qt-6-only API, no platform code | builds here; other platforms not built in this session |

## Commands and exit codes

All commands were run in
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-review-1e1ccd541`;
logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/review-1e1ccd541.r1/logs`.

Baseline (the shipped fix and the shipped test):

| # | command | log | EXIT |
|---|---|---|---|
| 1 | `cmake --build build --target MixerConcurrencyTest -j4` | `build-1.log` | 0 |
| 2 | `ctest -R MixerConcurrencyTest --output-on-failure` (from `build/tests`) | `test-1-green.log` | 0 |
| 3 | `./MixerConcurrencyTest recallingASnapshotWaitsForTheRenderPeriodToEnd` | `test-2-single-green.log` | 0 |

The shipped test is blind (only the guard hunk reverted):

| # | command | log | EXIT |
|---|---|---|---|
| 4 | `git apply -R .../logs/existing-fix.patch` | - | 0 |
| 5 | `cmake --build build --target MixerConcurrencyTest -j4` | `build-2-red.log` | 0 |
| 6 | `./MixerConcurrencyTest recallingASnapshotWaitsForTheRenderPeriodToEnd` | `test-3-red.log` | **0 - PASSED without the fix** |

Diagnosis probe, then the guard restored:

| # | command | log | EXIT |
|---|---|---|---|
| 7 | `cmake --build build --target MixerConcurrencyTest -j4` / `./MixerConcurrencyTest scratchWhatBlocksMe` | `build-3-probe.log`, `test-4-probe.log` | 0 |
| 8 | `git apply .../logs/existing-fix.patch` (restore) | - | 0 |

RED / GREEN with the new test (step 4b). `logs/fix.patch` is the guard hunk of the product change
under test (a copy of `logs/existing-fix.patch`); reverting the seam as well would only make the test
fail to compile, which proves nothing, so the harness stays in place for both runs:

| # | command | log | EXIT |
|---|---|---|---|
| 9 | `cmake --build build --target MixerConcurrencyTest -j4` (guard in place) | `build-4-green.log` | 0 |
| 10 | `./MixerConcurrencyTest recallingASnapshotWaitsForTheRenderPeriodToEnd` | `test-5-single-green.log` | 0 |
| 11 | `git apply -R .../logs/fix.patch` | - | 0 |
| 12 | `cmake --build build --target MixerConcurrencyTest -j4` | `build-5-red.log` | 0 |
| 13 | `./MixerConcurrencyTest recallingASnapshotWaitsForTheRenderPeriodToEnd` | `test-6-red.log` | **1** |
| 14 | `git apply .../logs/fix.patch` | - | 0 |
| 15 | `cmake --build build --target MixerConcurrencyTest -j4` | `build-6-green.log` | 0 |
| 16 | `./MixerConcurrencyTest recallingASnapshotWaitsForTheRenderPeriodToEnd` | `test-7-green.log` | 0 |

The RED run fails on exactly the new assertion:

    FAIL!  : MixerConcurrencyTest::recallingASnapshotWaitsForTheRenderPeriodToEnd()
             'probe.boundaryHeld.load()' returned FALSE.
             (a snapshot recall wrote a send's fader tap outside the change boundary)
       Loc: [../../tests/src/core/MixerConcurrencyTest.cpp(781)]

Family and gates:

| # | command | log | EXIT |
|---|---|---|---|
| 17 | `cmake --build build --target zene MixerConcurrencyTest ProjectStateTest MixerRoutingBackwardCompatTest MixerAbRegressionTest -j4` | `build-7-family.log` | 0 |
| 18 | `ctest -R "MixerConcurrencyTest\|ProjectStateTest\|MixerRoutingBackwardCompatTest\|MixerAbRegressionTest" --output-on-failure` | `test-8-family.log` | 0 (4/4 passed) |
| 19 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `gates-1.log` | 0 |

`gates-1.log` (unpiped exit 0):

    PASS  fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace
    PASS  no-tautology / doc-figures / complexity / file-length
    RESULT: PASS

## Self-check

**The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
Reverting the guard hunk (`git apply -R logs/fix.patch`, EXIT 0), rebuilding (`build-5-red.log`,
EXIT 0) and running the test gives EXIT 1 with `'probe.boundaryHeld.load()' returned FALSE`
(`test-6-red.log`). Re-applying (`git apply logs/fix.patch`, EXIT 0), rebuilding (`build-6-green.log`,
EXIT 0) and running gives EXIT 0 (`test-7-green.log`).

**The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected
values it uses).** It uses the injected seam `MixerSnapshots::setTapWriteProbeForTest()`: the probe
runs on the recalling thread at the exact write and stores `AudioEngine::isChangeInModelHeld()` (a
thread-local, so it answers a question about the calling thread, not about the machine). The test
reads that flag after joining the thread, so the ordering is fixed at the moment of the write. The
`escapesTheRenderPeriod()` helper's 300 ms deadline is only a guard against a hang; the verdict does
not depend on it - the RED failure is the seam flag, and it fails in about 0.8 s.

**The fix is at the cause and complete: every caller and sibling checked.** Callers of
`applySends()`: `MixerSnapshots::apply()` only, reached by `mixer.snapshot_recall`
(`ControlCommandsMixerSnapshots.cpp:161`) and by the recall's own undo and redo
(`ControlCommandsMixerSnapshots.cpp:162-163`) - one guarded write covers all three. Siblings that
write `MixerRoute::preFader()`: `src/core/Mixer.cpp:1664` (guarded, cluster 5157),
`src/core/Mixer.cpp:3055` (project load, before playback starts - the documented contract at
`include/Mixer.h:552`), and two unchecked sites reported under "Other findings" below (they are other
findings' files, not this one's; nothing I changed makes them worse or better).

**No collateral damage: old projects and saved data still load as before.** The seam is
`#ifdef LMMS_TESTING` only - the product build compiles it out entirely and keeps the same writes.
No XML, no stored field and no default changed (`channelFrom()` and `channelElement()` are untouched).
`ProjectStateTest` (save, load, duplicate names, non-finite floats, tap point round trip through
`mixer.snapshot_store/recall/remove`) is green at EXIT 0.

**If your verdict is ALREADY-FIXED: the code (file:line) or the intended contract that shows it.**
`src/core/MixerSnapshots.cpp:96` takes
`const auto guard = Engine::audioEngine()->requestChangesGuard();` before the send's writes and
`:105` writes `(*route)->setPreFader(send.preFader);` inside that guard's scope; the same lines are
already in the review base `fa4a5f2e7` (line 89 / line 95 of the same file) via
`2ef8d233ce`. The contract is the one `Mixer::createChannelSend()` implements for an existing route
(`src/core/Mixer.cpp:1658-1665`: lock, write `amount` and `preFader`, unlock) and the
`AutomatableModel` rule that a render-visible model write needs the audio change lock
(`src/core/AutomatableModel.cpp:67-78`).

## Other findings

Not fixed here: they are other files and other findings, listed with evidence so a lane that owns
them can act.

1. `src/core/ControlMixerSupport.cpp:373` - the undo lambda `recordMixerRouteStep()` installs writes
   `route->setPreFader(preFader)` after `route->amount()->setValue(amount)` with no change boundary of
   its own; its redo half goes through `Mixer::createChannelSend()`, which is guarded. Same field,
   same gap: `ProjectJournal::undo()` (`src/core/ProjectJournal.cpp:136`) runs the recorded lambda
   with no guard around it.
2. `src/core/ControlCommandsMixerTools.cpp:163` - `buildReturn()` writes `route->setPreFader(preFader)`
   immediately after a `createChannelSend()` call whose own boundary has already been released.
3. The earlier lane's regression test for this cluster was not sensitive to the fix (evidence above);
   its commit message claims a red run of exit 8 that I could not reproduce on this branch. Worth a
   recheck of other tests written with the same held-period-only pattern, e.g.
   `MixerConcurrencyTest::updatingAnExistingRouteWaitsForTheRenderPeriodToEnd()`, whose
   `createChannelSend()` path also blocks on the route models' own guards first.
