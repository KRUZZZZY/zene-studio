# Cluster 3705 — the wide capture block and the stereo pair could be drained from different blocks

**Verdict: FIXED**

## Finding (Luna's patrol, historical line numbers)

`src/core/audio/AudioDevice.cpp:71` (`publishCaptured`): the publisher pushes the
N-channel (wide) block first and the stereo pair second, while the render thread
drained the two rings separately. A render that landed between the two pushes
consumed one representation now and the other in the next period: the recorder
took the wide block in one period and fell back to the same stereo block in the
next — the same audio twice, and the stereo bus a whole capture block behind the
recorders.

An earlier patrol skipped this as out of scope ("the fix needs cross-ring state").
It is fixed below by making the two representations one block on both ends.

## Evidence that the window was real (before this change)

- `src/core/audio/AudioDevice.cpp:76` (old) — `engine->pushInputFramesWide(...)`,
  the wide block.
- `src/core/audio/AudioDevice.cpp:91` (old) — `engine->pushInputFrames(...)`, the
  stereo pair, in a later statement: two publications of one capture block.
- `src/core/AudioEngine.cpp:1015` + `:1019` (old) — `swapBuffers()` drained the two
  rings in two independent calls, `drainInputStage()` and `drainWideInputStage()`.
- `src/core/AudioEngine.cpp:966-984` (old) — STAGE 4 gives the recorders the wide
  block when the wide stage has frames and the stereo bus otherwise, so the two
  representations of one block were consumed by different periods.
- `src/core/AudioWideInputStage.cpp:87-112` (old) — the wide stage drained
  `min(staged, capacity)` frames, i.e. as much as it held, with no relation to the
  stereo ring.

The two rings are separate SPSC buffers with separate drop counters, so nothing
in the tree tied the two representations of one capture block together.

## The fix (at the cause, on both ends of the block)

1. **One block, two representations — the publisher side**
   `src/core/audio/AudioDevice.cpp:80-113` (`publishCaptured`):
   - the wide path is published first and **leads**: `pushInputFramesWide()` now
     returns the frames the wide stage actually took (`src/core/AudioEngine.cpp:692-709`),
     and the stereo pair is published for **exactly** those frames
     (`src/core/audio/AudioDevice.cpp:102-113`, `accepted`), so the two rings hold
     the same frames of the same block;
   - when the wide path refuses a block whole (a width it cannot carry, or a
     width change while frames of the old width are still staged), the block goes
     to **neither** path (`src/core/audio/AudioDevice.cpp:85-95`) instead of to the
     stereo path alone. The wide path counts it as dropped, and the capture
     counter still moves by the delivered frames, exactly as before.

2. **One block, two representations — the drain side**
   `src/core/AudioEngine.cpp:626-667` (`drainCaptureStages`, replacing
   `drainInputStage()` + `drainWideInputStage()`, called from `swapBuffers()`
   at `src/core/AudioEngine.cpp:1036`): the two stages are drained **as one
   block** — as many frames as *both* of them hold. A block whose stereo pair has
   not been published yet waits for it instead of being consumed on its own, so a
   render can no longer take the wide block in one period and the stereo pair in
   the next. The wide stage's drain takes that same frame count
   (`include/AudioWideInputStage.h:98`, `src/core/AudioWideInputStage.cpp:87-115`);
   the frames that are not taken stay in its ring for the next period.

3. **A capture path that does not use the wide path is untouched**
   `include/AudioEngine.h:693` (`m_captureWideInUse`) is set by the first
   `pushInputFramesWide()` and cleared by every device (re)start
   (`include/AudioEngine.h:461-465`, `AudioDevice::startProcessing` →
   `resetInputStream()`). While it is clear — the Dummy loopback, a device with
   no capture path — the stereo ring drains on its own, exactly as it always did.

The recorder's own rule (STAGE 4: the wide block when the wide stage has frames,
the stereo bus otherwise) now sees both representations of one block in the same
period, so a take can no longer alternate between the two.

## The regression (deterministic, no timing, no sleeps)

`tests/src/core/CapturePublisherTest.cpp` — two new cases:

- `theWideBlockAndItsStereoPairAreDrainedAsOneBlock` — the capture thread is
  preempted between the two pushes (the wide block is published, its stereo pair
  is not). The render must take **neither** representation, and after the stereo
  pair of the same block arrives it must take **both**.
- `aBlockTheWidePathRefusesIsNotPublishedToTheStereoPathAlone` — a block the
  wide path refuses whole (a width change while frames are still staged) must
  not reach the stereo path alone.

Both drive the engine's two publication entry points directly, so the interleaving
is exact: no wall-clock, no sleep, no race.

## Commands run (this session)

All logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3705.r1/logs`.

RED — the regression on the original product code:

```
cmake --build build --target CapturePublisherTest -j 8            > logs/build-red-1.log 2>&1   EXIT=0
cd build/tests && ctest -R '^CapturePublisherTest$' --output-on-failure > logs/test-red-1.log 2>&1   EXIT=8
```

`test-red-1.log`: the two new cases fail —
`inputWideFrames(): 64` vs `0` (the wide block was consumed on its own) and
`inputFramesStaged(): 64` vs `0` (the refused block reached the stereo path alone).

GREEN — with the fix:

```
cmake --build build --target CapturePublisherTest -j 8            > logs/build-green-1.log 2>&1   EXIT=0
cd build/tests && ctest -R '^CapturePublisherTest$' --output-on-failure > logs/test-green-1.log 2>&1   EXIT=0
```

The proof that the test catches the bug (step 4b) — the product patch only, the
test left in place:

```
git diff -- src/core/audio/AudioDevice.cpp src/core/AudioEngine.cpp include/AudioEngine.h \
          include/AudioWideInputStage.h src/core/AudioWideInputStage.cpp > logs/fix.patch   (280 lines)
git apply -R logs/fix.patch                                                              EXIT=0
cmake --build build --target CapturePublisherTest -j 8            > logs/build-revert-1.log 2>&1   BUILD_EXIT=0
cd build/tests && ctest -R '^CapturePublisherTest$' --output-on-failure > logs/test-revert-1.log 2>&1   TEST_EXIT=8
git apply logs/fix.patch                                                                  APPLY_EXIT=0
cmake --build build --target CapturePublisherTest -j 8            > logs/build-reapply-1.log 2>&1   BUILD_EXIT=0
cd build/tests && ctest -R '^CapturePublisherTest$' --output-on-failure > logs/test-reapply-1.log 2>&1   TEST_EXIT=0
```

`test-revert-1.log` fails with the same two assertions as the RED run; with the
patch re-applied it passes.

The affected family (14 executables, all built from the fixed tree):

```
cmake --build build --target zene RecordingRealtimeTest RecordingInputPathTest InputMonitorTest \
    RecordingLatencyTest InputChannelRingTest MultiTrackRecorderTest RetroAudioCaptureTest \
    HardwareIoTest DummyPacingTest VcaRefreshAtomicityTest TripleOscillatorUserWaveTest \
    SessionAudioRecordTest -j 8                                    > logs/build-family-2.log 2>&1   EXIT=0
cd build/tests && ctest -R '^(CapturePublisherTest|RecordingRealtimeTest|RecordingInputPathTest|InputMonitorTest|RecordingLatencyTest|InputChannelRingTest|MultiTrackRecorderTest|RetroAudioCaptureTest|HardwareIoTest|DummyPacingTest|VcaRefreshAtomicityTest|TripleOscillatorUserWaveTest|SessionAudioRecordTest|RtSafetySweep)$' \
    --output-on-failure                                              > logs/test-family-2.log 2>&1   EXIT=0
```

`test-family-2.log`: **14 of 14 passed** (the three tests the task named —
`DummyPacingTest`, `TripleOscillatorUserWaveTest`, `VcaRefreshAtomicityTest` —
are in that set and pass; `RtSafetySweep` passes with the renamed audio-thread
entry in `tests/rt-safety-scope.txt`).

## Gates

```
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py \
    /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3705 \
    > logs/gate-1.log 2>&1   EXIT=0
```

```
feature-check zene-qwen-3705 (base b61e5f965, 8 changed files)
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

## Collateral behaviour

- Nothing outside the capture block changed: the stereo bus, the retro window,
  the input meter and the tuner read the same buffers as before; the Dummy
  loopback (a stereo-ring-only producer) drains exactly as it did.
- A block the wide path refuses whole no longer reaches the stereo bus. Before
  the fix it did — and that is the bug: the recorder (wide path) and the bus
  (stereo path) were then one block apart. The block is now dropped from both
  and counted by the wide path's drop counter.
- The stereo bus can now lag the wide path by at most the block the capture
  thread had not finished publishing; it never carries a *different* block than
  the recorders.
- No project/save format, no control command, no undo step and no command
  surface was touched.
- Realtime: the new code is one relaxed atomic load, two ring reads and one
  ring read with a limit — no allocation, no lock, no logging, no growing
  container on the render or the capture thread. The declaration of the
  audio-thread capture drain moved from the two former entries to
  `AudioEngine::drainCaptureStages` in `tests/rt-safety-scope.txt`.

## Files committed

- `src/core/audio/AudioDevice.cpp` — `publishCaptured`: one block, two
  representations.
- `src/core/AudioEngine.cpp` — `drainCaptureStages()` (the paired drain),
  `pushInputFramesWide()` returns the frames taken and publishes the flag.
- `include/AudioEngine.h` — the declarations of the above, `m_captureWideInUse`,
  `resetInputStream()` clears the flag.
- `include/AudioWideInputStage.h`, `src/core/AudioWideInputStage.cpp` —
  `drain(maxFrames)`.
- `tests/src/core/CapturePublisherTest.cpp` — the regression.
- `tests/rt-safety-scope.txt` — the renamed audio-thread entry.
- `tests/upstream-modifications.txt` — the divergence ledger entries.
- `docs/qwen-fixes-2026-10/cluster-3705.md` — this report.
