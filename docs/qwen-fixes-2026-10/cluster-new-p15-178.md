# Cluster new-p15-178: RNNoise reports latency while bypassing after state creation fails

**Verdict: FIXED**

`RnnoiseDenoiserEffect::latencyFrames()` returned 479 at 48 kHz whether or not the RNNoise state existed. When `rnnoise_create()` returned nullptr, `processImpl()` returned `ContinueIfNotQuiet` before touching the buffer, so the audio passed through undelayed, but the host still applied 479 frames of latency compensation. `latencyFrames()` now also requires `m_rnnoiseState != nullptr`, matching the bypass condition in `processImpl()`.

## Evidence

- `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp:93-99`: the fixed `latencyFrames()` checks `m_rnnoiseState != nullptr` alongside the engine-rate check. The comment at `:95` names the bypass it mirrors.
- `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp:112-116` (unchanged): `processImpl()` returns `ContinueIfNotQuiet` without delay when `!m_rnnoiseState`. This is the path the old latency report disagreed with.
- `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp:79`: the state is created only in the constructor and destroyed only in the destructor, so the null check cannot change while the host is reading latency.
- `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.h:85`: `friend class ::RnnoiseDenoiserEffectTest;` under `#ifdef LMMS_TESTING`, the same test-friend pattern as `include/AudioEngine.h:657`. It is a test seam only; it does not change behaviour.
- `tests/src/plugins/RnnoiseDenoiserEffectTest.cpp:58-75`: `Engine48kScope` switches the engine to 48 kHz for the new tests and restores 44.1 kHz on exit, so the existing tests keep their rate.
- `tests/src/plugins/RnnoiseDenoiserEffectTest.cpp:108-148`: new tests:
  - `stateAt48kReportsRnnoiseLatency` (`:108`): normal path, state present at 48 kHz, latency 479. Passes before and after the fix.
  - `missingStateAt48kAddsNoLatency` (`:118`): the regression. Fails before the fix.
  - `missingStateAt48kPassesAudioThroughUndelayed` (`:127`): the bypass audio is the input, sample for sample. Passes before and after.
  - `discardState` (`:143`): makes the state missing the way the failed constructor leaves the effect.
- The existing `unsupportedRatePassesAudioThrough` and `unsupportedRateAddsNoLatency` tests are unchanged and still pass at 44.1 kHz.

## Test matrix

| Path | Covered by | Result |
| --- | --- | --- |
| Normal: state present, 48 kHz | `stateAt48kReportsRnnoiseLatency` | 479, passes before and after |
| Failure: state missing, 48 kHz (the finding) | `missingStateAt48kAddsNoLatency` | RED before fix (479 vs 0), GREEN after |
| Failure: state missing, 48 kHz, audio | `missingStateAt48kPassesAudioThroughUndelayed` | passes before and after (bypass already undelayed) |
| Unsupported rate (44.1 kHz) | `unsupportedRateAddsNoLatency`, `unsupportedRatePassesAudioThrough` | unchanged, pass |
| Reconfiguration | Not covered: the state is created in the constructor and never recreated, so there is no runtime transition to test |  |
| Audio thread | Fix is a pure read of a member and the engine rate; no allocation, lock, log or wait added |  |
| Platform | Test links `rnnoise_vendored` as before; no CMake change needed |  |

## Evidence table

| Check | Exact command and result |
| --- | --- |
| Build attempt, ccache temp dir not writable (not a code failure) | `cmake --build build --target RnnoiseDenoiserEffectTest -j4 > .../logs/build-1.log 2>&1; echo EXIT=$?` → **EXIT=2**. ccache could not create its temp file under the bunny-fix `dsh` folder. |
| Build, ccache temp redirected (RED build) | `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/zene-tmp/ccache-tmp-new-p15-178 cmake --build build --target RnnoiseDenoiserEffectTest -j4 > .../logs/build-2.log 2>&1; echo EXIT=$?` → **EXIT=0** |
| RED | From `build/tests`: `ctest -R '^RnnoiseDenoiserEffectTest$' --output-on-failure > .../logs/test-1.log 2>&1; echo EXIT=$?` → **EXIT=8**. `FAIL! : RnnoiseDenoiserEffectTest::missingStateAt48kAddsNoLatency()`, Actual `effect.latencyFrames()` = 479, Expected 0, `RnnoiseDenoiserEffectTest.cpp(124)`. Totals: 6 passed, 1 failed. |
| Fix | `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp` `latencyFrames()`, lines 93-99 (see above). |
| Build after fix | Same build command → `build-3.log`, **EXIT=0** |
| GREEN | From `build/tests`: `ctest -R '^RnnoiseDenoiserEffectTest$' --output-on-failure > .../logs/test-2.log 2>&1; echo EXIT=$?` → **EXIT=0**, 100% passed, 1/1. |
| Function count | `./RnnoiseDenoiserEffectTest -v2 > .../logs/test-2b.log 2>&1; echo EXIT=$?` → **EXIT=0**; `Totals: 7 passed, 0 failed, 0 skipped`. |
| Reversed-fix proof, step 1 | `git diff -- plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp > .../logs/fix.patch; echo EXIT=$?` → **EXIT=0**; then `git apply -R .../logs/fix.patch; echo EXIT=$?` → **EXIT=0** |
| Reversed-fix build | `cmake --build ... RnnoiseDenoiserEffectTest` → `build-4.log`, **EXIT=0** |
| Reversed-fix run | `ctest -R '^RnnoiseDenoiserEffectTest$' ... > .../logs/test-3.log`, **EXIT=8**. Same assertion: Actual 479, Expected 0, `RnnoiseDenoiserEffectTest.cpp(124)`. |
| Restored-fix proof, step 2 | `git apply .../logs/fix.patch; echo EXIT=$?` → **EXIT=0**; build `build-5.log` **EXIT=0**; `ctest ... > .../logs/test-4.log`, **EXIT=0**, 1/1 passed. |
| Product build | `cmake --build build --target zene rnnoisedenoiser -j4 > .../logs/build-6.log 2>&1; echo EXIT=$?` → **EXIT=0** |
| Affected family, first run | `ctest -R 'Rnnoise' ... > .../logs/test-5.log`, **EXIT=8**: `RnnoiseHarnessOutputTest` was "Not Run" because its helper binary was not built. Not a test result. |
| Helper build | `cmake --build build --target RnnoiseHarnessCli RnnoiseHarnessOutputTest -j4 > .../logs/build-7.log 2>&1; echo EXIT=$?` → **EXIT=0** |
| Affected family, final | From `build/tests`: `ctest -R 'Rnnoise' ... > .../logs/test-6.log 2>&1; echo EXIT=$?` → **EXIT=0**, 2/2 passed (`RnnoiseDenoiserEffectTest`, `RnnoiseHarnessOutputTest`). |
| Gates | `python3 feature_check.py <worktree>` as written → exited **1** before running any gate: `PermissionError` creating the sibling folder `zene-qwen-new-p15-178-feature-check`, which is outside the write area. The same script was run as a scratch copy in `$TMPDIR`, with `ROOT` pinned to the real qwen folder and the log folder changed to `.../logs/gates-1`. Nothing else changed. `python3 -I <copy> <worktree> > .../logs/gates-1/table.log 2>&1; echo EXIT=$?` → **EXIT=0**, `RESULT: PASS` (table below). |

Gate table (scratch copy, EXIT=0):

| Gate | Result |
| --- | --- |
| fork-sources | PASS |
| all-sources-reproduce | PASS |
| no-upstream-regression | PASS |
| check-namespace | PASS |
| no-tautology | PASS |
| doc-figures | PASS |
| complexity | PASS (no new item in changed files) |
| file-length | PASS (no new item in changed files) |

## Files committed

- `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.cpp`: the fix.
- `plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.h`: test-only friend under `LMMS_TESTING`. The reversed-fix proof leaves it in place, because reverting it would break compilation rather than exercise the behaviour.
- `tests/src/plugins/RnnoiseDenoiserEffectTest.cpp`: the regression tests.
- `docs/qwen-fixes-2026-10/cluster-new-p15-178.md`: this report.

No new files were created, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` need no entries; the three source files are already registered (`tests/fork-sources.txt:1291`, `:1292`, `:2898`).

## Self-check

- **Regression fails without the change and passes with it.** RED: `test-1.log`, EXIT=8, `missingStateAt48kAddsNoLatency` Actual 479 / Expected 0. Reverted fix: `test-3.log`, EXIT=8, same assertion. Restored fix: `test-4.log`, EXIT=0. Full command list in the evidence table above.
- **Deterministic.** No sleeps, wall-clock, threads or randomness. The engine rate is set through `ConfigManager` (`Engine48kScope`, `RnnoiseDenoiserEffectTest.cpp:58-75`). The missing state is produced by `discardState` (`:143`), which calls `rnnoise_destroy` and nulls the member through the `LMMS_TESTING` friend (`RnnoiseDenoiserEffect.h:85`). Both are fixed inputs.
- **Fix at the cause, complete.** `latencyFrames()` is overridden only by this effect (`plugins/RnnoiseDenoiser/RnnoiseDenoiserEffect.h:52`). Its callers read it through `Effect::latencyFrames()` or `Effect::compensatedLatencyFrames()` (`include/Effect.h:294-314`): `src/core/EffectChain.cpp:103`, `src/core/ControlDeviceSupport.cpp:462`, `src/gui/shell/DeviceCardState.cpp:106`. The bypass condition they must agree with is `processImpl()` at `RnnoiseDenoiserEffect.cpp:112-116`, and the new check mirrors it. The state is created once in the constructor (`:79`) and only freed in the destructor (`:85-89`), so the value cannot change while a host reads it.
- **No collateral damage.** The change only alters the result when `m_rnnoiseState` is null, which the code comments (`:112`) say happens only on OOM. Behaviour at 48 kHz with a state, and at other rates, is unchanged: `stateAt48kReportsRnnoiseLatency` (479) and both `unsupportedRate*` tests pass. No limit or check was added. I did not verify that no saved project stores the latency value; none of the callers listed above writes it.
- **Verdict FIXED**, so no already-fixed or false-positive citation is needed.
- **Gate status.** The official `feature_check.py` exits 1 in this sandbox before running any gate: `PermissionError` creating `zene-qwen-new-p15-178-feature-check`, a sibling folder outside the write area (`logs/gates-orig.log`, ORIG_GATE_EXIT=1). The gate table above is from a scratch copy with only the log folder and `ROOT` changed (EXIT=0). `submit_check.py` runs the official script, so its gate row will stay FAIL until the script can write its logs.

## Notes

- Engine restart in the test: `Engine48kScope` calls `Engine::destroy()`, sets `samplerate`, then `Engine::init(true)` twice in one binary (48 kHz, then back to 44.1 kHz). It passed here (`test-2b.log`, 7/7). I did not check whether other tests re-init the engine within one binary; this is the part to look at first if the test misbehaves.
- No build of other targets was needed beyond `zene` and `rnnoisedenoiser`.
