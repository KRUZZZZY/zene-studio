FIXED

# new-p12-282: VST wet/dry did not mix output channels above stereo

## Verdict and evidence
REAL. Before the fix, `plugins/VstEffect/VstEffect.cpp` (old lines 105-118) clamped the wet/dry loop with `std::min<ch_cnt_t>(out.channels(), 2)`. Output channels 3 and above kept the plug-in's unscaled wet output, so a dry-only setting (wet 0, dry 1) still passed processed audio on those channels. The mix is now `mixVstWetDry` in `plugins/VstEffect/VstWetDryMix.h`, called from `VstEffect::processImpl` (`plugins/VstEffect/VstEffect.cpp:105`).

Defined mapping: output channel `c` takes its dry signal from input channel `c`. An output with no matching input uses the last input channel. That is the rule the old code already used for channels 0 and 1, so stereo and mono-in/stereo-out behaviour is unchanged. With no input channels (instrument loaded as an effect) nothing is mixed, as before.

Realtime: the helper is `noexcept`, allocation-free and lock-free, and runs the same loop as before for more channels.

## Change
1. Commit 1fc44db37b: behaviour-preserving refactor. The wet/dry loop moves into `mixVstWetDry` (new file, registered in `tests/fork-sources.txt`) and still clamps at 2 channels. `plugins/VstEffect/VstEffect.cpp` is already listed in `tests/upstream-modifications.txt`.
2. Fix commit: `VstWetDryMix.h` loops over all `wet.channels()`.
3. Tests added to `tests/src/plugins/VstEffectControlsTest.cpp`: `dryOnlyMixSilencesEveryOutputChannel` (2 inputs, 4 outputs, dry-only) and `wetDryMixMapsExtraOutputsToLastInput` (mono input, 3 outputs, 50/50). Both are deterministic, with no timing.

Seam note: `VstEffect::processImpl` needs a live remote VST process, so it cannot be driven in a unit test. The mix is extracted so the test runs the exact code that `processImpl` calls.

## Commands (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p12-282.r1/logs)
| Step | Command | EXIT |
|---|---|---|
| RED build (bug in place) | `cmake --build build --target VstEffectControlsTest -j4 > red-build.log` | 0 |
| RED test | `cd build/tests && ctest -R VstEffectControlsTest --output-on-failure > red-test.log` | 8 (2 failed: Actual 9 vs 0.5; Actual 2 vs 1.5) |
| GREEN build | `cmake --build build --target zene VstEffectControlsTest -j4 > green-build.log` | 0 |
| GREEN test | `ctest -R VstEffectControlsTest` (green-test.log) | 0, 1/1 passed |
| 4b revert | `git diff -- plugins/VstEffect/VstWetDryMix.h > fix.patch && git apply -R fix.patch` | 0 |
| 4b revert build | `cmake --build build --target VstEffectControlsTest -j4 > revert-build.log` | 0 |
| 4b revert test | `ctest -R VstEffectControlsTest` (revert-test.log) | 8, both new tests fail |
| 4b reapply | `git apply fix.patch` | 0 |
| 4b reapply build | `cmake --build build --target zene VstEffectControlsTest -j4 > reapply-build.log` | 0 |
| 4b reapply test | `ctest -R VstEffectControlsTest` (reapply-test.log) | 0, passed |

## Gates
`python3 .../feature_check.py <worktree>` did NOT run. The script's `git checkout --detach` of its baseline tree was refused by bunny-guard (output in `feature-check.log`, EXIT=1). I did not work around it. The gate table is therefore unverified. By inspection: the new function `mixVstWetDry` has CCN 3 and `VstWetDryMix.h` is about 55 lines.

## Other findings
None.

Delegation: Haiku ran the RED build and test, and the GREEN / revert / reapply sequence, and reported exit codes. I read the logs myself and checked `git status` and `git diff`.
