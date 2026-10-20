FIXED (port of accepted `b8a352ffe`; cluster new-p9-576-port, branch `bugs/qwen-new-p9-576`)

# Cluster new-p9-576-port

The finding is REAL on this branch: `AutomationLatency::refreshClips()` walked every automation clip
and published each clip's new latency one atomic store at a time while compensation stayed enabled
(pre-fix `src/core/AutomationLatency.cpp:177-182`), so an audio read overlapping a refresh could mix
one clip's new latency with another's old one. The sibling-branch fix (double-buffered latency banks
pinned by a `ReadSnapshot`) had never landed here, and this line had since replaced the
`automatedValuesAt` audio path with the `AutomationRenderScratch`-based `fillAutomatedValues` API,
so the snapshot had to be threaded through that API instead. Round 1 of this port ended rc=1 with no
commits; this line of work re-applies the accepted test, re-implements the accepted fix against the
current API, and completes the full procedure with a committed report. The round-2 review (Luna)
required the audio-side snapshot acquisition to be strictly bounded and non-waiting; `readSnapshot()`
now registers at most twice and otherwise returns an unpinned fallback (see "The fix"), and every
command below is recorded with its exact path and exit code.

## What was wrong

`refresh()` (250 ms poll while compensation is on, and the `automation.latency_compensation`
command's refresh) updated each clip's latency frames one clip at a time. The audio thread read each clip's
atomic independently inside `readTime`/`shiftBlock` (per clip, and once per tick of a period). A
plugin latency or channel PDC change while several clips were active therefore let a single period
read clip A with the new latency and clip B with the old one, shifting their reads to different
times. The regression proves it deterministically: with refresh paused mid-walk (a test effect
blocks inside its `latencyFrames()`), the pre-fix tree reported `firstDuring: 0.2` for the clip that
had already been republished while its neighbour still had the old value (`firstBefore: 0.25`).

## The fix (ported and adapted)

- `src/core/AutomationLatency.cpp:187` (`readSnapshot()`), `:250` (`refreshClips`) and
  `include/AutomationLatency.h` (accepted design, applies verbatim): each clip stores two latency
  banks (`include/AutomationClip.h`, `m_latencyFrames[2]`); `refreshClips()` fills the bank no
  reader holds and publishes it with one `m_state` store (generation bits prevent ABA); the audio
  side pins a bank with a lock-free, allocation-free reader count and reads every clip through that
  pinned `ReadSnapshot`. While compensation is off nothing is published differently than before
  (every bank entry is 0), so older projects still render bit for bit.
- **Strictly bounded, non-waiting audio-side acquisition (round-2 review).** `readSnapshot()`
  (`src/core/AutomationLatency.cpp:187`) registers on the published bank and validates that the
  publication did not change while registering; a publication that raced the registration is retried
  at most `SnapshotAttempts` (= 2, `include/AutomationLatency.h:124`) times. After that it returns
  an unpinned fallback (`owner == nullptr`, `state = 0`): `enabled()` is false and every
  `latencyFrames()` is 0, and the fallback never dereferences a bank - the whole block is read
  uncompensated, coherently, for every clip. Safe bank reuse is preserved: the writer rewrites a bank
  only after observing its reader count as 0 (`refreshClips`, `src/core/AutomationLatency.cpp:254`),
  so an unpinned reader cannot be reading a bank being rewritten. All waiting or skipped-publication
  handling lives on the control-thread writer: `refreshClips` yields only when `waitForFreeBank`
  (enable and project load, control thread) and returns false on the poll path instead of waiting.
- `src/core/Song.cpp:491` - `processNextBuffer()` pins ONE snapshot per audio period and passes it to
  the ramps (`buildAutomationRamps` snapshot overload, `src/core/Song.cpp:1421`) and to
  `processAutomations` (`src/core/Song.cpp:567`), so a publish landing mid-period cannot give two
  ticks of the same period different banks.
- `src/core/TrackContainer.cpp:445-459` - `fillAutomatedValues` is now the snapshot-taking virtual
  (absolute reads, nested pattern reads at `:529`, relative clips); the 4-argument form pins one
  snapshot for control-thread callers. `automatedValuesAt` keeps its signature and pins once.
  `src/core/PatternStore.cpp:234` and `Song::fillAutomatedValues` carry the snapshot through the
  pattern time offset unchanged.
- Siblings and callers checked and kept consistent: `AutomationLatency::compensatedClips` (the
  `automation.latency_compensation` status path) reads through one snapshot; `setEnabled`/`load`/
  `clear` publish the enabled bit in the same `m_state` word; every `readTime`/`shiftBlock` caller
  (`TrackContainer.cpp:406` region, `Song.cpp` ramps) takes the snapshot; no new control command was
  added and no command file was touched.

## Regression

`tests/src/core/W3EngineTest.cpp:242`
`latencyRefreshKeepsOneReadOnPublishedClipValues()`: two clips drive two effects on channel 1's
chain; a `RefreshGateEffect` (`W3EngineTest.cpp:76`) blocks inside its `latencyFrames()` the second
time refresh computes a clip's upstream frames. While the refresh thread is parked between the two
clips (semaphore handshake, no sleeps), the test reads `automatedValuesAt(200)` and requires both
values to be exactly what they were before the channel's input latency changed. Without coherent
publication the first clip already carried its new 40-tick latency (0.2 instead of 0.25) and the
test fails; with it, the read pins the previously published bank and both values are unchanged.

## Verification evidence

Every command was run from the worktree root
`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-576-port`
unless the row states a different cwd. All logs are under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-576-port.r2/logs`.

| Check | Exact command | cwd | Exit | Evidence |
|---|---|---|---:|---|
| Baseline build of the test target (clean tree) | `cmake --build build --target W3EngineTest -j 20` | worktree root | 0 | `build-1-baseline.log` |
| Baseline test run (clean tree) | `ctest -R '^W3EngineTest$' --output-on-failure` | `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-576-port/build/tests` | 0 | `test-1-baseline.log`: 1/1 passed |
| RED: test-only change on the original product | `ctest -R '^W3EngineTest$' --output-on-failure` | `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-576-port/build/tests` | 8 | `test-2-red.log`: `Actual (firstDuring): 0.2`, `Expected (firstBefore): 0.25` |
| Build with the fix (core library) | `cmake --build build --target zene -j 20` | worktree root | 0 | `build-3-fix.log` |
| Build of the changed-header consumers | `cmake --build build --target W3EngineTest AutomationRenderAllocationTest SampleAccurateAutomationTest AutomationTrackTest AutomationModesTest AutomationLanesTest -j 20` | worktree root | 0 | `build-4-tests.log` |
| GREEN: affected family | `ctest -R 'W3EngineTest\|AutomationRenderAllocationTest\|SampleAccurateAutomationTest\|AutomationTrackTest\|AutomationModesTest\|AutomationLanesTest' --output-on-failure` | `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-576-port/build/tests` | 0 | `test-3-green.log`: 7/7 passed |
| GREEN: full automation family | `ctest -R 'Automation\|W3Engine' --output-on-failure` | `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-576-port/build/tests` | 0 | `test-6-family.log`: 16/16 passed |
| Reverse-patch setup, round 1 (product diff vs base `2da2ed356`) | `git diff 2da2ed356 -- include/AutomationClip.h include/AutomationLatency.h include/TrackContainer.h include/PatternStore.h include/Song.h src/core/AutomationClip.cpp src/core/AutomationLatency.cpp src/core/PatternStore.cpp src/core/Song.cpp src/core/TrackContainer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-576-port.r2/logs/fix.patch` | worktree root | 0 | `fix.patch` (695 lines) |
| Reverse the product diff, round 1 | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-576-port.r2/logs/fix.patch` | worktree root | 0 | only the test and the registration stayed modified |
| Reverse-patch build, round 1 | `cmake --build build --target W3EngineTest -j 20` | worktree root | 0 | `build-5-reversed.log` (the test compiles against the old API) |
| Reverse-patch RED, round 1 | `ctest -R '^W3EngineTest$' --output-on-failure` | `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-576-port/build/tests` | 8 | `test-4-reversed-red.log`: same assertion, `firstDuring: 0.2` vs `firstBefore: 0.25` |
| Restore the fix, round 1 | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-576-port.r2/logs/fix.patch` | worktree root | 0 | product changes back in place |
| Rebuild + GREEN after restore, round 1 | `cmake --build build --target W3EngineTest -j 20` then `ctest -R '^W3EngineTest$' --output-on-failure` | worktree root, then `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-576-port/build/tests` | 0, 0 | `build-6-reapplied.log`, `test-5-green.log`: 1/1 passed |
| Gates after the round-1 commits | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py` | worktree root | 0 | `gate-1.log`: all PASS (`RESULT: PASS`) |
| Rebuild with the bounded `readSnapshot()` (round 2) | `cmake --build build --target zene -j 20` | worktree root | 0 | `build-r3-1-zene.log` |
| Rebuild the automation test targets (round 2) | `cmake --build build --target W3EngineTest AutomationModesTest AutomationLanesTest AutomationWriteToEndTest AutomationThinTest AutomationPrepassProofTest AutomationRenderAllocationTest -j 20` | worktree root | 0 | `build-r3-2-tests.log` |
| GREEN: automation family with the bounded acquisition | `ctest -R 'Automation\|W3Engine' --output-on-failure` | `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-576-port/build/tests` | 0 | `test-r3-1-green-family.log`: 16/16 passed |
| Reverse-patch setup, round 2 (full product diff vs base) | `git diff 2da2ed356 -- include/AutomationClip.h include/AutomationLatency.h include/PatternStore.h include/Song.h include/TrackContainer.h src/core/AutomationClip.cpp src/core/AutomationLatency.cpp src/core/PatternStore.cpp src/core/Song.cpp src/core/TrackContainer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-576-port.r2/logs/fix-r3.patch` | worktree root | 0 | `fix-r3.patch` (709 lines) |
| Reverse the product diff, round 2 | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-576-port.r2/logs/fix-r3.patch` | worktree root | 0 | pre-fix product tree restored |
| Reverse-patch build, round 2 | `cmake --build build --target zene W3EngineTest -j 20` | worktree root | 0 | `build-r3-3-reversed.log` |
| Reverse-patch RED, round 2 | `ctest -R '^W3EngineTest$' --output-on-failure` | `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-576-port/build/tests` | 8 | `test-r3-2-reversed-red.log`: `Actual (firstDuring): 0.2`, `Expected (firstBefore): 0.25` |
| Restore the product diff, round 2 | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-576-port.r2/logs/fix-r3.patch` | worktree root | 0 | product changes back in place |
| Rebuild + GREEN after restore, round 2 | `cmake --build build --target zene W3EngineTest -j 20` then `ctest -R '^W3EngineTest$' --output-on-failure` | worktree root, then `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-576-port/build/tests` | 0, 0 | `build-r3-4-restored.log`, `test-r3-3-green.log`: 1/1 passed |
| Gates after the final edit | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py` | worktree root | 0 | `gate-r3-1.log`: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS (`RESULT: PASS`) |
| Whitespace | `git diff --check` | worktree root | 0 | clean |

The round-2 review's own direct ratchet invocations (complexity, file-length) exit 1 only on
pre-existing train entries unrelated to this change; the diff-aware gate run above reports no new
items in this change and exits 0.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Round 2 reverse (full product diff vs base `2da2ed356`, test kept):
  `ctest -R '^W3EngineTest$' --output-on-failure` exit **8**
  (`test-r3-2-reversed-red.log`, `firstDuring: 0.2` vs `firstBefore: 0.25`). Restored: same command
  exit **0** (`test-r3-3-green.log`, 1/1). The round-1 reverse run showed the same pair
  (`test-4-reversed-red.log` exit 8, `test-5-green.log` exit 0), and the test-only RED on the
  original tree exited **8** with the same assertion (`test-2-red.log`).
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected
  values it uses).** The interleaving is forced with a `QSemaphore` handshake: the refresh thread
  blocks inside `RefreshGateEffect::latencyFrames()` (armed once, `m_blockNext`), releases
  `m_entered`, and waits on `m_resume`; the main thread reads only after `waitUntilEntered()`
  succeeds (a 3 s cap that only turns a hang into a QVERIFY failure). Injected values: channel 1's
  input latency set to `ceil(framesPerTick * 40)` frames between the two reads, clip curves fixed
  (0 at tick 0, 1 at tick 800, linear). No sleeps, no timing assumptions.
- **Nothing on the audio path waits or spins unboundedly; the acquisition is strictly bounded.**
  `readSnapshot()` (`src/core/AutomationLatency.cpp:187`) is a `for` loop bounded by
  `SnapshotAttempts` (= 2, `include/AutomationLatency.h:124`): load, `fetch_add`, validate, then
  `fetch_sub` and retry at most once more; on exhaustion it returns the unpinned fallback
  (`ReadSnapshot(nullptr, 0)`), which reports `enabled() == false` and `latencyFrames() == 0`
  without touching any bank (`src/core/AutomationLatency.cpp:176` checks `m_owner`). No `yield`,
  sleep, lock, condition variable or futex wait exists on the audio side; the only yielding in the
  subsystem is `refreshClips(waitForFreeBank=true)` (`src/core/AutomationLatency.cpp:257`), which
  runs on the control thread at enable and project load, and the poll path skips a busy bank
  instead of waiting.
- **Safe bank reuse is preserved with the bounded protocol.** The writer (`refreshClips`,
  `src/core/AutomationLatency.cpp:254`) rewrites a bank only after observing its reader count as 0;
  a raced reader that registered late validates its publication, unregisters, and falls back
  unpinned - it never reads bank contents - so it can neither observe a rewrite nor block one.
- **The fix is at the cause and complete: every caller and sibling of what you changed was checked
  and stays consistent.** Callers audited with ripgrep before the change: `readTime`/`shiftBlock`
  (only `TrackContainer.cpp` relative/absolute reads and `Song::buildAutomationRamps` - all now take
  the period snapshot), `latencyFrames()`/`setLatencyFrames` (only `AutomationLatency` itself,
  `Song::buildAutomationRamps` and `W3EngineTest` - the setter is gone, the getter pins its own
  snapshot for control-thread callers), `fillAutomatedValues` (only `Song::processAutomations`,
  `TrackContainer::automatedValuesAt`, the nested `PatternStore` read and the allocation-probe test
  which keeps the 4-argument form), `compensatedClips` and the `automation.latency_compensation`
  command (signature unchanged, now reads one snapshot). No other overrides of the changed virtuals
  exist in the tree. The round-2 change touches only `readSnapshot()` and the fallback guard in
  `ReadSnapshot::latencyFrames()`; every snapshot consumer sees the same type and semantics.
- **No collateral damage: old projects and saved data still load as before, and any limit or check
  you added does not hit other callers that legitimately exceed it.** The save format is untouched
  (`AutomationLatency::save` still writes the same single element; banks are derived, never saved).
  With compensation off - which is what every older project loads - both banks stay 0 and every read
  path takes the same arithmetic as before (the `W3EngineTest` load/undo test and the automation
  family, 16/16, cover this). The `SnapshotAttempts` bound is an internal constant, not a limit any
  caller can exceed; the fallback path degrades only the one audio block that hit the race, and only
  its compensation, coherently, while the next period pins the newly published bank.
- **Nothing added runs on the audio thread and allocates, frees, locks, does I/O or emits a
  signal.** The audio-side additions are `AutomationLatency::readSnapshot()` / `releaseSnapshot()`
  (`src/core/AutomationLatency.cpp:187`, `:206`: atomic load, `fetch_add`, validate, `fetch_sub` -
  lock-free, allocation-free, bounded) and the pass-through of the pinned `ReadSnapshot` (two words)
  through `Song::processNextBuffer`, `buildAutomationRamps`, `processAutomations`,
  `fillAutomatedValues`, `collectAutomatedValues`, `applyRelativeClips`, `readTime`, `shiftBlock`.
  `std::this_thread::yield()` appears only in the control-thread `refreshClips(waitForFreeBank=true)`
  path (enable and project load). `ReadSnapshot`'s move/destructor are two word copies and a
  `fetch_sub`; the spill path in `processAutomations` is unchanged.
- **Undo and commands.** No new control command was added and no command file was touched (the
  ControlCommandsSnapshot canary is unaffected); `automation.latency_compensation` keeps its
  schema, handler and one-step undo (proved by the existing `W3EngineTest`
  `automationReadsWhereTheLatentAudioIs` undo assertions, still passing).
- **Verdict evidence (REAL).** Pre-fix `src/core/AutomationLatency.cpp:177-182` wrote each clip's
  atomic inside the walk while `m_enabled` stayed true; the RED run (exit 8, `0.2` vs `0.25`) is the
  observed mixture.
- **ALREADY-FIXED / FALSE-POSITIVE evidence: not applicable - verdict is FIXED.** The pre-fix code
  is in this branch's history at `2da2ed356` (`src/core/AutomationLatency.cpp:177-182`, per-clip
  atomic stores during the walk) and the regression fails against it (exit 8), so the defect was
  neither already fixed nor a false positive here.

## Files committed

- `include/AutomationClip.h`, `include/AutomationLatency.h`, `include/TrackContainer.h`,
  `include/PatternStore.h`, `include/Song.h`
- `src/core/AutomationClip.cpp`, `src/core/AutomationLatency.cpp`, `src/core/TrackContainer.cpp`,
  `src/core/PatternStore.cpp`, `src/core/Song.cpp`
- `tests/src/core/W3EngineTest.cpp`
- `tests/upstream-modifications.txt` (the eight inherited product files above, with reasons)
- `docs/qwen-fixes-2026-10/cluster-new-p9-576-port.md`

No `tests/fork-sources.txt` change is needed: no new file was created, and
`include/AutomationLatency.h` / `src/core/AutomationLatency.cpp` are already registered fork-new
sources (`tests/fork-sources.txt:713`, `:1281`).
