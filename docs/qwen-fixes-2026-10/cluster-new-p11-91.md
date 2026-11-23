FIXED

# Cluster new-p11-91 - Dispersion keeps using the sample rate from when it was constructed

Finding: `plugins/Dispersion/Dispersion.cpp:~55` (historical line). Verdict REAL, fixed on this
branch.

## Decision evidence (file:line, this tree, after the fix)

- The rate is captured once and never refreshed:
  - `plugins/Dispersion/Dispersion.cpp:59` - the constructor stores
    `m_sampleRate(Engine::audioEngine()->outputSampleRate())`.
  - `plugins/Dispersion/Dispersion.h:56` - a plain `float m_sampleRate;`, no handler, no atomic.
  - Before this commit the four Dispersion sources (`Dispersion.cpp`, `Dispersion.h`,
    `DispersionControls.cpp`, `DispersionControlDialog.cpp`) contained no occurrence of
    `sampleRateChanged` at all, so nothing refreshed it.
- Every coefficient is derived from that member, once per period, in
  `DispersionEffect::processImpl`:
  - `plugins/Dispersion/Dispersion.cpp:105` - the Nyquist clamp `min(freq, m_sampleRate * 0.499)`.
  - `plugins/Dispersion/Dispersion.cpp:111` - `w0 = (2*pi / m_sampleRate) * freq`, which feeds
    `apCoeff1`/`apCoeff2` at `:112-113`.
  - `plugins/Dispersion/Dispersion.cpp:116` - `dcCoeff = 0.001 * (44100.f / m_sampleRate)`, the
    DC-removal time constant.
- The engine does announce a rate change, so the contract the effect failed to honour exists:
  - `include/AudioEngine.h:655` - `void sampleRateChanged();`.
  - `src/core/AudioEngine.cpp:808` - `initDevices()` emits it (before processing starts).
  - `src/core/AudioEngine.cpp:1416-1423` - `setAudioDevice()` calls `stopProcessing()` at `:1418`
    and emits at `:1423`.
  - `src/core/AudioEngine.cpp:1442-1450` - `restoreAudioDevice()` calls `stopProcessing()` at
    `:1446` and emits at `:1450`.
- Siblings already listen: `plugins/Bitcrush/Bitcrush.cpp`, `plugins/Compressor/Compressor.cpp`,
  `plugins/CrossoverEQ/CrossoverEQ.cpp`, `plugins/FrequencyShifter/FrequencyShifterEffect.cpp:60`,
  `plugins/LOMM/LOMM.cpp`, `plugins/SlewDistortion/SlewDistortion.cpp`, and the earlier fix in the
  same shape `plugins/Lb302/Lb302.cpp` (recorded in `tests/upstream-modifications.txt:2509`,
  cluster new-p10-185). Dispersion was the one left out.
- Measured on the unfixed tree (`logs/test-2-red.log`, `logs/test-6-red-proof.log`): with the effect
  constructed at 44.1 kHz and the device moved to 48 kHz while it stayed loaded, its stored rate was
  still the old one (`Actual (effect->m_sampleRate): 44100`, `Expected (48000.f): 48000`) and its
  render stayed more than 1e-3 away from a render of an effect constructed at 48 kHz, while two
  renders at the same rate agree exactly.

## Fix

`plugins/Dispersion/Dispersion.cpp`, in the constructor body only:

```cpp
connect(Engine::audioEngine(), &AudioEngine::sampleRateChanged, this,
    [this] { m_sampleRate = Engine::audioEngine()->outputSampleRate(); });
```

- At the cause: the member the three coefficient expressions read is the member that now follows
  the engine. `processImpl`, `runDispersionAP` and the header are unchanged; the filter state and
  the control set are unchanged, so a saved project loads and sounds exactly as before.
- Realtime: `processImpl()` gained no work - no allocation, lock, wait or log on the audio path.
  The slot runs on the thread that emits, i.e. the control/GUI thread, and every production emitter
  runs with the engine stopped (`stopProcessing()` precedes the emit in `setAudioDevice()` and
  `restoreAudioDevice()`; `initDevices()` emits before processing starts). That is the same argument
  `tests/src/plugins/LommSampleRateTest.cpp:30-38` records for the same signal, so the plain-float
  write and the audio-thread read are not concurrent. Local TSan does not run on this box; this is a
  design argument, not a measured race-freedom claim.
- Undo, commands, registration canaries: no model is touched, no command added, no control added.
- All three reconfiguration sites are covered by the one connection: `initDevices()` (effect loaded
  before a device exists), `setAudioDevice()` (the finding's scenario), `restoreAudioDevice()`
  (export/render restore).

## Regression test

`tests/src/plugins/DispersionNonFiniteTest.cpp` (the existing Dispersion test file, fork-NEW, already
registered in `tests/fork-sources.txt:3476` and `tests/CMakeLists.txt:4939`). Two new slots; the
plugin sources are compiled into this binary, so the shipped code is what runs. No plugin module, no
audio device I/O, no thread, no sleep, no clock: the rate is injected by
`AudioDevice::setSampleRateForTesting()` (`include/AudioDevice.h:87`) plus the engine signal.

1. `rateChangeReachesTheFilterAndDcCoefficients` - the finding's scenario as behaviour:
   effect constructed at 44.1 kHz, device moved to 48 kHz while it stays loaded; its render must
   equal the render of an effect constructed at 48 kHz (tolerance 1e-6). Two guards keep the test
   honest: the DC-removal control must be enabled and the stored rate must be the construction rate
   before the change, and a third effect rendered at 44.1 kHz before the change must differ from the
   48 kHz render by more than 1e-3 - otherwise the rate does not reach the coefficients and the
   equality check would pass on unfixed code.
2. `sampleRateTracksTheEngineAfterAChange` - the stored rate itself follows the engine across two
   changes (44.1 -> 48 -> 96 kHz) on an effect that has already rendered a period, and the render
   stays finite after each change.

Test matrix:

| Path | Covered by |
| --- | --- |
| normal (rate change reaches the coefficients) | slot 1 |
| failure (the bug: stale rate) | slot 1 without the fix, slot 2 without the fix |
| exhaustion | not applicable: the fix adds no buffer, limit or allocation; `MAX_DISPERSION_FILTERS` and the amount flush path are untouched |
| reconfiguration | one slot drives the same signal all three emitters use; slot 2 covers repeated changes and a change after processing |
| audio thread | `processImpl` is unchanged; nothing allocates, locks, waits or logs per period |
| platform build | uses only `QObject::connect` and `AudioEngine::outputSampleRate()`, both already used by six sibling plugins; the test is compiled into the existing QtTest target, no new dependency |

## Commands and exit codes (all logs in `~/.config/kads/zene/state/runs/new-p11-91.r1/logs`)

| # | Command | Exit |
| --- | --- | --- |
| 1 | `cmake --build build --target DispersionNonFiniteTest -j4` (baseline, before any edit) `build-1.log` | 0 |
| 2 | `ctest -R DispersionNonFiniteTest --output-on-failure` (baseline, `test-1.log`) | 0 |
| 3 | `cmake --build build --target DispersionNonFiniteTest -j4` (tests added, no fix, `build-2-red.log`) | 0 |
| 4 | `ctest -R DispersionNonFiniteTest --output-on-failure` (`test-2-red.log`) | **8** (RED) |
| 5 | `cmake --build build --target DispersionNonFiniteTest dispersion -j4` (first fix, `build-3-green.log`) | 0 |
| 6 | `ctest -R DispersionNonFiniteTest --output-on-failure` (`test-3-green.log`) | **8** (vacuous guard fired; see Self-check) |
| 7 | `cmake --build build --target DispersionNonFiniteTest -j4` (guard reworked, `build-4-green.log`) | 0 |
| 8 | `ctest -R DispersionNonFiniteTest --output-on-failure` (`test-4-green.log`) | 0 (GREEN) |
| 9 | `git diff -- plugins/Dispersion/Dispersion.cpp > logs/fix.patch` | 0 |
| 10 | `git apply -R logs/fix.patch` (step 4b revert) | 0 |
| 11 | `cmake --build build --target DispersionNonFiniteTest -j4` (`build-6-red-proof.log`) | 0 |
| 12 | `ctest -R DispersionNonFiniteTest --output-on-failure` (`test-6-red-proof.log`) | **8** (both new slots fail) |
| 13 | `git apply logs/fix.patch` (re-apply) | 0 |
| 14 | `cmake --build build --target DispersionNonFiniteTest dispersion zene -j4` (`build-7-regreen.log`) | 0 |
| 15 | `ctest -R DispersionNonFiniteTest --output-on-failure` (`test-7-regreen.log`) | 0 |
| 16 | `./DispersionNonFiniteTest` (`test-8-verbose.log`: `Totals: 12 passed, 0 failed`) | 0 |
| 17 | `cmake --build build --target LommSampleRateTest -j4` (`build-9-lomm.log`) | 0 |
| 18 | `ctest -R "Dispersion\|LommSampleRateTest\|BasicFiltersSampleRateTest"` (`test-10-family.log`, 3/3 passed) | 0 |
| 19 | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`gates-1.log`) | 0 |

Log 12 lines that make the proof (`test-6-red-proof.log`):

```
FAIL!  : DispersionNonFiniteTest::rateChangeReachesTheFilterAndDcCoefficients() 'largestDifference(newRateRender, reconfiguredRender) <= RenderTolerance' returned FALSE. (the effect kept the coefficients of the rate it was constructed at)
FAIL!  : DispersionNonFiniteTest::sampleRateTracksTheEngineAfterAChange() Compared floats are not the same (fuzzy compare)
   Actual   (effect->m_sampleRate): 44100
   Expected (48000.f)             : 48000
```

## Gates (`logs/gates-1.log`)

| Gate | Exit | Note |
| --- | --- | --- |
| fork-sources | 0 | |
| all-sources-reproduce | 0 | |
| no-upstream-regression | 0 | |
| check-namespace | 0 | |
| no-tautology | 0 | |
| doc-figures | 0 | |
| complexity | PASS | `no new item in your files` |
| file-length | PASS | `no new item in your files` |

`RESULT: PASS`, exit 0 unpiped. The two ratchets print a verdict rather than a code; this run
reports no new item in a file this branch changes, so the branch-changed-files rule holds.

## Self-check

Answers to every ASK item of `submit_check.py`:

- *The regression test FAILS with the product change reverted and PASSES with it (exit codes).*
  Proven, not reasoned: `git diff -- plugins/Dispersion/Dispersion.cpp > logs/fix.patch` (exit 0),
  `git apply -R logs/fix.patch` (exit 0), rebuild (exit 0), `ctest -R DispersionNonFiniteTest`
  exit **8** with both new slots failing (`test-6-red-proof.log`); `git apply logs/fix.patch`
  (exit 0), rebuild (exit 0), same ctest exit **0** (`test-7-regreen.log`); the binary run reports
  `Totals: 12 passed, 0 failed` (`test-8-verbose.log`). The earlier pre-fix run `test-2-red.log`
  shows the same two failures. The first green attempt failed honestly (row 6): my vacuity guard
  rendered the 44.1 kHz reference from an effect that also received the signal, so after the fix the
  two references became equal and the guard refused to certify a vacuous test. The guard now renders
  at the old rate before the change; no assertion was weakened and no gate was touched.
- *Deterministic: no wall-clock timing, sleeps or races.* The seams are
  `AudioDevice::setSampleRateForTesting()` (`include/AudioDevice.h:87`) and the engine signal
  `emit engine->sampleRateChanged()`, delivered synchronously on the calling thread; the audio
  content is one injected impulse through an effect whose state starts at zero, and every comparison
  is exact-value or tolerance based. No timer, sleep, thread, file or audio-device I/O appears in
  the two slots.
- *At the cause, complete for every caller and sibling.* `m_sampleRate` is read only in
  `processImpl` (`plugins/Dispersion/Dispersion.cpp:105`, `:111`, `:116`) and written only in the
  constructor and the new slot; `Dispersion.h`, `DispersionControls.cpp` and
  `DispersionControlDialog.cpp` never touch it, so the header, the controls, the save/load path,
  `amount` and the filter state stay as they were. Checked by script over `plugins/*/*.cpp`: of the
  plugins that cache `outputSampleRate()` at construction, Dispersion was the only one without a
  `sampleRateChanged` handler; the six siblings (Bitcrush, Compressor, CrossoverEQ, FrequencyShifter,
  LOMM, SlewDistortion) and the Lb302 fix keep their own handling.
- *No collateral damage.* No model, range, default or control XML name changed, so a saved project
  loads and renders exactly as before; the fix adds no limit or check that could reject another
  caller; `m_state`, `m_feedbackVal`, `m_integrator` and `m_amountVal` are untouched. Nothing
  allocates, locks, waits or logs on the audio thread, and no command or undo step was added.
- *ALREADY-FIXED / FALSE-POSITIVE evidence.* Not applicable: the verdict is FIXED. The code that
  shows the defect before this commit is `plugins/Dispersion/Dispersion.cpp:59` (one-shot capture)
  with no handler in any of the four Dispersion sources, plus the measured
  `Actual 44100 / Expected 48000` in `test-6-red-proof.log`.
- *Sibling commands in the command files I changed.* I changed no command file: the diff is
  `plugins/Dispersion/Dispersion.cpp`, `tests/src/plugins/DispersionNonFiniteTest.cpp`,
  `tests/upstream-modifications.txt` and this report. None of `app.*`, `arrangement.*` or any other
  control command is touched, so that sibling list does not apply and the `ControlCommandsSnapshot`
  canary is unaffected.
- Unverified: race-freedom is argued from `stopProcessing()` preceding every emit, not measured -
  local TSan does not run on this box.

## Files committed

- `plugins/Dispersion/Dispersion.cpp` (fix)
- `tests/src/plugins/DispersionNonFiniteTest.cpp` (two regression slots)
- `tests/upstream-modifications.txt` (reason for the inherited-file change)
- `docs/qwen-fixes-2026-10/cluster-new-p11-91.md` (this report)

## Other findings

- `plugins/SpectrumAnalyzer/SaProcessor.cpp:55` also caches `outputSampleRate()` at construction,
  but its analysis loop re-reads it every block at `:168`, so it does not carry this defect. Not
  audited further.
- Nine `-R` matches in the family run are "Not Run" in this build tree because their executables are
  not built here (`FreeBoySampleRateHandoff`, eight `NeuralAmp*`). `FreeBoySampleRateHandoff` has no
  make target at all in this configuration; none of them includes `Dispersion.h`. Untouched by this
  change and left as the tree provides them.
