FIXED

# Cluster 5465 — Punch gating maps a capture batch to the wrong song interval

## Verdict

REAL, fixed at the cause in `src/core/AudioEngine.cpp:903-919`.

`AudioEngine::drainCaptureStages()` (`src/core/AudioEngine.cpp:860-903`) drains the whole
staging ring, bounded only by `InputStageCapacityFrames` (16384), because a capture thread that
ran ahead of the render thread must not lose frames: one render can therefore receive a batch
of more than one engine period. `AudioEngine::punchWindow(frames)` then passed the whole batch
to `punchFramesForPeriod()` from `m_periodStartFrame`, i.e. it treated the first frame of the
backlog as if it were the first frame of the period being rendered. A punch-in or punch-out
inside such a batch therefore cut the wrong period: the older, already-passed frames were gated
as if they were now, and the period actually being rendered was cut as if it were later.

The batch ends at the end of the period being rendered. Its last `m_framesPerPeriod` frames are
this period's input; the frames before them are the periods the render missed. `punchWindow()`
now places the batch's first frame that many frames before `m_periodStartFrame` (and leaves a
one-period batch exactly as before):

```cpp
const double batchStartFrame = frames > m_framesPerPeriod
	? m_periodStartFrame + static_cast<double>(m_framesPerPeriod) - static_cast<double>(frames)
	: m_periodStartFrame;
```

The subtraction is done in `double`, not in the unsigned `f_cnt_t` domain, so an oversized
batch does not wrap.

## Callers and siblings

`punchWindow()` is the one place a capture batch is mapped to the song timeline. Its callers
all receive the same drained batch and are fixed together:

- `src/core/AudioEngine.cpp:1266` — the N-channel (wide) recorder path.
- `src/core/AudioEngine.cpp:1275` — the stereo recorder path.
- `src/core/SampleRecordHandle.cpp:338` — the record-armed clip path (`writePunched()` uses the
  same batch-relative indices).

`punchFramesForPeriod()` itself (`include/PunchWindow.h:57-72`) is unchanged: it already takes
an arbitrary origin and frame count. No project format, command, undo or GUI code changes.

## Reproduction and regression evidence

Regression test:
`tests/src/core/RecordingLatencyTest.cpp:207` —
`anOversizedCaptureBatchIsGatedAtItsOwnSongPosition`.

It stops the Dummy device (so no background render exists), drains any staged input, arms
engine recorder route 0 to a temporary WAV, arms the punch region to ticks `[0, 1)`, puts the
transport at song frame 0, stages a **two-period** batch with distinct dyadic sample values,
and renders exactly one period manually. Only the batch's second period lies in the punch
region, so exactly `framesPerPeriod()` frames must reach the take and they must be batch frames
`[periodFrames, 2*periodFrames)`. Before the fix the take received the batch's first period
(394 frames for 44.1 kHz / 140 BPM; 512 frames at other frame rates), i.e. audio from before
punch-in, and lost the inside period. The test also asserts `inputBufferFrames() == batchFrames`
and `recorder.track(0).framesPushed() == periodFrames`, and reads the take back through
libsndfile to compare every sample.

Every command below was run from the worktree; build and test output is in
`/home/kruzzzzy/.config/kads/zene/state/runs/5465.r1/logs/`.

| Step | Command | Log | EXIT |
|---|---|---|---|
| Baseline build | `cmake --build build --target RecordingLatencyTest -j4` | `build-0-baseline.log` | 0 |
| Baseline test | `ctest -R '^RecordingLatencyTest$' --output-on-failure` (from `build/tests`) | `test-0-baseline.log` | 0 |
| RED with the test, old product code | same build, then same ctest | `build-1-red.log`, `test-1-red.log` | build 0, test **8** |
| GREEN with test and fix | same build, then same ctest | `build-8-green.log`, `test-8-green.log` | build 0, test **0** |

The RED run failed with `Actual (info.frames): 394` / `Expected: 256`, i.e. the take held the
pre-punch period.

4b reversal proof, exactly as required:

```
git diff -- src/core/AudioEngine.cpp > .../logs/fix.patch      EXIT=0
git apply -R .../logs/fix.patch                                 EXIT=0
cmake --build build --target RecordingLatencyTest -j4           EXIT=0   (build-9-red-without-fix.log)
ctest -R '^RecordingLatencyTest$' --output-on-failure           EXIT=8   (test-9-red-without-fix.log)
git apply .../logs/fix.patch                                    EXIT=0
cmake --build build --target RecordingLatencyTest -j4           EXIT=0   (build-10-green-with-fix.log)
ctest -R '^RecordingLatencyTest$' --output-on-failure           EXIT=0   (test-10-green-with-fix.log)
```

The reverted run failed with
`FAIL! ... anOversizedCaptureBatchIsGatedAtItsOwnSongPosition ... Actual (recorder.track(0).framesPushed()): 394, Expected: 256`.
The restored run passed all 8 cases.

Test family after the fix:

```
cmake --build build --target PunchWindowTest MultiTrackRecorderTest RecordingRealtimeTest -j4
    EXIT=0   (build-11-family.log)
ctest -R '^(PunchWindowTest|MultiTrackRecorderTest|RecordingRealtimeTest|RecordingLatencyTest)$'
    EXIT=0   (test-11-family.log, 4/4 passed)
```

Gates:

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>
    EXIT=0   (gates-1.log: RESULT: PASS; complexity and file-length report "no new item in your files")
```

## Realtime, undo and registration

- `punchWindow()` runs on the render thread but adds only one comparison, one subtraction and
  one addition on values already in registers. No allocation, free, lock, wait, log or Qt call
  is added; the function was already `noexcept` and called from the render path.
- No control command, undo step or saved-data format is touched. The fix only changes the
  timeline origin used by the existing gate.
- `src/core/AudioEngine.cpp` is already registered in `tests/upstream-modifications.txt`; its
  cluster 5465 reason was added, and the existing `tests/src/core/RecordingLatencyTest.cpp`
  entry now describes the new regression case. The file still ends with a newline.

## Self-check

1. **Regression test fails reverted and passes restored.** Yes — reverted `git apply -R` run:
   `test-9-red-without-fix.log` EXIT=8 (`framesPushed` 394 vs 256); restored run:
   `test-10-green-with-fix.log` EXIT=0. Commands and exit codes are in the table above.
2. **Deterministic, no wall clock/sleep/race.** The test stops the audio device, drains the
   staging ring, pushes one exact batch with `pushInputFrames()`, and drives exactly one
   `renderNextPeriod()` on the test thread. There is no `QTest::qWait`, sleep or background
   render in the case; the only "seam" is the existing public `pushInputFrames()` +
   `renderNextPeriod()` input path. Sample values are exact dyadic multiples of `1/1024`, so
   the 24-bit read-back comparison has no rounding ambiguity.
3. **Cause and complete.** The cause is the origin passed to `punchFramesForPeriod()`; it is
   fixed in `punchWindow()` (`src/core/AudioEngine.cpp:903-919`), which every caller uses. All
   three call sites were checked (`AudioEngine.cpp:1266`, `:1275`,
   `SampleRecordHandle.cpp:338`). A one-period batch keeps the old origin, so ordinary takes
   are unchanged.
4. **No collateral damage.** The change only applies when the drained batch is larger than one
   engine period; for `frames <= m_framesPerPeriod` the origin is bit-for-bit the previous
   `m_periodStartFrame`. No limit was added and no saved data changed. The existing punch
   loopback test and the recording/multi-track/realtime family still pass (`test-11-family.log`,
   EXIT=0).
5. **ALREADY-FIXED / FALSE-POSITIVE.** Not the verdict: the pre-fix RED run above shows the
   defect. No code-reference closure is claimed.

## Other findings

None. Local TSan is not available on this box; the change adds no synchronisation, so the
existing lock-free contract is unchanged.
