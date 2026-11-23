FIXED

new-p9-76: a failed WAV write no longer discards the finished take. `session.record_slot_land` used to call
`SessionAudioRecorder::takeFinished()` before writing, so a refused write returned after the recorder had given up
the only copy of the take and the user could not land it again. The take is now peeked, kept in the recorder while
the write fails, and given up only after the WAV is on disk.

## Evidence and verdict

REAL. The historical line 225 now holds `recordSlot()`; the code the finding describes is `writeAndLand()`
(`src/core/ControlCommandsSessionAudioRecord.cpp:279`), which takes the finished take and then writes it.

- Before the fix, `writeAndLand()` took the take at `src/core/ControlCommandsSessionAudioRecord.cpp:284`
  (`recorder.takeFinished()`), and `SessionAudioRecorder::takeFinished()` reset the accumulator, cleared the cell,
  and set `State::Idle` (`src/core/SessionAudioRecorder.cpp:84-101` at base). The write is a later step
  (`src/core/ControlCommandsSessionAudioRecord.cpp:293` at base), so its `Refused` return left the recorder Idle
  with no buffer: `session.record_slot_land` could not be retried and `session.record_slot` could arm a new take,
  overwriting the user's performance.
- The intended contract is shown by the sibling command family: `record.sampler_capture_land` peeks with
  `takeFinished()` and only calls `releaseFinished()` after `writeRetroAudioTake()` succeeds
  (`src/core/ControlCommandsSamplerTake.cpp:320-343`, `src/core/SamplerTakeRecorder.cpp:85-105`). The session
  audio path was the outlier.
- Test seam: `retroaudiotesting::failNextTakeFinish()` (`include/RetroAudioCapture.h:209-216`,
  `src/core/RetroAudioCapture.cpp:283-285`) makes the next write fail its flush/close exactly as a full disk does;
  no sleep or race is involved.

## Fix

- `include/SessionAudioRecorder.h:92-97,134-136`: declares `finishedTake()` (the finished take while the state is
  Done, without giving it up) and the `m_finished` cache it keeps; `takeFinished()` keeps its release contract.
- `src/core/SessionAudioRecorder.cpp:85-113`: `finishedTake()` joins the drain thread once, cuts the take to its
  span once, caches it and leaves the recorder Done; `takeFinished()` now takes that buffer and then releases
  (`m_accum`, cell, `State::Idle`). `cancel()` also drops the cached buffer
  (`src/core/SessionAudioRecorder.cpp:73-83`).
- `src/core/ControlCommandsSessionAudioRecord.cpp:279-303`: `writeAndLand()` peeks with `finishedTake()`, releases
  only on the empty-take refusal, returns the write refusal with the take still in hand, and calls `takeFinished()`
  only after `writeRetroAudioTake()` succeeded, immediately before `applyLanded()`. The refusal text now says the
  take stays in hand.
- The pre-write "cell was filled" check keeps its place and its comment was corrected
  (`src/core/ControlCommandsSessionAudioRecord.cpp:324-334`): it still refuses before any WAV is written.

## Regression test

`tests/src/core/SessionAudioRecordTest.cpp`, slot `aFailedTakeWriteKeepsTheTakeInHandForARetry`:

1. records one bar, waits for `done`;
2. `failNextTakeFinish()`, then `session.record_slot_land` to a path in a `QTemporaryDir` - expects `Refused`, no
   file at the path, an empty cell, and the recorder still `done`;
3. lands the same take again, checks the WAV's PCM frame by frame, the cell, and the one undo/redo step through
   the existing `landAndCompare()` helper, then asserts the recorder is `idle` once the take is safely on disk.
   The recorder states the test drives, in order: `done` (the take finished) -> `done` (the failed write kept it in
   hand) -> `idle` (landed).

## Review verification (2026-11-23)

The fixer's proof manifest and run directory at
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-76.r1` were not present in the review filesystem, so those
artifacts could not be inspected. I independently reproduced the regression: with the product hunks reversed,
`cmake --build build --target zene SessionAudioRecordTest SessionAudioRecorderTest -j12` exited 0 and
`ctest -R '^SessionAudioRecordTest$' --output-on-failure` from `build/tests` exited 8 with actual state `idle` instead
of `done` at line 292. After restoring the product files, `cmake --build build --target zene SessionAudioRecordTest
SessionAudioRecorderTest -j4` exited 0 and `ctest -R 'SessionAudioRecord' --output-on-failure` exited 0 (2/2).

The following red/green details are from the original fixer report; its abbreviated redirect paths and referenced log
files were unavailable for inspection in this review:

- RED test (before the product fix; test already added):
  `cmake --build build --target SessionAudioRecordTest -j4 > .../logs/build-1.log 2>&1` (EXIT=0), then
  `ctest -R SessionAudioRecordTest --output-on-failure > .../logs/test-1.log 2>&1` (EXIT=8):
  `FAIL! : SessionAudioRecordTest::aFailedTakeWriteKeepsTheTakeInHandForARetry() ... Actual (state()): "idle"
  Expected ("done")`.
- GREEN test (fix applied):
  `cmake --build build --target zene SessionAudioRecordTest SessionAudioRecorderTest -j4 > .../logs/build-2.log 2>&1`
  (EXIT=0), then `ctest -R 'SessionAudioRecord' --output-on-failure > .../logs/test-2.log 2>&1` (EXIT=0; 2/2
  passed, 100%).
- 4b revert: `git diff HEAD~1 -- include/SessionAudioRecorder.h src/core/SessionAudioRecorder.cpp
  src/core/ControlCommandsSessionAudioRecord.cpp > .../logs/fix.patch` (EXIT=0);
  `git apply -R .../logs/fix.patch` (EXIT=0); `cmake --build build --target zene SessionAudioRecordTest
  SessionAudioRecorderTest -j4 > .../logs/build-8.log 2>&1` (EXIT=0); `ctest -R 'SessionAudioRecord'
  --output-on-failure > .../logs/test-9.log 2>&1` (EXIT=8; the same `"idle" != "done"` failure). 4b restore:
  `git apply .../logs/fix.patch` (EXIT=0); `cmake --build build --target zene SessionAudioRecordTest
  SessionAudioRecorderTest -j4 > .../logs/build-9.log 2>&1` (EXIT=0); `ctest -R 'SessionAudioRecord'
  --output-on-failure > .../logs/test-10.log 2>&1` (EXIT=0). The same cycle was run once before the final
  `idle` assertion was added (`build-3.log`/`test-3.log` EXIT=8, `build-4.log`/`test-4.log` EXIT=0).
- Final GREEN with the complete test: `cmake --build build --target SessionAudioRecordTest -j4 >
  .../logs/build-7.log 2>&1` (EXIT=0); `ctest -R SessionAudioRecordTest --output-on-failure >
  .../logs/test-8.log 2>&1` (EXIT=0).
- Family: `cmake --build build --target SessionAudioCellPlaybackTest -j4 > .../logs/build-5.log 2>&1` (EXIT=0);
  `cmake --build build --target SessionModelTest SessionSchedulerTest SessionSchedulerRenderTest
  SessionTransitionsTest -j4 > .../logs/build-6.log 2>&1` (EXIT=0); final `ctest -R
  'SessionAudio|SessionTransitions|SessionScheduler|SessionModel' --output-on-failure > .../logs/test-11.log 2>&1`
  (EXIT=0; 8/8 passed, including `ControlSessionTransitions`). A repeat of
  `ctest -R '^ControlSessionTransitions$' ... > .../logs/test-7.log 2>&1` also passed (EXIT=0), and an earlier
  family run after all binaries were built passed too (`.../logs/test-6.log`, EXIT=0). Note: the first family run
  (`.../logs/test-5.log`, EXIT=8) had four tests `Not Run` because those binaries were not built yet and
  `ControlSessionTransitions` failed with them; after building every binary in the family all eight pass, so that
  failure came from the stale/partial tree, not this change.
- Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py
  /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-76 > .../logs/gates-2.log 2>&1`
  (EXIT=0; table PASS on all eight rows, complexity and file-length "no new item in your files", RESULT: PASS).
  The first gate run on the same product code was `.../logs/gates-1.log` (EXIT=0).

## Self-check

- **RED/GREEN (exit codes).** With the product change reverted and the test in place, `ctest -R 'SessionAudioRecord'`
  exits 8 (`logs/test-9.log`: `Actual (state()): "idle"`, `Expected: "done"`); with the fix restored the same command
  exits 0 (`logs/test-10.log`, 2/2). The initial RED run before the fix was `logs/test-1.log` (exit 8), the first
  GREEN run `logs/test-2.log` (exit 0), and the final test with the `idle` assertion passed in `logs/test-8.log`
  (exit 0). All builds used to produce those runs exited 0 (`build-1`, `build-2`, `build-3`, `build-4`, `build-7`,
  `build-8`, `build-9`).
- **Determinism (seam).** The failure is injected, not waited for: `retroaudiotesting::failNextTakeFinish()`
  (`include/RetroAudioCapture.h:209-216`, consumed once at `src/core/RetroAudioCapture.cpp:283-285`) makes the next
  `writeRetroAudioTake()` fail its flush/close as a full disk does. The take itself is driven period by period with
  the audio device stopped (`initTestCase` calls `audioDev()->stopProcessing()`), the destination is a
  `QTemporaryDir`, and there are no sleeps, timers or wall-clock assertions; the test checks recorder states, file
  existence, the cell and the WAV's PCM.
- **Cause and callers.** The only product caller of `SessionAudioRecorder::takeFinished()`/`finishedTake()` is
  `writeAndLand()` (`rg -n 'takeFinished|finishedTake' src include`); the command path is
  `landSlot()` -> `writeAndLand()` and the other two session commands do not touch the take. The unit test file
  `tests/src/core/SessionAudioRecorderTest.cpp` still expects `takeFinished()` to release (lines 100-108, 123, 144)
  and passes unchanged, so the existing contract is kept and the new `finishedTake()` is additive.
- **Siblings in the changed command file.** `src/core/ControlCommandsSessionAudioRecord.cpp` registers exactly three
  commands: `session.record_slot`, `session.record_slot_status`, `session.record_slot_land`. `record_slot` and
  `record_slot_status` never take the buffer (arm/launch and read-only state) and are unchanged; `record_slot_land`
  is the fixed one. The sibling *family* `record.sampler_capture_land` already had this contract
  (`src/core/ControlCommandsSamplerTake.cpp:320-343` peeks and releases only after a successful write), and the fix
  now matches it. The ASK's `app.*` and `arrangement.*` ids are not declared in any file this commit changes - that
  list comes from the branch-wide diff - and no command was added, removed or re-registered (the
  ControlCommandsSnapshot canary is untouched).
- **No collateral damage.** No saved-project field, schema, limit or check changed. The new path only differs when
  `writeRetroAudioTake()` fails: it now returns with the recorder Done instead of Idle and with a longer refusal
  message; success, frames written, the cell edit and the single undo step are identical. Save/reload of a landed
  take is covered by `aSyncedTakeKeepsItsLengthAndAlignmentAfterSaveAndReload`, and the existing filled-cell refusal
  by `aLandingNeverOverwritesACellFilledWhileItRecorded`; both pass in `logs/test-6.log`.
- **Realtime.** No audio-thread path changed. `finishedTake()` is a control-thread call (class comment
  `include/SessionAudioRecorder.h:57-63`), `m_finished` is touched only there, and `push()`, `slotStarted()`,
  `slotStopped()` are untouched; the existing allocation probe
  `SessionAudioRecorderTest::theAudioThreadPathDoesNotAllocate` passes (`logs/test-6.log`).
- **Undo.** One honest step, unchanged: `addUndoStep(...)` runs once, only after a successful write, and the
  undo/redo round trip is asserted by `landAndCompare()` in the new test.
- **Verdict evidence.** The verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so the code-reference question
  does not apply; the base defect is cited above (`takeFinished()` before the write at the base
  `src/core/ControlCommandsSessionAudioRecord.cpp:284` and `src/core/SessionAudioRecorder.cpp:84-101`).
- **Registration.** All three changed product files were already in `tests/fork-sources.txt`
  (`include/SessionAudioRecorder.h:1381`, `src/core/ControlCommandsSessionAudioRecord.cpp:1894`,
  `src/core/SessionAudioRecorder.cpp:2228`), the test file is an existing registered test, and the `fork-sources`
  gate passes (`logs/gates-1.log`).

## Other findings

- `render.realtime` (`src/core/ControlCommandsRenderRealtime.cpp:300-307`) also calls `takeFinished()` before
  writing and loses the capture if `writeWav` fails. That is a render that can be run again from the project, not a
  performance that only exists in the recorder, so it was left unchanged here.
- `ControlSessionTransitions` failed once in a tree where four sibling test binaries had never been built
  (`logs/test-5.log`) and passed after building them (`logs/test-6.log`, `logs/test-7.log`). No product cause was
  found; it is recorded here rather than fixed as unrelated.
