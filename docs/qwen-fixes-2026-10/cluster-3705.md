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
    > logs/gate-1.log 2>&1   EXIT=0     (8 changed files, before this report was added)
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py \
    /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3705 \
    > logs/gate-2.log 2>&1   EXIT=0     (9 changed files, the final tree)
```

```
feature-check zene-qwen-3705 (base b61e5f965, 9 changed files; logs in /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3705-feature-check)
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

The pre-submit check passes as well:

```
python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py \
    /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3705 3705 \
    > logs/submit-check-2.log 2>&1   EXIT=0
RESULT: PASS - submit
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

## Self-check

Every item the pre-submit check asks about, with the evidence this session
produced.

**? The regression test FAILS with your product change reverted and PASSES with
it (exit codes).**
Reverted: `git apply -R logs/fix.patch` (EXIT=0) → `cmake --build build --target
CapturePublisherTest -j 8` (BUILD_EXIT=0) → `ctest -R '^CapturePublisherTest$'`
(**TEST_EXIT=8**, `logs/test-revert-1.log`: the two new cases fail with
`inputWideFrames(): 64` vs `0` and `inputFramesStaged(): 64` vs `0`).
Re-applied: `git apply logs/fix.patch` (APPLY_EXIT=0) → rebuild (BUILD_EXIT=0) →
`ctest -R '^CapturePublisherTest$'` (**TEST_EXIT=0**, `logs/test-reapply-1.log`).
The same two cases failed on the untouched tree before the fix
(`logs/test-red-1.log`, EXIT=8) and pass with it (`logs/test-green-1.log`,
EXIT=0). The patch contains the product files only — the test file was never
reverted, so the test is the same binary in both runs.

**? The test is deterministic: no wall-clock timing, sleeps or races (name the
seam or injected values it uses).**
The seam is the pair of publication entry points of the engine itself:
`AudioEngine::pushInputFramesWide()` (the wide block) and
`AudioEngine::pushInputFrames()` (the stereo pair). The test publishes the wide
block, renders once — the state the capture thread is preempted in — and asserts
that **neither** representation was consumed; it then publishes the stereo pair
of that same block with fixed values (`SampleFrame(0.25f, 0.5f)`, 64 frames) and
asserts that **both** are consumed together. Every value is a literal
(`0.25f`, `0.5f`, 64 frames); there is no clock, no sleep, no thread and no
timing assumption in the test — the interleaving is produced by the order of the
calls, not by a race. The second case uses the same seam: a four-channel wide
block that is left undrained, then a two-channel block through the publisher,
whose width change the wide path refuses.

**? The fix is at the cause and complete: every caller and sibling of what you
changed was checked and stays consistent.**
- `pushInputFramesWide()` has exactly one caller,
  `AudioDevice::publishCaptured` (`src/core/audio/AudioDevice.cpp:80`); the
  return type changed to `std::size_t` in the declaration
  (`include/AudioEngine.h:415`) and the definition
  (`src/core/AudioEngine.cpp:692`) together, and no other call site exists
  (`grep -rn pushInputFramesWide src include tests`).
- Every backend that captures goes through that one publisher:
  `src/core/audio/AudioAlsa.cpp:618`, `src/core/audio/AudioJack.cpp:475`,
  `src/core/audio/AudioSdl.cpp:194`, `src/core/audio/AudioPortAudio.cpp:228` —
  no backend publishes to the wide path on its own, so the pairing covers all of
  them.
- The other producer of the stereo ring, `AudioEngine::pushInputFrames()`, is
  unchanged: it is used by the Dummy loopback
  (`src/core/AudioEngine.cpp:1883`) and by tests, and it never sets the wide
  flag, so that path drains exactly as before.
- The two drains that were merged are declared and defined together
  (`include/AudioEngine.h:696`, `src/core/AudioEngine.cpp:626`), and the only
  caller, `swapBuffers()` (`src/core/AudioEngine.cpp:1036`), was updated.
- `AudioWideInputStage::drain()` gained a limit; the declaration
  (`include/AudioWideInputStage.h:98`) and the definition
  (`src/core/AudioWideInputStage.cpp:87`) changed together, and the direct caller
  in `tests/src/core/CapturePublisherTest.cpp` keeps working through the default
  argument.
- The recorder's own choice (STAGE 4, `src/core/AudioEngine.cpp:966-984`:
  the wide block when the wide stage has frames, the stereo bus otherwise) was
  checked and left as it is: with the paired drain the two representations of one
  block are always consumed in the same period, so the rule can no longer
  alternate between them mid-take.
- `tests/rt-safety-scope.txt` names the audio-thread drain; the two entries for
  the functions that were merged were replaced by the one that exists now, so
  the declared audio-thread frontier still covers the code (the `RtSafetySweep`
  ctest passes).

**? No collateral damage: old projects and saved data still load as before, and
any limit or check you added does not hit other callers that legitimately exceed
it.**
No project, save, undo or command surface was touched: the diff is confined to
the capture path, one test file, the RT-safety scope file and the divergence
ledger (`git show --stat 9102db166`). No new control command was added, so the
`ControlCommandsSnapshot` canary does not move. The new limits are internal to
the capture path: the wide stage's drain limit is the number of frames the
stereo ring holds, which is bounded by the same `InputStageCapacityFrames`
(16384) the ring and the destination buffer already use, so no caller can
exceed it; the publisher's `accepted` count is what the wide stage actually
took, so it can only be smaller than the block, never larger. A capture path
that does not use the wide path is unaffected (the `m_captureWideInUse` flag is
clear for it, and the stereo ring drains on its own — the `DummyPacingTest`,
`InputMonitorTest`, `HardwareIoTest` and `SessionAudioRecordTest` cases in the
family run below all exercise that path). The one deliberate behaviour change is
named in the report: a block the wide path refuses whole no longer reaches the
stereo bus alone, because that is the misalignment itself; it is counted by the
wide path's drop counter.

**? If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or
the intended contract that shows it.**
Not applicable: the verdict is **FIXED**, and the "before" evidence is quoted
above with file:line (`src/core/audio/AudioDevice.cpp:76` and `:91` (old) for
the two publications, `src/core/AudioEngine.cpp:1015`/`:1019` (old) for the two
independent drains, `src/core/AudioEngine.cpp:966-984` (old) for the recorder's
choice between the two representations). The intended contract is
`docs/RECORD-INPUTS.md` §4: "the N captured channels reach the recorders through
the wide path, and the bus carries the configured pair" — the two are two
representations of the same capture, which is what the fix makes true at the
drain as well as at the publication.

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

## Independent QA follow-up (2026-10-20)

The paired drain also had to bound its non-wide-mode drain by the staged-frame
snapshot. If render observed no wide frames and the capture thread published the
first wide block before the drain call, an unbounded drain could consume that
new block alone while the mode flag still read false. `drainCaptureStages()` now
passes the observed `wideStaged` count as the limit in that branch. The added
`aWideBlockPublishedAfterTheSnapshotWaitsForTheNextDrain` case checks the
snapshot/publish/drain ordering without a timing assumption.

QA verification:

```
cmake --build build --target zene CapturePublisherTest RecordingRealtimeTest RecordingInputPathTest InputMonitorTest RecordingLatencyTest InputChannelRingTest MultiTrackRecorderTest RetroAudioCaptureTest HardwareIoTest DummyPacingTest VcaRefreshAtomicityTest TripleOscillatorUserWaveTest SessionAudioRecordTest -j 12
EXIT=0

cd build/tests && ctest -R '^(CapturePublisherTest|RecordingRealtimeTest|RecordingInputPathTest|InputMonitorTest|RecordingLatencyTest|InputChannelRingTest|MultiTrackRecorderTest|RetroAudioCaptureTest|HardwareIoTest|DummyPacingTest|VcaRefreshAtomicityTest|TripleOscillatorUserWaveTest|SessionAudioRecordTest|RtSafetySweep)$' --output-on-failure
EXIT=0 (14/14 passed)

python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3705
EXIT=0
```

The original RED/restore proof was already captured by the fix loop at
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3705.r1/negctl` and was
not repeated during QA.
