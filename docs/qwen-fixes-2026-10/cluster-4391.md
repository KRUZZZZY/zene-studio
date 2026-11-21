FIXED

# Cluster 4391: RNNoise stereo collapse

RNNoise was fed a mono downmix and its single denoised output was written to
both channels. Hard-panned material bled into the silent channel, and mirrored
stereo cancelled to silence in the downmix, so both channels collapsed.

## Code evidence

Before (`plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp:126-128`):

```cpp
const float monoIn = (buf[i][0] + buf[i][1]) * 0.5f;
m_inputBuf[m_inputCount++] = monoIn * RNNOISE_SCALE_IN;
```

and one denoised value written to both channels (`:146-147`):

```cpp
buf[i][0] = dry[0] * d + denoised * w;
buf[i][1] = dry[1] * d + denoised * w;
```

`plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.h:62` held one
`DenoiseState* m_rnnoiseState` and one output buffer.

After (`plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp:135-137`):

```cpp
m_inputBuf[m_inputCount] = buf[i][0] * RNNOISE_SCALE_IN;
m_inputBufRight[m_inputCount] = buf[i][1] * RNNOISE_SCALE_IN;
++m_inputCount;
```

Each channel now has its own RNNoise state and frame (`:142-143`):

```cpp
rnnoise_process_frame(m_rnnoiseState, m_outputBuf, m_inputBuf);
rnnoise_process_frame(m_rnnoiseStateRight, m_outputBufRight, m_inputBufRight);
```

and each channel is mixed from its own output (`:154-156`):

```cpp
const SampleFrame& dry = m_dryBuf[m_outputPos];
buf[i][0] = dry[0] * d + m_outputBuf[m_outputPos] * RNNOISE_SCALE_OUT * w;
buf[i][1] = dry[1] * d + m_outputBufRight[m_outputPos] * RNNOISE_SCALE_OUT * w;
```

`plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.h:62` and `:65` declare both
states, `:85-86` both right-channel frame buffers. The constructor creates and the
destructor destroys both states, and `latencyFrames()`/`processImpl()` require
both to be non-null before applying the delay or denoising.

No serialization, control-command, or undo change was made. `processImpl()`
adds no allocation, lock, wait, or log: it only writes two fixed member arrays
and calls `rnnoise_process_frame()` a second time (same cost class as before).

## Test names

`RnnoiseDenoiserEffectTest` (tests/src/plugins/RnnoiseDenoiserEffectTest.cpp):

- `hardPannedRightChannelStaysSilent()`
- `outOfPhaseStereoIsNotCollapsed()`
- `inPhaseStereoStaysIdentical()`

## Commands and exits

`logs/` below is `/home/kruzzzzy/.config/kads/zene/state/runs/4391.r1/logs/`.
Each ctest command below was run exactly with the output redirect and
`echo EXIT=$? >> <log>`, so the final `EXIT=` line is inside the log.

- Build (all touched targets):
  `cmake --build build --target zene rnnoisedenoiser RnnoiseDenoiserEffectTest RnnoiseHarnessOutputTest -j$(nproc)`
  -> `logs/build-2-green.log` records `[100%] Built target` for all four
  targets (no explicit EXIT line was captured in that log).
- Unit test:
  `cd build/tests && ctest -R RnnoiseDenoiserEffectTest --output-on-failure`
  -> 1/1 passed; the same log appends the direct binary run
  (`Totals: 10 passed, 0 failed`); `logs/test-2-green.log` ends `EXIT=0`.
- Family: `cd build/tests && ctest -R Rnnoise --output-on-failure` ->
  `100% tests passed out of 2`; `logs/test-3-family.log`.
- Red with product change reverted: `git apply -R logs/fix.patch`; then
  `cmake --build build --target RnnoiseDenoiserEffectTest -j$(nproc)` ->
  `logs/build-5-reversed-final.log`; then
  `cd build/tests && ctest -R RnnoiseDenoiserEffectTest --output-on-failure > logs/test-4-red-final.log 2>&1; echo EXIT=$? >> logs/test-4-red-final.log`
  -> 0/1 passed and the log ends `EXIT=8`. Failing assertions:
  `hardPannedRightChannelStaysSilent` (`right == 0.0f`) and
  `outOfPhaseStereoIsNotCollapsed` (`peaks.left > 1e-4f`).
- Restored: `git apply logs/fix.patch`; then
  `cmake --build build --target RnnoiseDenoiserEffectTest -j$(nproc)` ->
  `logs/build-6-restored-final.log`; then the same ctest command with
  `>> logs/test-5-green-final.log` -> `100% tests passed out of 1` and the log
  ends `EXIT=0`.
- Product diff restored byte-identically:
  `git diff d00c2c5cf2^ d00c2c5cf2 -- plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.h | diff - logs/fix.patch`
  -> `DIFF_EXIT=0` (fix.patch is that same two-file diff).
- Final family: `cd build/tests && ctest -R Rnnoise --output-on-failure` ->
  `100% tests passed out of 2`, log ends `EXIT=0`; `logs/test-6-final.log`.
- Gates: `python3 .../feature_check.py <repo>` -> `RESULT: PASS`
  (`fork-sources`, `all-sources-reproduce`, `no-upstream-regression`,
  `check-namespace`, `no-tautology`, `doc-figures`,
  `complexity`, `file-length` all PASS); `logs/gates-2.log`
  (earlier identical run: `logs/gates.log`).

## Self-check

- **Regression test fails with the product change reverted and passes with
  it (exit codes inside the logs).** Reverted: `logs/test-4-red-final.log`
  ends `EXIT=8`, 0/1 passed, and `hardPannedRightChannelStaysSilent` and
  `outOfPhaseStereoIsNotCollapsed` fail. Restored:
  `logs/test-5-green-final.log` ends `EXIT=0`, 1/1 passed;
  `logs/test-6-final.log` ends `EXIT=0`, 2/2 passed. The restored product
  diff is byte-identical to `logs/fix.patch` (`diff` exit 0).
- **The test is deterministic: no wall-clock timing, sleeps or races.**
  `tests/src/plugins/RnnoiseDenoiserEffectTest.cpp:54` fixes
  `NoiseSeed = 0x5eed1234u`; `nextNoiseSample()` is an LCG and the same 1440
  frames are generated every run. `Engine48kScope` injects the sample rate;
  no `rand()`, clock, sleep, or thread is used.
- **Fix is at the cause and complete: every caller and sibling checked.**
  The cause was mono downmix plus one output written to both channels; now each
  channel has its own state, input accumulation, and output frame. The only
  users of the states are the constructor, destructor, `latencyFrames()`, and
  `processImpl()` in the same file, and all were updated to require/handle both
  states. The states and buffers are private members; no other caller exists.
- **No collateral damage: old projects and saved data load as before.**
  The change only adds private members and per-channel processing; there is no
  format, serialization, control, or undo change, so saved projects and control
  values are unchanged. No new limit or check was added to shared code; the
  null-state guards remain local to this effect.
- **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not applicable: this is FIXED.
- **Sibling commands in changed command files.** Not applicable: no command
  file was changed. The commit touches only the two RnnoiseDenoiser product
  files, the unit test, and this report; the sibling commands listed
  (`app.*`, `arrangement.*`) are unaffected.
