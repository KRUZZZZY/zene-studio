FIXED

Cluster new-p16-95 — "Lowering the LFO sample rate can make its frequency exceed Nyquist" (plugins/Delay/Lfo.h)

**Verdict: FIXED, but latent.** The class-level defect is real and is pinned by a regression test that is RED on the
original code and GREEN after the fix. The finding's user-visible scenario (a 20 kHz LFO) cannot happen through the
product: the only caller clamps the LFO to 0.2-100 Hz, and the lowest supported sample rate leaves a Nyquist limit of
22.05 kHz. See "Reachability" below.

## The defect

`plugins/Delay/Lfo.h`, `Lfo::setSampleRate()` (pre-fix lines 60-69) recomputes `m_twoPiOverSr` and `m_increment` from
the stored `m_frequency`, but never re-checks that frequency against the new Nyquist limit.

- The class's own contract is in `setFrequency()` (`Lfo.h:44-55`): `frequency > m_samplerate / 2.0` is rejected.
  So the invariant is `m_frequency <= m_samplerate / 2`, and `setSampleRate()` breaks it.
- Once the invariant breaks, `m_increment = m_frequency * m_twoPiOverSr` exceeds pi per sample, so `tick()`
  advances the phase by more than half a cycle per sample. That is an alias of a lower frequency.
- Pre-fix RED evidence: `Lfo(48000)`, `setFrequency(20000)`, `setSampleRate(8000)` leaves `m_frequency == 20000`
  (expected 4000) and `m_increment == 2.5 * 2pi`. See the RED log in the evidence table.

## Fix

`plugins/Delay/Lfo.h:60-75` (post-fix; the clamp is at lines 68-73): after the existing `samplerate <= 0` guard and the `m_samplerate` /
`m_twoPiOverSr` update, clamp `m_frequency` to `m_samplerate / 2.0`, then recompute `m_increment`. The clamp is a
no-op unless the frequency is above the new Nyquist.

Design choice: clamp rather than reject. Rejecting would leave a stale value above the limit. Clamping keeps the LFO
at its highest valid rate. The clamp is not undone if the rate goes back up, because the class keeps no memory of the
requested frequency. In the product that does not matter: `DelayEffect::processImpl` calls `setFrequency()` on every
sample with the model's value (`plugins/Delay/DelayEffect.cpp:126-127`), and a valid model value is accepted again
immediately.

## Reachability in the product (why this is latent)

- Only caller of `plugins/Delay/Lfo.h`: `plugins/Delay/DelayEffect.cpp:64` (constructor), `:127` (setFrequency per
  sample), `:158` (`changeSampleRate()` → `setSampleRate`). `FlangerEffect` uses `QuadratureLfo`, not this class.
- `DelayEffect.cpp:126`: `lfoFrequency = 1.0 / *lfoPeriodPtr`.
- The period model is `plugins/Delay/DelayControls.cpp:38`: `TempoSyncKnobModel(2.f, 0.01f, 5.f, ...)`. Values are
  clamped to [0.01, 5] s by `AutomatableModel::fittedValue` (`src/core/AutomatableModel.cpp:604-606`) on every
  `setValue` (`:462-500`) and on automation (`include/AutomatableModel.h:228`). So the LFO frequency is in
  [0.2, 100] Hz.
- The engine never goes below 44100 Hz: `src/core/AudioEngine.cpp:481` takes the max of the configured rate and
  `SUPPORTED_SAMPLERATES.front()` (`include/AudioEngine.h:84`, 44100). The Nyquist limit is therefore at least 22050 Hz.
- A 20 kHz LFO would need a period of 50 µs. The model forbids that, so the product cannot reach the aliasing case.

## Regression test

`tests/src/core/MathTest.cpp`, in the existing `MathTest` class (which already includes `Lfo.cpp` with the private
access seam, and already has `initializeLfoStateForTest`, lines 43-50):

- `LfoSampleRateChangeClampsFrequencyToNyquist` (lines 127-141): the finding's scenario. RED without the fix
  (`MathTest.cpp:137`: Actual 20000, Expected 4000). GREEN with the fix.
- `LfoSampleRateChangeKeepsValidFrequency` (lines 143-167): no-collateral guard. A valid 440 Hz is kept on a
  downward change, a valid 20 kHz is kept at exactly the 24 kHz limit, an upward change changes nothing, and
  `setSampleRate(0)` is ignored. This test PASSES on the original code too. It guards against over-clamping; it is
  not the regression proof.

Registration: `tests/upstream-modifications.txt` gets one line each for `plugins/Delay/Lfo.h` and
`tests/src/core/MathTest.cpp` (both are inherited files).

## Test matrix

| Path | Case | Covered by |
|---|---|---|
| Normal (finding's scenario) | 20 kHz at 48 kHz → 8 kHz: frequency 4000, increment <= pi | ClampsFrequencyToNyquist |
| Reconfiguration, still valid | 440 Hz at 48 kHz → 8 kHz: unchanged; increment matches formula | KeepsValidFrequency |
| Reconfiguration, upward | 440 Hz at 8 kHz → 96 kHz: unchanged | KeepsValidFrequency |
| Boundary | 20 kHz at 96 kHz → 48 kHz (24 kHz limit): unchanged | KeepsValidFrequency |
| Failure | `setSampleRate(0)`: frequency and rate unchanged | KeepsValidFrequency (and existing ZeroLfoSampleRateIsIgnored) |
| Audio thread | The change is one compare and one assignment; no allocation, lock, log or Qt call. It is not on the per-sample `tick()`/`setFrequency()` path. | by reading |
| Platform | Header-only inline; no new include | by reading |

## Evidence (exact commands and exit codes)

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-95.r1/logs/`.

Builds used `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp`. The default ccache temp dir is not writable from this
sandbox. The first build attempt failed with that error and no product change (`build-1.log`, EXIT=2). Every build
after it was run with the override.

| Step | Command | EXIT | Log |
|---|---|---|---|
| RED build (original `Lfo.h`, new test) | `cmake --build build --target MathTest -j4` | 0 | build-3.log |
| RED run | `cd build/tests && ctest -R '^MathTest$' --output-on-failure` | 8 | test-2-red.log: `FAIL! MathTest::LfoSampleRateChangeClampsFrequencyToNyquist()`, Actual 20000, Expected 4000, `MathTest.cpp(137)`; 1 test run, 0 passed |
| Proof step 1: save fix | `git diff -- plugins/Delay/Lfo.h > fix.patch` | 0 | fix.patch |
| Proof step 2: revert fix | `git apply -R fix.patch` | 0 | — |
| Proof step 3: RED with fix reverted | `cmake --build build --target MathTest -j4` then ctest as above | 0 then 8 | build-7-revert.log, test-6-proof-reverted.log (same FAIL at MathTest.cpp:137) |
| Proof step 4: reapply fix | `git apply fix.patch` | 0 | — |
| Proof step 5: GREEN with fix | build-8-reapply.log, then ctest as above | 0 then 0 | test-7-proof-reapplied.log: 100% passed, 1 of 1 |
| Final rebuild of dependents | `cmake --build build --target zene DelayControlsTest DelayLfoAutomationTest EqPeakHandoffTest MathTest -j4` | 0 | build-9-final.log |
| Final family run | `ctest -R '^(MathTest\|DelayControlsTest\|DelayLfoAutomationTest\|EqPeakHandoffTest)$' --output-on-failure` | 0 | test-8-final-family.log: 100% passed, **4 of 4** tests run |

Other test targets that compile `plugins/Delay/Lfo.h` or `Lfo.cpp` (checked with `grep -rl "Delay/Lfo"` over
`tests/src` and `tests/reference`, and the `DelayEffect.cpp` entries in `tests/CMakeLists.txt`):
`DelayControlsTest`, `DelayLfoAutomationTest`, `EqPeakHandoffTest` (all in the family run above), and
`PluginPortsMigrationTest` (`tests/src/plugins/PluginPortsMigrationTest.cpp:133` includes `Lfo.cpp`).

`PluginPortsMigrationTest`: `build-10-ppm.log` EXIT 0; `test-9-ppm.log` EXIT 8, **Totals: 88 passed, 1 failed,
2 skipped**. The one failure is `nonRealtimeLadspaEffectsAreInvalid` (`PluginPortsMigrationTest.cpp:1616`): "SWH
Decimator test fixture was not loaded". The fixture is the LADSPA plugin `decimator_1202.so`. It is not installed on
this machine: `ls /usr/lib/ladspa` and `ls /usr/lib64/ladspa` show nothing, and a filesystem `find` for "decimator" under
a `ladspa` path returns nothing. The test never touches `Lfo`. It is an environment failure, not a regression from
this change. I did not rerun it on the original code, so this rests on the missing fixture, not on a before/after run.

Gates:

| Step | Command | EXIT | Log / result |
|---|---|---|---|
| feature_check, as written | `python3 .../qwen/feature_check.py <worktree>` | 1 | gates-1.log: `PermissionError` creating the sibling log dir `<worktree>-feature-check`, which is outside the write-jail. No gate ran; this is not a gate result. |
| feature_check logic, logs in LOG FOLDER | `python3 -I gate_runner.py <worktree> <LOG FOLDER>` | 0 | gates-2.log: `RESULT: PASS`. fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures exit 0. complexity rc=1 and file-length rc=1, both "no new item in your files". |

`gate_runner.py` imports `feature_check.py`'s own functions (`SIMPLE`, `RATCHET`, `run`, `base_listings`, `REG`, `key`,
`item_file`) and only changes where the logs go. It is in the LOG FOLDER, not the worktree. It is not a replacement
for the gate. The gate's own run should be repeated where the sibling log dir is writable.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** Yes: `build-7-revert.log` and
  `test-6-proof-reverted.log` (EXIT 0 build, EXIT 8 ctest, `MathTest.cpp(137)`), then `test-7-proof-reapplied.log`
  (EXIT 0). The fix was reverted and reapplied with `git apply -R` / `git apply` from `fix.patch`.
- **The test is deterministic.** It uses no clock, sleep, thread or randomness. Inputs are literal sample rates and
  frequencies. State comes from the existing `initializeLfoStateForTest` seam (`MathTest.cpp:43-50`, fixed 48 kHz,
  phase 0) and the `#define private public` access already used by the file.
- **The fix is at the cause and complete.** Every caller and sibling was checked:
  - `Lfo::Lfo` (`plugins/Delay/Lfo.cpp:34-42`) starts with `m_frequency(0.0)` and calls `setSampleRate`. The clamp is a
    no-op there.
  - `DelayEffect` constructor (`:64`) and `changeSampleRate` (`:158`) call `setSampleRate`. Both benefit; neither needed
    a change.
  - `setFrequency` (`Lfo.h:44-55`) is unchanged, so its guard still rejects frequencies above Nyquist.
  - `StereoDelay::setSampleRate` (`plugins/Delay/StereoDelay.cpp:79`) is a different class with delay lengths, not LFO
    frequency. I did not audit it for this bug.
- **No collateral damage.** The clamp fires only when `m_frequency > m_samplerate / 2`, which the product cannot
  produce (see Reachability). It adds no new limit to `setFrequency`, so no caller that is in range changes. No
  save/load code and no saved-data format is touched, so old projects load as before.
- **Verdict evidence.** FIXED: the class contract is at `Lfo.h:44-55`, the defect is at pre-fix `Lfo.h:60-69`, and the
  product's unreachability is at `DelayControls.cpp:38`, `AutomatableModel.cpp:604-606`, `DelayEffect.cpp:126-127`,
  and `AudioEngine.cpp:481`.
- **Submit check (`submit_check.py`, pre-commit run, `submit-check-1.log`, EXIT 1).** Failing items before this report
  was committed: commits on branch, clean tree, report missing, Self-check, tests changed. These are resolved by this
  commit. The gates item fails because `submit_check.py` calls `feature_check.py` as written, and that hits the same
  `PermissionError` as above. The gate itself passes when run with its logs in the LOG FOLDER (`gates-2.log`).

## Scope note

The original test run (`test-1-red.log`) failed for a test-side reason: my first version of
`LfoSampleRateChangeKeepsValidFrequency` set 20 kHz while the rate was 8 kHz, so the set was rejected. I fixed the
test before recording the RED run in `test-2-red.log`. No product code was touched by that fix.
