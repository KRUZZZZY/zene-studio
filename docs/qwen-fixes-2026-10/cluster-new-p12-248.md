FIXED

# cluster new-p12-248 — Sample accurate automation locks on the audio thread

new-p12-248: FIXED (round 6 evidence). Round 2 added the lock-free render guard (`RenderLock`) on the
block, tick and relative-clip reads; round 5 removed the data loss that guard caused in the recording
scan, which now reads an atomically published target and takes no clip lock. Round 6 re-ran the whole
proof into its own log folder; no product code changed in round 6.

## Verdict: REAL
- `Song::processNextBuffer()` calls `buildAutomationRamps()` once per period (src/core/Song.cpp:582).
  For each opt-in clip, that call ran `AutomationClip::writeBlockRamp()` and `objects()`, and both took
  the clip's `m_clipMutex` with a blocking `QMutexLocker` (src/core/AutomationClip.cpp:1077, :331).
- In the same period, the per-tick read also took a blocking lock: `processAutomations` →
  `collectAutomatedValues` → `valueAt` (src/core/TrackContainer.cpp:589 → AutomationClip.cpp:864),
  `applyRelativeClips` → `valueAt`/`getMin`/`getMax`/`objects` (TrackContainer.cpp:479), and the
  recording scan's `firstObject()` for every clip in range (Song.cpp:796 → AutomationClip.cpp:288).
- Control-thread edits hold that same mutex without the engine guard: `putValue` (AutomationClip.cpp:418),
  `removeNode` (:514), `setDragValue` (:772). `putValue` also emits `dataChanged()` while holding the lock.
  So a period that reaches a clip under edit waited for the edit to finish.

## Fix, part 1: the render never waits on the clip lock (round 2, kept)
- `include/AutomationClip.h` adds `AutomationClip::RenderLock`, an RAII guard that calls
  `m_clipMutex.tryLock()` once and never waits (include/AutomationClip.h:135-142).
- `Song::buildAutomationRamps` takes each clip through `RenderLock` and leaves a busy clip out of the
  block (src/core/Song.cpp:1635). No ramp is published, so `AutomatableModel::automationRamp()` returns
  null for the period and the control keeps its per-tick value.
- `TrackContainer::collectAutomatedValues` and `applyRelativeClips` do the same for their tick
  (src/core/TrackContainer.cpp:471, :575).
- Why `tryLock` rather than an immutable published copy: the clip's node map is a `QMap` (allocating,
  implicitly shared). An immutable published copy would need a node capacity (which would refuse large
  clips: definition-of-done item 4) and an off-thread reclamation scheme. That is a redesign. The guard
  removes the wait with no allocation, no new command and no change to saved data. Without contention
  behaviour is unchanged; under contention one block or tick reads the previous value.

## Fix, part 2: recording never loses a tick (round 5, this review)
The round-2 guard was also applied to the recording scan in `Song::processAutomations`. A failed
acquisition skipped the whole branch, including its only `queuePendingWrite()` call, so a recording
clip silently lost that tick's value while an edit held the lock (src/core/Song.cpp:793-824 before
this round; recording is enabled through include/AutomationClip.h:395-396).

The scan now takes no clip lock at all:
- `include/AutomationClip.h`: `m_liveTarget`, a `std::atomic<const AutomatableModel*>`, is published
  under `m_clipMutex` by `publishLiveTarget()` (include/AutomationClip.h:478-486). It is republished by
  every change to `m_objects`: `addObject` (AutomationClip.cpp:216), `removeObject` (:245),
  `objectDestroyed` (:1562), `cleanObjects` (:1585), and `clearObjects` (header :406). Those changes
  already take the engine change guard, and a target's destructor takes the guard before it tells the
  clip, so a pointer read inside a render period stays alive for it.
- `liveTarget()` reads that atomic with acquire (header :121-124); `hasLiveTarget()` is derived from
  the same pointer (:126). The old `std::atomic<bool> m_hasLiveTarget` is gone.
- `m_isRecording` is now `std::atomic<bool>` (header :509); `isRecording()`/`setRecording()` load/store
  it (header :395-396). Both atomics are always lock-free on the supported targets.
- `Song::processAutomations` uses `p->liveTarget()` and `p->isRecording()` with no guard and no
  `RenderLock` (src/core/Song.cpp:790-800), so `queuePendingWrite()` runs for every in-range tick while
  an edit holds the lock (Song.cpp:821-823). `RenderLock` stays on `buildAutomationRamps`,
  `collectAutomatedValues` and `applyRelativeClips`, as accepted in the earlier review.
- A clip with no live target still records nothing: `liveTarget()` is null and `writes` stays false;
  the old `firstObject()` fallback model is no longer consulted on this path
  (src/core/AutomationClip.cpp:286-296 remains the control-thread fallback for min/max).

Realtime: the scan does two lock-free atomic loads and the existing lock-free `queuePendingWrite()`
(no allocation, no logging, no wait). No new control command and no undo change. Local TSan does not
run on this box; the race argument is the atomic publication above, with the engine change guard
protecting pointer lifetime across a period.

## Regression test
tests/src/core/AutomationRenderLockTest.cpp (registered in tests/CMakeLists.txt,
tests/fork-sources.txt and tests/all-sources.txt). Five cases; the two that matter here:
- `theRampPublishDoesNotWaitForAnEditedClip` drives the per-block publish through an explicit
  three-state sequence, one automation period each: **baseline publish** (no edit, ramp published),
  **edit-held skip** (a real `putValue()` holds the clip lock; the publish returns and leaves no ramp),
  **released publish** (the edit is gone, the ramp is published again).
- `aRecordingClipKeepsItsTickWhileAnEditHoldsTheLock` (new this round): the clip is recording; a real
  `putValue()` edit holds the clip lock; the render period must return, `pendingWriteCount()` must be
  greater than zero right after it, and firing the `automationWriteDrainTimer` must plant a node at the
  recorded tick with the value the control held. A no-edit control half repeats the period at a second
  tick and asserts the same, so the edit-held assertions cannot pass vacuously.
- The edit is REAL, not a seam: `AutomationClip::putValue()` emits `dataChanged()` inside its
  `m_clipMutex` scope, and a `Qt::DirectConnection` slot parks that thread on a `std::promise`, so the
  lock is held the way an editor holds it. The test waits on that promise before reading; no sleeps.
  The only bound is the 10 s escape hatch for the unfixed build, whose read would never return.

## State sequence (test `aRecordingClipKeepsItsTickWhileAnEditHoldsTheLock`)
Song.cpp is in the diff, so the states the test drives, in order:
1. recording enabled, a real `putValue()` edit holds the clip lock, period at tick 100 returns;
2. `pendingWriteCount() > 0` straight after that period (the tick was queued, not lost);
3. `automationWriteDrainTimer` fires, a node exists at tick 100 with the control's value;
4. control half: no edit, period at tick 140 queues a write, the drain plants a node at tick 140.

## Commands and EXIT codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-248.r6/logs)
- GREEN build: `cmake --build build -j4 --target zene AutomationRenderLockTest` → build-1-green.log EXIT=0.
- GREEN test: `ctest -R AutomationRenderLockTest --output-on-failure` (from build/tests)
  → test-1-green.log EXIT=0 (1/1 passed).
- Family: `ctest -R '^(AutomationModesTest|AutomationPreFirstValueTest|AutomationRenderLockTest|SampleAccurateAutomationTest|ControlRecordingRoutesTest|ControlSessionRecordLandTest)$' --output-on-failure`
  after building those targets (build-4-family.log EXIT=0) → test-5-family-green.log EXIT=0, 6/6 passed.
  (A first, broader `ctest -R Automation` run, test-2-family-green.log EXIT=8, failed only on
  ControlAutomationScriptTest/ControlAutomationModesTest, which need LMMS_TEST_PLUGIN_DIR, and on
  13 tests whose binaries were not built; it is not used as evidence.)
- 4b patch: `git show 2c58bac4de -- include src > logs/fix.patch` (the round-5 product hunks: 3 files,
  the atomic target/recording publication and the Song.cpp scan change) → EXIT=0;
  `git apply -R logs/fix.patch` → EXIT=0.
- 4b RED: rebuild → build-2-red.log EXIT=0; `ctest -R '^AutomationRenderLockTest$' --output-on-failure`
  → test-3-red.log **EXIT=8**: `FAIL! : aRecordingClipKeepsItsTickWhileAnEditHoldsTheLock()
  'clip->pendingWriteCount() > 0' returned FALSE. (a recording clip lost its tick while an edit held the
  clip lock)`, Loc tests/src/core/AutomationRenderLockTest.cpp(280); Totals: 6 passed, 1 failed.
- 4b GREEN: `git apply logs/fix.patch` → EXIT=0, `git status --short` empty; rebuild →
  build-3-green-again.log EXIT=0; ctest → test-4-green-again.log **EXIT=0** (1/1 passed).
- Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`
  → feature-check.log EXIT=0, RESULT: PASS (all 8 rows PASS).
- Completion check: see the last section of this file.
- Registration: `tests/upstream-modifications.txt` lists `src/core/AutomationClip.cpp` and the
  `include/AutomationClip.h` and `src/core/Song.cpp` reasons; the list ends with a newline.

## Self-check
- **Regression test FAILS with the product change reverted and PASSES with it:** test-3-red.log EXIT=8
  (the recording case loses its tick), test-4-green-again.log EXIT=0. The red failure is the
  data-loss assertion, not a timeout: the round-2 `tryLock` guard is still present in the red build, so
  the period returns but records nothing.
- **Deterministic:** no sleeps and no injected wall-clock values. The clip lock is held by a real
  `putValue()` parked on a `std::promise` from a direct `dataChanged` slot, and the test waits on that
  promise before reading. The recording tick is fixed by `setTicks(100)` / `setTicks(140)`; the drain is
  fired by invoking the `automationWriteDrainTimer` timeout directly. The 10 s `future::wait_for` is
  only the escape hatch for the fully unfixed build, whose read would otherwise block.
- **Fix at the cause and complete; callers and siblings checked:** the recording scan takes no clip
  lock; `liveTarget()`/`isRecording()` are the only render-side reads, both atomics. Every `m_objects`
  mutation republishes the pointer (`addObject`, `removeObject`, `objectDestroyed`, `cleanObjects`,
  `clearObjects`). The remaining `RenderLock` users are the three accepted read paths.
- **No collateral damage:** no saved data, file format, command or undo path is touched; no limit or
  capacity is added. Without contention every path behaves as before; the six-test family passes
  (test-5-family-green.log EXIT=0).
- **GUI screenshot:** none. The diff touches no src/gui file, so no screenshot is claimed.
- **ALREADY-FIXED / FALSE-POSITIVE code reference:** not applicable; the verdict is FIXED.
- **Sibling commands in the command files you changed:** this job changed no command file.

Delegation: Haiku ran the green build/test runs, the family run, and the revert / red / restore / green
sequence; I read its logs (test-1, test-3, test-4, test-5) and the clean `git status` myself.

## Other findings
- `NotePlayHandle::processTimePos` (src/core/NotePlayHandle.cpp:752) reads the per-note detuning clip
  with the blocking `valueAt()` on the render path. Piano-roll detuning edits hold the same kind of clip
  lock. This is the same realtime exposure in a separate, inherited feature; it is not changed here
  because its regression would need a note-play fixture.
- `AutomationClip::hasAutomation()` (include/AutomationClip.h) reads `m_timeMap.isEmpty()` without the
  lock, and the render paths call it before taking any lock. This is a pre-existing unsynchronised read.
