FIXED

# cluster new-p12-248 — Sample accurate automation locks on the audio thread

new-p12-248: FIXED (round 3; round 1 reported NEEDS-CONTEXT without a test, round 2's submit check
asked for a state-sequence artifact and an explicit three-state test, both added here)

## Verdict: REAL
- `Song::processNextBuffer()` calls `buildAutomationRamps()` once per period (src/core/Song.cpp:582).
  For each opt-in clip, that call ran `AutomationClip::writeBlockRamp()` and `objects()`, and both took
  the clip's `m_clipMutex` with a blocking `QMutexLocker` (src/core/AutomationClip.cpp:1077, :331).
- In the same period, the per-tick read also took a blocking lock: `processAutomations` →
  `collectAutomatedValues` → `valueAt` (src/core/TrackContainer.cpp:589 → AutomationClip.cpp:864),
  `applyRelativeClips` → `valueAt`/`getMin`/`getMax`/`objects` (TrackContainer.cpp:479), and the
  recording scan's `firstObject()` for every clip in range (src/core/Song.cpp:796 → AutomationClip.cpp:288).
- Control-thread edits hold that same mutex without the engine guard: `putValue` (AutomationClip.cpp:418),
  `removeNode` (:514), `setDragValue` (:772). `putValue` also emits `dataChanged()` while holding the lock.
  So a period that reaches a clip under edit waited for the edit to finish.
- The RED run below shows the wait: each of the four reads failed to return while a real `putValue`
  edit held the lock.

## Fix (at the cause, every audio-thread taker of the clip lock)
- `include/AutomationClip.h`: adds `AutomationClip::RenderLock`, an RAII guard that calls
  `m_clipMutex.tryLock()` once and never waits. While it owns the lock, the clip's own accessors
  (`objects()`, `firstObject()`, `valueAt()`, `writeBlockRamp()`) re-enter the recursive mutex as owner,
  so they never wait either.
- `src/core/Song.cpp` `buildAutomationRamps`: each clip is taken through `RenderLock`. A clip whose
  lock is busy is left out of that block. No ramp is published, so `AutomatableModel::automationRamp()`
  returns null for the period (AutomatableModel.cpp:1070) and the control keeps its per-tick value.
- `src/core/Song.cpp` `processAutomations` recording scan: a busy clip is skipped for that tick.
  `firstObject()` is no longer reached without the lock.
- `src/core/TrackContainer.cpp` `collectAutomatedValues` and `applyRelativeClips`: a busy clip is
  skipped for that tick, so the control keeps its previous value for one tick.
- `include/Song.h` contract comment and docs/SAMPLE-ACCURATE-AUTOMATION.md updated to say "never waits".

Why `tryLock` rather than an immutable published copy: the clip's node map is a `QMap` (allocating,
implicitly shared). An immutable published copy would need a node capacity (which would refuse
large clips: definition-of-done item 4) and an off-thread reclamation scheme. That is a redesign. The
guard removes the wait with no allocation, no new command and no change to saved data.
- Without contention, behaviour is unchanged: `tryLock` succeeds wherever the blocking lock would
  have.
- With contention, the only difference is that one block or tick reads the previous value instead
  of waiting for the edit.

Realtime: there is no allocation and no logging. The thread never waits: `tryLock` never blocks, and
`unlock` at most wakes a waiter. The repo already uses this pattern on the render path
(include/Track.h:476 and include/PlayHandle.h:122, `tryLock`). Local TSan does not run on this box.
Race reasoning: every read of the node map on these paths still happens under the clip mutex, now
held by the guard. When the guard fails, the path reads nothing from that clip. So no read happens
without the mutex that did not already happen before this change (`hasAutomation()` was already
read unlocked, see Other findings).

## Regression test
tests/src/core/AutomationRenderLockTest.cpp. Registered in tests/CMakeLists.txt,
tests/fork-sources.txt and tests/all-sources.txt. Four cases:
`theRampPublishDoesNotWaitForAnEditedClip`, `thePerTickReadDoesNotWaitForAnEditedClip`,
`aRelativeClipReadDoesNotWaitForAnEditedClip`, `aWholePeriodDoesNotWaitForAnEditedClip`.
- A helper thread runs a real `putValue()`. A `Qt::DirectConnection` slot on `dataChanged`, which is
  emitted inside putValue's lock scope, parks that thread on a promise, so the clip lock is held the
  way an editor holds it.
- The read under test runs on its own thread.
- Each case also has a control half: after the edit is released, the same read publishes the ramp
  or value, so the "skipped" assertion cannot pass vacuously.

## State sequence
`theRampPublishDoesNotWaitForAnEditedClip` drives the per-block publish through three explicit
states, one automation period each (a model's published ramp is only valid for the period it was
built in), with a hard assertion per state:

1. **baseline publish** - no edit in progress; `buildAutomationRamps()` publishes the ramp
   (`model.automationRamp() != nullptr`).
2. **edit-held skip** - a real `putValue()` edit holds the clip's `m_clipMutex`; the publish
   returns without waiting and leaves the period with no ramp (`automationRamp() == nullptr`).
3. **released publish** - the edit is released; the same block publishes the ramp again
   (`automationRamp() != nullptr`).

State 2 is the bug: in the unfixed build the publish blocks inside the edit and the reader thread
never returns (05-test-red.log: four reads each hit the 10 s escape hatch, 40.5 s total). State 1 and
state 3 are the controls that show the assertion cannot pass vacuously.

## Commands and EXIT codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-248.r3/logs)
Round 3 reruns the extended test from the configured build tree; the round-2 log paths are stale and
superseded by these.
- GREEN (extended three-state test, fix in place): `cmake --build build --target zene AutomationRenderLockTest -j4` → 01-build-green.log EXIT=0; `ctest -R '^AutomationRenderLockTest$' --output-on-failure` (run from build/tests) → 02-test-green.log EXIT=0 (Passed 0.51 sec)
- 4b patch (the fix is committed, so the patch is read from the fix commit, not from `git diff`): `git diff HEAD^ HEAD -- include/AutomationClip.h include/Song.h src/core/Song.cpp src/core/TrackContainer.cpp > logs/fix.patch` → EXIT=0 (99 lines); `git apply -R logs/fix.patch` → 03-revert.log REVERT_EXIT=0
- 4b RED: `cmake --build build --target zene AutomationRenderLockTest -j4` → 04-build-red.log EXIT=0; `ctest -R '^AutomationRenderLockTest$' --output-on-failure` → 05-test-red.log **EXIT=8** (Totals: 2 passed, 4 failed; `theRampPublishDoesNotWaitForAnEditedClip` fails at state 2)
- 4b GREEN: `git apply logs/fix.patch` → 06-restore.log RESTORE_EXIT=0; `cmake --build build --target zene AutomationRenderLockTest -j4` → 07-build-green-4b.log EXIT=0; `ctest -R '^AutomationRenderLockTest$' --output-on-failure` → 08-test-green-4b.log **EXIT=0** (100% passed)
- Family: `ctest -R '^(AutomationModesTest|AutomationPreFirstValueTest|AutomationRenderLockTest|SampleAccurateAutomationTest|ControlRecordingRoutesTest|ControlSessionRecordLandTest)$' --output-on-failure` → 09-test-family.log EXIT=0 (6/6 passed)
- Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` → 10-feature-check.log EXIT=0, RESULT: PASS (all 8 rows PASS)
- Full-tree rebuild: every target this fix needs was rebuilt by name (`zene` plus `AutomationRenderLockTest`, which pulls `include/AutomationClip.h` and `include/Song.h`). Other targets that include those headers were not rebuilt in this lane; the header change is purely additive (a nested class plus a comment), so their objects stay valid at link time.

## Self-check
- **Regression test FAILS with the product change reverted and PASSES with it:** `git apply -R logs/fix.patch` then rebuild and run → 05-test-red.log EXIT=8 (4 of 4 cases fail; the extended three-state case fails at state 2). `git apply logs/fix.patch` then rebuild and run → 08-test-green-4b.log EXIT=0.
- **State-sequence artifact:** `theRampPublishDoesNotWaitForAnEditedClip` drives baseline publish → edit-held skip with no ramp → released publish, each with its own period counter and assertion (see State sequence above). 02-test-green.log and 08-test-green-4b.log are the green runs; 05-test-red.log is the red run.
- **Deterministic:** no sleeps and no injected timing. The lock is held by a real edit parked on a `std::promise` from a direct `dataChanged` slot (a synchronisation hook), and the test waits on that promise before reading. The fixed read never waits, so GREEN does not depend on timing (0.51 s). The single `future::wait_for(10 s)` bound is only the escape hatch for the unfixed build, whose read would otherwise block until release. It mirrors the existing `PhaseDSidechainTest::theSidechainAmountReadTakesNoLock`, which uses `wait_for(2 s)`.
- **Fix at the cause and complete; callers and siblings checked:** every audio-thread call into `m_clipMutex` on the automation-track path is now under `RenderLock`: buildAutomationRamps (`objects`, `writeBlockRamp`), collectAutomatedValues (`valueAt`, `objects`), applyRelativeClips (`valueAt`, `getMin`/`getMax`→`firstObject`, `objects`), and the processAutomations recording scan (`firstObject`). `aWholePeriodDoesNotWaitForAnEditedClip` proves a whole `processNextBuffer()` period returns. Other callers found by `rg`: AutomatableModel.cpp:350 and :1182 (`globalAutomationValueAt`) are control-thread paths. NotePlayHandle.cpp:752 (per-note detuning) is a separate inherited feature and is listed under Other findings.
- **No collateral damage:** no saved data, file format, command or undo path is touched. No limit or capacity was added. Without contention, every path behaves as before (`tryLock` succeeds where `lock` would have). SampleAccurateAutomationTest, AutomationPreFirstValueTest, AutomationModesTest, ControlRecordingRoutesTest and ControlSessionRecordLandTest pass alongside this test (09-test-family.log EXIT=0, 6/6).
- **ALREADY-FIXED / FALSE-POSITIVE code reference:** not applicable. The verdict is FIXED, and the bug was real (RED evidence above).
- **Sibling commands in command files changed:** this job changed no command file (no `ControlCommands*.cpp` in the diff). The app.* and arrangement.* commands the check lists come from earlier history on this branch and do not touch automation clip locking.

## Other findings
- `NotePlayHandle::processTimePos` (src/core/NotePlayHandle.cpp:752) reads the per-note detuning clip with the blocking `valueAt()` on the render path. Piano-roll detuning edits hold the same kind of clip lock. This is the same realtime exposure in a separate, inherited feature. It is not changed here because its regression would need a note-play fixture.
- `AutomationClip::hasAutomation()` (include/AutomationClip.h:247) reads `m_timeMap.isEmpty()` without the lock, and the render paths call it before taking any lock. This is a pre-existing unsynchronised read and is not changed here.
- `ControlAutomationScriptTest` / `ControlAutomationModesTest` failed in round 2 in this lane's build because no instrument plugin modules were available; that was an environment gap and the round-2 logs no longer exist, so this run does not re-verify it and the tests were not part of the family run.
