FIXED

# cluster new-p12-248 — Sample accurate automation locks on the audio thread

new-p12-248: FIXED (round 2; round 1 reported NEEDS-CONTEXT without a test, and the review sent it back)

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
tests/src/core/AutomationRenderLockTest.cpp (new). Registered in tests/CMakeLists.txt,
tests/fork-sources.txt and tests/all-sources.txt. Four cases:
`theRampPublishDoesNotWaitForAnEditedClip`, `thePerTickReadDoesNotWaitForAnEditedClip`,
`aRelativeClipReadDoesNotWaitForAnEditedClip`, `aWholePeriodDoesNotWaitForAnEditedClip`.
- A helper thread runs a real `putValue()`. A `Qt::DirectConnection` slot on `dataChanged`, which is
  emitted inside putValue's lock scope, parks that thread on a promise, so the clip lock is held the
  way an editor holds it.
- The read under test runs on its own thread.
- Each case also has a control half: after the edit is released, the same read publishes the ramp
  or value, so the "skipped" assertion cannot pass vacuously.

## Commands and EXIT codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-248.r2/logs)
- `cmake --build build --target zene` (CMake regeneration for the new test; `cmake build` itself is refused by the lane guard, 02-reconfigure.log EXIT=97) → 04-build-zene.log EXIT=0
- RED (test first, no product change): `cmake --build build --target AutomationRenderLockTest -j4` → 05-build-red.log EXIT=0; `ctest -R '^AutomationRenderLockTest$' --output-on-failure` → 06-test-red.log EXIT=8 (4 failed: each read "waited for the clip lock an edit held")
- GREEN: `cmake --build build --target zene AutomationRenderLockTest -j4` → 07-build-green.log EXIT=0; ctest → 08-test-green.log EXIT=0 (Passed 0.51 sec)
- 4b: `git diff -- include/AutomationClip.h include/Song.h src/core/Song.cpp src/core/TrackContainer.cpp > logs/fix.patch && git apply -R logs/fix.patch` EXIT=0; `cmake --build build --target zene AutomationRenderLockTest -j4` → 12-build-4b-red.log EXIT=0; `ctest -R '^AutomationRenderLockTest$' --output-on-failure` → 13-test-4b-red.log **EXIT=8** (Totals: 2 passed, 4 failed)
- 4b: `git apply logs/fix.patch` EXIT=0; `cmake --build build --target zene AutomationRenderLockTest -j4` → 16-build-4b-green.log EXIT=0; ctest → 17-test-4b-green.log **EXIT=0** (100% passed)
- Family: build zene + 8 automation/recording test targets → 24-build-family-final.log EXIT=0; `ctest -R '^(AutomationModesTest|AutomationPreFirstValueTest|AutomationRenderLockTest|SampleAccurateAutomationTest|ControlRecordingRoutesTest|ControlSessionRecordLandTest)$'` → 25-test-family-final.log EXIT=0 (6/6 passed)
- `ControlAutomationScriptTest` and `ControlAutomationModesTest` fail with the fix (21-test-family.log EXIT=8) AND without it (fix reverted, 22-build-baseline.log EXIT=0, 23-test-baseline.log EXIT=8, the same 4 and 5 failures). Their message is "this song exposes no automatable device parameter ... the binary needs the instrument modules": this is an environment gap and predates the fix.
- Full-tree rebuild: `cmake --build build -j4` was REFUSED by the lane guard ("full builds are the parent's job"; 11-build-all.log, 14-build-4b-green-all.log). Every target I ran was rebuilt by name. Other targets that include AutomationClip.h/Song.h were not rebuilt here. The header change is purely additive (a nested class plus a comment).
- Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` → 26-feature-check.log EXIT=0, RESULT: PASS (all 8 rows PASS)

## Self-check
- **Regression test FAILS with the product change reverted and PASSES with it:** `git apply -R logs/fix.patch` then rebuild and run → 13-test-4b-red.log EXIT=8 (4 of 4 cases fail). `git apply logs/fix.patch` then rebuild and run → 17-test-4b-green.log EXIT=0.
- **Deterministic:** no sleeps and no injected timing. The lock is held by a real edit parked on a `std::promise` from a direct `dataChanged` slot (a synchronisation hook), and the test waits on that promise before reading. The fixed read never waits, so GREEN does not depend on timing (0.51 s). The single `future::wait_for(10 s)` bound is only the escape hatch for the unfixed build, whose read would otherwise block until release. It mirrors the existing `PhaseDSidechainTest::theSidechainAmountReadTakesNoLock`, which uses `wait_for(2 s)`.
- **Fix at the cause and complete; callers and siblings checked:** every audio-thread call into `m_clipMutex` on the automation-track path is now under `RenderLock`: buildAutomationRamps (`objects`, `writeBlockRamp`), collectAutomatedValues (`valueAt`, `objects`), applyRelativeClips (`valueAt`, `getMin`/`getMax`→`firstObject`, `objects`), and the processAutomations recording scan (`firstObject`). `aWholePeriodDoesNotWaitForAnEditedClip` proves a whole `processNextBuffer()` period returns. Other callers found by `rg`: AutomatableModel.cpp:350 and :1182 (`globalAutomationValueAt`) are control-thread paths. NotePlayHandle.cpp:752 (per-note detuning) is a separate inherited feature and is listed under Other findings.
- **No collateral damage:** no saved data, file format, command or undo path is touched. No limit or capacity was added. Without contention, every path behaves as before (`tryLock` succeeds where `lock` would have). SampleAccurateAutomationTest, AutomationPreFirstValueTest, AutomationModesTest, ControlRecordingRoutesTest and ControlSessionRecordLandTest pass (25-test-family-final.log EXIT=0). The two Control* failures are identical without the fix (23-test-baseline.log).
- **ALREADY-FIXED / FALSE-POSITIVE code reference:** not applicable. The verdict is FIXED, and the bug was real (RED evidence above).
- **Sibling commands in command files changed:** this job changed no command file (no `ControlCommands*.cpp` in the diff). The app.* and arrangement.* commands the check lists come from earlier history on this branch and do not touch automation clip locking.

## Other findings
- `NotePlayHandle::processTimePos` (src/core/NotePlayHandle.cpp:752) reads the per-note detuning clip with the blocking `valueAt()` on the render path. Piano-roll detuning edits hold the same kind of clip lock. This is the same realtime exposure in a separate, inherited feature. It is not changed here because its regression would need a note-play fixture.
- `AutomationClip::hasAutomation()` (include/AutomationClip.h:247) reads `m_timeMap.isEmpty()` without the lock, and the render paths call it before taking any lock. This is a pre-existing unsynchronised read and is not changed here.
- `ControlAutomationScriptTest` / `ControlAutomationModesTest` fail in this lane's build because no instrument plugin modules are available (23-test-baseline.log).
