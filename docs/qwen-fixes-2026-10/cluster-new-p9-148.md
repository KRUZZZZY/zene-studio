ALREADY-FIXED

# cluster new-p9-148 - granular range changes resize the ring buffer on the audio thread

## Verdict

ALREADY-FIXED on branch `bugs/qwen-new-p9-148`.  The bug described by the
finding (a plain `bool` update flag written by the controls path and a ring
buffer resize performed by `processImpl()` on the audio thread) existed in
the pre-fix code, but was replaced by an off-audio-thread prepared-buffer
handoff in commit `b3a4d9d7c3` ("fix(L10): prepare granular buffers off the
audio thread (cluster Q050-005)"), with two follow-ups
(`1d29c8b3b0`, `284740ac67`) that reclaim retired buffers on a control-thread
timer.  No product code was changed in this session.

## Pre-fix code (the bug the finding names)

From `git show b3a4d9d7c3^:plugins/GranularPitchShifter/GranularPitchShifterEffect.h`
(the historical line numbers in the finding match this revision exactly):

- `GranularPitchShifterEffect.h:107`:
  `void sampleRateNeedsUpdate() { m_sampleRateNeedsUpdate = true; }` - a plain
  `bool` written from the controls path.
- `GranularPitchShifterEffect.h:109`: `void changeSampleRate();`
- `GranularPitchShifterEffect.h:178`: `bool m_sampleRateNeedsUpdate = false;`

From `git show b3a4d9d7c3^:plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp`:

- `:257-260` - the audio thread reads and clears the flag in `processImpl()`:
  `if (m_sampleRateNeedsUpdate) { m_sampleRateNeedsUpdate = false;
  changeSampleRate(); }`
- `:266` - `void GranularPitchShifterEffect::changeSampleRate()`
- `:275` - `m_ringBuf.resize(m_ringBufLength);` executed on the audio thread.

This is exactly the reported data race and audio-thread allocation/free.

## Current code (the fix that is already present)

- `plugins/GranularPitchShifter/GranularPitchShifterEffect.h:111-112` -
  `sampleRateNeedsUpdate()` is now an off-thread *preparation* request, not a
  shared boolean.  There is no `changeSampleRate()` and no
  `m_sampleRateNeedsUpdate` member left anywhere in the plugin.
- `plugins/GranularPitchShifter/GranularPitchShifterEffect.h:158-167` -
  `PreparedBuffers` owns a complete replacement ring buffer plus the derived
  sample rate, nyquist, DC coefficient and pitch speed values.
- `plugins/GranularPitchShifter/GranularPitchShifterEffect.h:177-180` -
  `std::atomic<PreparedBuffers*> m_preparedBuffers`,
  `std::array<std::atomic<PreparedBuffers*>, 2> m_retiredBuffers`, and
  `m_retiredPending` are the only cross-thread state.
- `plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp:285-310` -
  `sampleRateNeedsUpdate()` allocates and zero-fills the replacement vector on
  the calling (control/GUI) thread and publishes it with a relaxed-free
  `exchange`; the previous prepared object is deleted on that same thread.
- `plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp:313-344` -
  `applyPreparedBuffers()` runs at the start of `processImpl()` and only
  exchanges atomics, `swap`s two vectors (no allocation/free), resets scalar
  state and retires the old object.  It never deletes anything.
- `plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp:349-361` -
  retired buffers are destroyed only by `reclaimRetiredBuffers()` on the
  control thread, driven by the `granularRetiredBufferReclaimTimer` created in
  the constructor (`:73-80`) or by the next `sampleRateNeedsUpdate()`.
- `plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp:58-69,95-101` -
  the grain vector is reserved for `MaxGrains` in the constructor, and
  `spawnGrain()` never grows past the reservation, so grain churn on the audio
  thread neither allocates nor frees.
- Callers of `sampleRateNeedsUpdate()` are all control-thread paths:
  constructor (`:70`), the queued `AudioEngine::sampleRateChanged` connection
  (`:71-72`), and `GranularPitchShifterControls::updateRange()`
  (`GranularPitchShifterControls.cpp:64,67,104`, reached from
  `loadSettings()` and a UI range change).

The existing regression test
`tests/src/core/GranularPitchShifterRealtimeTest.cpp:194`
(`sampleRateHandoffDoesNotAllocateAndKeepsPitchShift`) drives the real control
path (a persisted range change through `loadSettings()`), renders on a
separate audio thread, and fails if the audio callback allocates or frees.

## RED/GREEN proof run in this session

I temporarily restored the exact pre-fix product files (revision
`b3a4d9d7c3^`) for the two product files, rebuilt the plugin and the test,
and ran the existing regression test; then I restored HEAD and reran it.  The
test failed without the fix and passed with it.

```text
# save pre-fix sources outside the worktree
git show b3a4d9d7c3^:plugins/GranularPitchShifter/GranularPitchShifterEffect.h > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-148.r1/logs/pre-fix-GranularPitchShifterEffect.h          EXIT=0
git show b3a4d9d7c3^:plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-148.r1/logs/pre-fix-GranularPitchShifterEffect.cpp        EXIT=0

# put the pre-fix product code in the worktree and capture the reversal
cp /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-148.r1/logs/pre-fix-GranularPitchShifterEffect.h plugins/GranularPitchShifter/GranularPitchShifterEffect.h                                                     EXIT=0
cp /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-148.r1/logs/pre-fix-GranularPitchShifterEffect.cpp plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp                                                 EXIT=0
git diff -- plugins/GranularPitchShifter/GranularPitchShifterEffect.h plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-148.r1/logs/revert-to-prefix.patch                                                       EXIT=0

# build pre-fix code
cmake --build build --target GranularPitchShifterRealtimeTest -j4 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-148.r1/logs/build-2-red.log 2>&1                                                                                                                                                                                                   EXIT=0

# RED: run the regression test against the pre-fix code
cd build/tests && ./GranularPitchShifterRealtimeTest sampleRateHandoffDoesNotAllocateAndKeepsPitchShift > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-148.r1/logs/test-2-red.log 2>&1                                                                                                                                                                   EXIT=1
```

RED output (`logs/test-2-red.log`):

```text
FAIL!  : GranularPitchShifterRealtimeTest::sampleRateHandoffDoesNotAllocateAndKeepsPitchShift() Compared values are not the same
   Actual   (state.callbackAllocations): 1
   Expected (std::uint64_t{0})         : 0
   Loc: [../../tests/src/core/GranularPitchShifterRealtimeTest.cpp(235)]
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 482ms
```

The one allocation inside the audio callback is the ring-buffer resize that
the finding describes.

```text
# restore HEAD product code
git apply -R /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-148.r1/logs/revert-to-prefix.patch   EXIT=0
git status --short -- plugins/GranularPitchShifter                                                    (no output)

# rebuild with the fix
cmake --build build --target GranularPitchShifterRealtimeTest -j4 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-148.r1/logs/build-3-green.log 2>&1   EXIT=0
# build-3 rebuilds both plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp and the test, then links libgranularpitchshifter.so.

# GREEN: same test, fixed code
cd build/tests && ./GranularPitchShifterRealtimeTest sampleRateHandoffDoesNotAllocateAndKeepsPitchShift > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-148.r1/logs/test-3-green.log 2>&1   EXIT=0
```

GREEN output (`logs/test-3-green.log`):

```text
QINFO  : GranularPitchShifterRealtimeTest::sampleRateHandoffDoesNotAllocateAndKeepsPitchShift() Granular rate handoff: callback allocations=0, frees=0, max callback=14340 ns, measured pitch=464.444 Hz (expected 466.164 Hz)
PASS   : GranularPitchShifterRealtimeTest::sampleRateHandoffDoesNotAllocateAndKeepsPitchShift()
Totals: 3 passed, 0 failed, 0 skipped, 0 blacklisted, 485ms
```

Whole test family after restoring the fix:

```text
cd build/tests && ctest -R GranularPitchShifterRealtimeTest --output-on-failure > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-148.r1/logs/test-4-family.log 2>&1   EXIT=0
1/1 Test #805: GranularPitchShifterRealtimeTest ...   Passed    0.56 sec
100% tests passed out of 1
```

Baseline build/run before the experiment (HEAD clean):

```text
cmake --build build --target GranularPitchShifterRealtimeTest -j4 > .../logs/build-1.log 2>&1                 EXIT=0
cd build/tests && ctest -R GranularPitchShifterRealtimeTest --output-on-failure > .../logs/test-1.log 2>&1     EXIT=0
1/1 Test #805: GranularPitchShifterRealtimeTest ...   Passed    0.60 sec
```

## Gates

```text
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-148 > /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-148.r1/logs/gates-1.log 2>&1   EXIT=0
```

`logs/gates-1.log`:

```text
feature-check kads-new-p9-148 (base fa4a5f2e7, 349 changed files; logs in .../build/feature-check)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

## Self-check

**1. The regression test FAILS with your product change reverted and PASSES with it (exit codes).**
Yes, seen in this session.  With the pre-fix product files
(`b3a4d9d7c3^`) built in, the test exits 1 with
`callbackAllocations: 1` (expected 0) at
`tests/src/core/GranularPitchShifterRealtimeTest.cpp:235`; with HEAD restored
and rebuilt it exits 0 with `callback allocations=0, frees=0`.  Commands and
exit codes are in the RED/GREEN section above.

**2. The test is deterministic: no wall-clock timing, sleeps or races (name the seam or injected values it uses).**
The pass/fail assertion is the allocation-probe counters
(`test::tlAllocationCount` / `test::tlFreeCount` around a `RealtimeScope`
in `renderAudioCallbacks`, `GranularPitchShifterRealtimeTest.cpp:113-119,
140-141,235-236`), not elapsed time.  The audio rate is injected through
`FixedRateAudioDevice::setSampleRateForTesting` and the range change through a
literal saved `<range value="1"/>` document, so the rate/range sequence is
fixed.  `steady_clock` appears only in bounded wait loops used to detect a
hang (5 s warm-up / 10 s capture) and in a 100 ms upper-bound sanity check on
one callback; the sample-rate handoff itself is observed through the
`captureComplete` flag.  The test still uses a real thread, as the property
being tested is "no allocation on the render thread"; the assertion is
counter-based and does not depend on scheduling order, only that at least one
block is processed after the change.  (This test was written with the original
fix; I did not modify it.)

**3. The fix is at the cause and complete: every caller and sibling of what you changed was checked and stays consistent.**
No code was changed in this session.  For the existing fix I checked every
caller of `sampleRateNeedsUpdate()`: constructor
(`GranularPitchShifterEffect.cpp:70`), queued `sampleRateChanged` connection
(`:71-72`), and `GranularPitchShifterControls::updateRange()`
(`GranularPitchShifterControls.cpp:64,67,104`), the latter reached from both
`loadSettings()` and the UI combo box.  All run on the control/GUI thread and
only publish a prepared buffer.  On the audio side, `processImpl()` calls
`applyPreparedBuffers()` (`:107`), which uses atomic exchange plus
`std::vector::swap` and never deletes; disposal happens on the control thread
via the timer or the next update.  Grain storage is reserved to `MaxGrains`
in the constructor and `spawnGrain()` refuses to grow past it (`:58-59,69,
95-101`).  No `changeSampleRate()` / `m_sampleRateNeedsUpdate` / audio-thread
`m_ringBuf.resize()` remains (`rg` over the plugin returned none).

**4. No collateral damage: old projects and saved data still load as before, and any limit or check you added does not hit other callers that legitimately exceed it.**
No product code changed, so save/load and old projects are untouched.  The
range model still accepts the historical values 0-4; an additional follow-up
clamps malformed out-of-range saved values at the control model
(`savedOutOfRangeValueIsBoundedByTheControlModel`, passing in the family run)
without changing valid projects.  The prepared-buffer handoff preserves
`m_sampleRate`, `m_nyquist`, `m_ringBufLength`, `m_dcCoeff`, `m_truePitch` and
`m_speed`, so rendering after the handoff is the same algorithm at the new
rate.

**5. If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the intended contract that shows it.**
`plugins/GranularPitchShifter/GranularPitchShifterEffect.h:111-112,158-167,
177-180` and `plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp:58-59,
69,73-80,107,285-361` show the prepared-buffer handoff; the pre-fix code at
`b3a4d9d7c3^` (`...Effect.h:107,109,178`;
`...Effect.cpp:257-260,266,275`) shows the exact bug.  The fix commit is
`b3a4d9d7c3`, with control-thread reclamation follow-ups `1d29c8b3b0` and
`284740ac67`.  The RED/GREEN experiment above shows the existing test
distinguishes the two revisions.

**6. Sibling commands in the command files you changed - confirm each is consistent with your fix (or say why it is not affected).**
This unit changed no command files and no agent command.  `submit_check.py`
lists `app.*`, `arrangement.*` siblings only because it diffs the branch
against `050/m0-train-35`, so other lanes' commits between that base and HEAD
appear in the diff; none of those files were touched by this session or by
the granular fix (`git log --oneline fa4a5f2e7..HEAD -- tools/mcp-zene-control`
shows other lanes' work, and the granular fix commit `b3a4d9d7c3` touched only
the plugin, tests and registries).  No command semantics are affected.

**TSan note.**  Local TSan does not run on this box, so the race freedom is
argued from the design: the only cross-thread values are
`std::atomic<PreparedBuffers*>` slots with acquire/release ordering, the
prepared object is exclusively owned by the control thread until the exchange
and by the audio thread after it, and the retired object is exclusively owned
by the control thread after the swap.  No plain flag is shared any more.

## Other findings

- The existing test's `maxCallbackNanoseconds < 100000000` assertion is a
  wall-clock ceiling (very loose, 100 ms).  It is not the mechanism that
  catches this bug (the allocation counters are), but it is a timing
  assertion.  Not changed here because it is outside this finding and
  weakening or rewriting an existing test is out of scope.
- `sampleRateNeedsUpdate()` still (correctly) allocates a large vector on the
  control thread; this can make a range change on a 120 s buffer briefly block
  the GUI thread, but it is off the audio thread by design and is not a
  realtime defect.
