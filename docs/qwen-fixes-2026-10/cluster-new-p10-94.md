FIXED

# Cluster new-p10-94: refuse an impulse the resampler cannot convert

Product fix: commit `429eaf572a` (`plugins/ConvolutionReverb/ConvolutionReverb.cpp`).
This cluster adds the regression tests in `tests/src/plugins/ConvolutionFftwPlanScopeTest.cpp` and this report.

## Evidence

### 1. The failure and the fix

- **Where the exception starts.** `AudioResampler.cpp:477-480` throws `std::runtime_error` when `src_process` fails. `resampleBuffer` (`src/core/SampleBufferResample.cpp:58`) has no try/catch in its body (lines 58-80 read), so the exception goes up to its caller.
- **Before (`429eaf572a~1`, `ConvolutionReverb.cpp:103-114`).** `atRate` called `resampleBuffer` with no try. It returned the result only when it was non-null and non-empty, and otherwise returned `source`, the decoded buffer at its original rate. So a failure produced the source at the wrong rate, and an exception thrown by the resampler escaped `atRate`.
- **After (`429eaf572a`).**
  - `atRate` (`ConvolutionReverb.cpp:105`) catches `std::exception` at line 120 around `resampleBuffer` (`std::bad_alloc` included) and returns `nullptr` (line 122).
  - An empty result while frames were expected returns `nullptr` (line 126).
  - An empty result when `expected == 0` keeps the one-frame fallback and returns `source` (line 125).
  - `loadImpulse` returns `false` on `nullptr` (`ConvolutionReverb.cpp:208`), before the channel buffers are built. The previous impulse and kernel stay in place.
- **What the code can actually see.** A null return from `resampleBuffer` is not reachable from `atRate`. `resampleBuffer` returns `input` unchanged in its early return (`SampleBufferResample.cpp:67`) and otherwise returns a new buffer, and `atRate` only passes it a non-null source (`loadImpulse` returns earlier on `decoded == nullptr`). The `resampled != nullptr` check is defensive. The real failure modes are: (a) the exception from the resampler; (b) an empty result when frames were expected; (c) `std::bad_alloc`, which is also a `std::exception`.
- **Known limit.** The catch is `std::exception` only. A non-standard throw would still escape `atRate`.

### 2. Tests

Five slots in `tests/src/plugins/ConvolutionFftwPlanScopeTest.cpp` exercise the impulse-load path. This pass added the fifth, `emptyResampleOfAShortImpulseKeepsTheFallback`. Two helpers: `writeWav` (writes a 16-bit stereo WAV) and `tryLoad` (turns an exception from `loadImpulse` into a failure message).

| Slot | Line | What it checks |
|---|---|---|
| `unresamplableImpulseIsRefused` | 191 | A 1 Hz impulse resamples to nothing at the engine rate: it is refused and the state is unchanged. This is the bug-catching slot (RED with the fix reversed, EXIT=8). |
| `veryShortImpulseStillLoads` | 217 | Covers the normal short-resample branch: 2 frames at 2x the engine rate resample to 1 frame (expected 1), which loads. It does not pin the one-frame fallback. |
| `emptyResampleOfAShortImpulseKeepsTheFallback` | 235 | 1 frame at 4x the engine rate resamples to 0 frames (expected 0), so the one-frame fallback keeps it and it loads. This slot pins the fallback (section 3). |
| `validImpulseLoads` | 257 | Positive control: a normal response loads at the engine rate and at another valid rate. |
| `undecodableImpulseIsRefused` | 285 | Body read. The name says "undecodable", but the body uses a missing file (none is written): `loadImpulse` returns false without throwing, and `impulsePath()` and `tailFrames()` are unchanged. It does not test a corrupt file. |

**The one-frame fallback is pinned.** `expected == 0` (`ConvolutionReverb.cpp:125`) is intentional, per its comment. `veryShortImpulseStillLoads` does not reach it: 2 frames at 2x give `expected == 1`, and the resampler returns one frame through the normal branch. `emptyResampleOfAShortImpulseKeepsTheFallback` does reach it: a 1-frame WAV at 4x gives an empty result with `expected == 0`, so `atRate` returns `source`. The slot passes with the product as committed and fails with line 125 deleted (section 3).

The new slot is a behaviour pin. Its RED proof is the mutation, not a run before the fix; it was not run against `429eaf572a~1`.

### 3. Commands and results

Exit codes are from the run record for each step. The steps marked "`EXIT=` line" have that line appended to their log; the earlier logs have none.

| Step | Log (`logs/`) | Result | Exit |
|---|---|---|---|
| Red: new slots before fix | `test-1-red.log` | `Totals: 8 passed, 1 failed` | 8 |
| Red: second run | `test-2-red.log` | `Totals: 8 passed, 1 failed` | 8 |
| Build: test rebuilt with the fix | `build-3.log` | `Built target ConvolutionFftwPlanScopeTest` | not recorded |
| Green | `test-3-green.log` | `100% tests passed out of 1` | 0 |
| Family run (4 tests) | `test-4-family.log` | `100% tests passed out of 4` (4/4) | 0 |
| 4b reverse: build | `build-5-rev.log` | built | 0 (`EXIT=0` line in log) |
| 4b reverse: test | `test-5-red-reversed.log` | `Totals: 8 passed, 1 failed` | 8 (`EXIT=8` line in log) |
| 4b reapply: build | `build-6-reapplied.log` | built | 0 (`EXIT=0` line in log) |
| 4b reapply: test | `test-6-green-reapplied.log` | `100% tests passed out of 1` | 0 (`EXIT=0` line in log) |
| Fallback pin: build, product as committed | `build-7.log` | built, after the cast in section "Disclosures" | 0 (`EXIT=0` line in log) |
| Fallback pin: test, product as committed | `test-7-new.log` | `100% tests passed out of 1` | 0 (`EXIT=0` line in log) |
| Mutation: build, fallback line removed | `build-8-mut.log` | built | 0 (`EXIT=0` line in log) |
| Mutation: test | `test-8-mutated.log` | `FAIL! : emptyResampleOfAShortImpulseKeepsTheFallback() 'loaded' returned FALSE`; `Totals: 9 passed, 1 failed` | 8 (`EXIT=8` line in log) |
| Restored: build | `build-9-restored.log` | built | 0 (`EXIT=0` line in log) |
| Restored: test, verbose | `test-9-restored.log` | all 10 QTest entries PASS (8 slots + initTestCase and cleanupTestCase); `Totals: 10 passed, 0 failed` | 0 (`EXIT=0` line in log) |
| Gates at HEAD | `gates-2.log` | `RESULT: PASS`; no FAIL; complexity and file-length: `no new item in your files` | 0 (`EXIT=0` line in log) |
| Final build, four family targets, at HEAD | `build-10.log` | exit 0 | 0 (`EXIT=0` line in log) |
| Final test, ConvolutionFftwPlanScopeTest, at HEAD | `test-10-final.log` | `100% tests passed out of 1` | 0 (`EXIT=0` line in log) |
| Family test, Convolution, SampleBufferResample, AudioResampler, at HEAD | `test-11-family.log` | `100% tests passed out of 4` (4/4) | 0 (`EXIT=0` line in log) |

Disclosures:

- The 4b reverse/reapply patch was built from commit `429eaf572a`, because the fix was committed before 4b. It was not built from a working-tree diff.
- The first family run exited 8 because three test executables were not built. Its log was overwritten by the passing rerun, so that failure is not in the logs.
- Complexity (CCN) was counted by hand first and then checked by the gate. The gate result is the record. No gate limit was changed.
- TSan was not run.
- Threads: `loadImpulse` is called from the constructor (`ConvolutionReverb.cpp:173`), from `ConvolutionReverbControls::loadSettings` (`:164`), and from the `sampleRateChanged` lambda (`:176`). `atRate` is called only from `loadImpulse` (`:207`). Nothing on the audio thread calls them. The emitting thread of `sampleRateChanged` was not checked.
- Fallback-pin pass, first build: the new slot's comparison of `sampleRate()` (an unsigned `sample_rate_t`) with a signed value failed `-Werror=sign-compare`. The expected value is now cast to `sample_rate_t`. This changes no behaviour.
- Fallback-pin pass, mutation: deleting the fallback line alone did not compile. `expected` (line 114) became unused, and the build treats warnings as errors (`-Werror=unused-variable`). That first mutated build exited 2, and the ctest run after it used the stale binary and exited 0. That run is not evidence and its logs were overwritten. The mutant therefore replaces the deleted line with `(void)expected;`; nothing else differs. The patch is the run's `logs/mutate.patch`, not committed. The product file was restored and equals `429eaf572a` (`git diff HEAD` empty).
- Gates were rerun at HEAD for the final pass: `gates-2.log` (the gate row above). The earlier `gates-1.log` row is superseded.
- For `build-10`, `test-10-final`, `test-11-family`, `gates-2` and `submit-1`, the log files first held only the quiet-runner summary. They were replaced with the full output of the same runs, and the recorded `EXIT=` line was kept.

## Other findings

Not changed in this cluster.

- **Callers ignore the `bool` from `loadImpulse`.** The call in `ConvolutionReverbControls::loadSettings` (`ConvolutionReverb.cpp:164`), the constructor (`:173`), and the `sampleRateChanged` lambda (`:176`) are bare statements. A refused impulse is silent at all three. (An earlier note cited line 151 for `loadSettings`; line 151 is in `saveSettings`. The call is at line 164.)
- **`impulseLoad` reports success from `restored.ok`.** `src/core/ControlCommandsDeviceTools.cpp:349-375` restores the effect state with `controlRestoreEffectState` and returns success when `restored.ok` (lines 370-371). It never checks whether `loadImpulse` accepted the path. I did not trace `controlRestoreEffectState` to confirm that it reaches `loadSettings`. If it does, a refused impulse is reported as a success.

## Self-check

Answers to the five ASK items that `submit_check.py` printed (`logs/submit-1.log`). Paths are given where they are not `plugins/ConvolutionReverb/ConvolutionReverb.cpp`.

1. **Fails with the change reverted, passes with it (exit codes).** Reverted: `logs/test-5-red-reversed.log:11` reads `FAIL! : ConvolutionFftwPlanScopeTest::unresamplableImpulseIsRefused() 'error.isEmpty()' returned FALSE` (loadImpulse threw on a 1 Hz impulse, `SRC ratio outside [1/256, 256]`), and the run exits `EXIT=8` (`:30`). Reapplied: `logs/test-6-green-reapplied.log:8` `EXIT=0`. At HEAD: `logs/test-10-final.log` `EXIT=0` (1/1), and the family run `logs/test-11-family.log` `EXIT=0` (4/4).
2. **Deterministic.** The test file has no sleep, timer, thread or random source: a grep of `tests/src/plugins/ConvolutionFftwPlanScopeTest.cpp` for `sleep|qWait|QElapsedTimer|std::chrono|QTimer|msleep|usleep|QThread|rand|time(` found nothing. The inputs are injected. `writeWav` (`:50`) writes a WAV at a chosen rate into a `QTemporaryDir` (`:193`, `:219`, `:237`, `:259`, `:287`), at 1 Hz, at 2x and 4x the engine rate, and at the engine rate. The engine rate is read from `Engine::audioEngine()->outputSampleRate()` (`:221`, `:239`, `:261`), and the engine is started at `:110-111` with the device stopped. Repeat runs agree: `logs/test-1-red.log:11` and `logs/test-2-red.log:11` give the same failure message.
3. **Fix at the cause; callers and siblings: partly.** The changed unit is `atRate` (`ConvolutionReverb.cpp:105-127`). Its only caller is `loadImpulse` (`:207`), which returns false on a null result (`:208`). Two gaps stay open. First, the three callers of `loadImpulse` ignore its bool: `:164`, `:173` and `:176` are bare calls, so a refused impulse is silent there (see Other findings). Second, the other `resampleBuffer` callers, `src/core/ControlStemSource.cpp:130` and `src/core/RenderQueue.cpp:207`, do not pass through `atRate` and were not changed. I did not check whether they catch the resampler's exception. Not traced: whether `impulseLoad` (`src/core/ControlCommandsDeviceTools.cpp:349-375`) reaches `loadImpulse`.
4. **No collateral damage: partly.** The normal path is unchanged. A non-empty resample is returned as before (`:124`). `veryShortImpulseStillLoads` (`:217`) and `validImpulseLoads` (`:257`) pass: all 10 QTest entries pass in `logs/test-9-restored.log`, and the ctest entry passes at HEAD in `logs/test-10-final.log`. Two changes can reach saved projects, both intended:
   - An impulse that used to throw is now refused, and the previous kernel stays (`unresamplableImpulseIsRefused`, `:191`). Before, the exception escaped `loadImpulse`.
   - An impulse whose resample came back empty while frames were expected used to load as the wrong-rate `source` (`429eaf572a~1`, `ConvolutionReverb.cpp:113`: `return resampled != nullptr && resampled->size() > 0 ? resampled : source;`). Now it is refused (`:126`, `:208`). I did not establish that a real file reaches this case, and no slot tests it.

   The code added is a try/catch and two null returns inside `atRate` (`:120-126`). No limit was added. `loadImpulse` is the only caller of `atRate`.
5. **ALREADY-FIXED or FALSE-POSITIVE.** Not applicable: the verdict is FIXED (line 1 of this report).
