FIXED

# cluster new-p14-85: de-esser attenuation state freezes while bypassed

## Verdict: REAL

`DeEsserEffect::processBypassedImpl()` (plugins/DeEsser/DeEsser.h, was line 83) only stored 0 dB in the meter.
`processImpl()` (plugins/DeEsser/DeEsser.cpp:105-134) carries `m_envelope` and `m_gain` (DeEsser.h members) from
block to block, and while bypassed `processImpl()` is never called (include/AudioPlugin.h:272-278, src/core/Effect.cpp:269-272),
so both froze. The filters' memory (`m_detector`, `m_low`) froze too. Un-bypassing resumed from the frozen attenuation.

Correction to the finding: the release is 60 **ms** (`settleCoefficient(60.f, rate)`, ms per include/BiquadDesign.h:120-124),
not 60 seconds, so the stale attenuation lasts a few hundred ms after un-bypassing, not "a long time". The defect is real; the
audible span is shorter than the finding says.

## Fix

`processBypassedImpl()` now also resets `m_envelope = 0`, `m_gain = 1`, `m_detector.reset()` and `m_low.reset()`
(BiquadSection::reset, include/BiquadDesign.h:103). Plain stores and a `std::fill` over fixed arrays: no allocation, lock or log on the audio thread.
`processBypassedImpl` runs under the same processLock as `processImpl`, so there is no new race.
No control command, no saved-data change. Siblings checked: Compressor and DynamicsProcessor already reset their state in `processBypassedImpl`
(plugins/Compressor/Compressor.cpp:595-); other `processBypassedImpl` users are outside this finding.

## Regression test

tests/src/plugins/DeEsserBypassTest.cpp, slot `quietSignalAfterBypassIsNotAttenuated` (plus `loudSibilanceIsTurnedDown` as the premise).
Registered in tests/CMakeLists.txt (LMMS_TESTS + a per-test source block) and tests/fork-sources.txt.
It processes loud 10 kHz sine blocks (reduction > 6 dB), bypasses (meter 0), un-bypasses on a -40 dBFS sine (under the -30 dB threshold)
and requires 0 dB reduction and output equal to input within 1e-6. No timing, no sleeps; sample blocks are fixed.

## Commands and results (logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p14-85.r1/logs)

| step | command | EXIT |
|---|---|---|
| RED build | `cmake --build build -j4 --target DeEsserBypassTest` (build-red-3.log) | 0 |
| RED run | `cd build/tests && ctest -R DeEsserBypassTest --output-on-failure` (test-red-1.log) | 8 (FAIL: `gainReductionDb()` 11.99998, expected 0, DeEsserBypassTest.cpp:119) |
| GREEN build | `cmake --build build -j4 --target DeEsserBypassTest deesser` (build-green-1.log) | 0 |
| GREEN run | `ctest -R DeEsserBypassTest --output-on-failure` (test-green-1.log) | 0 |
| 4b revert | `git diff -- plugins/DeEsser > fix.patch && git apply -R fix.patch` | 0 |
| 4b build+run without fix | build (build-red-4.log) 0; ctest (test-red-2.log) | 8 (FAIL) |
| 4b re-apply | `git apply fix.patch` | 0 |
| 4b build+run with fix | build (build-green-2.log) 0; ctest (test-green-2.log) | 0 (PASS) |

`ctest -R DeEsser` matches only this test; no other test in the family exists. `deesser` module target also rebuilt (EXIT 0) after the header change.

## Gates

`python3 .../feature_check.py <worktree>` could NOT run: it exited 1 (feature-check-1.log) because its own
`git checkout --detach` on the baseline tree was refused by bunny-guard (exit 97). I did not work around it. Checked by hand instead:
the new function is a straight-line body (CCN 1); the new test file is 127 lines; the new test file and this report are in tests/fork-sources.txt (file ends with a newline);
DeEsser.h was already in fork-sources.txt; tests/CMakeLists.txt was already in tests/upstream-modifications.txt.
The registration this missed - tests/src/plugins/DeEsserBypassTest.cpp in tests/all-sources.txt - was added by the QA
review (`qa-fix(new-p14-85)`); the manifest now reproduces from its own recipe (`bash tests/all-sources-reproduce.sh`
EXIT 0) and `bash tests/fork-sources-gate.sh` EXIT 0.
Local TSan does not run on this box; the race argument above is by design.

## Self-check

Completion check (submit_check.py, submit-check-1.log) EXIT=1: every item PASS except `gates (feature_check.py)`, which FAILs
only because feature_check.py's internal `git checkout --detach` on its baseline tree is refused by bunny-guard (exit 97, feature-check-1.log). That is BLOCKED
on the gate runner, not a finding in this change; the hand checks are listed under Gates.

- Test fails reverted / passes with fix: reverted product change -> ctest EXIT=8 (test-red-2.log, `gainReductionDb()` 11.99998 vs 0); with fix -> EXIT=0 (test-green-2.log).
- Deterministic: fixed 4410-frame 10 kHz sine blocks computed in the test, engine fixed at 44100 Hz, no timers, sleeps or threads; the bypass is driven by `setEnabled(false/true)` between blocks.
- Cause and completeness: the cause is that `processBypassedImpl` reset only the meter; all state that `processImpl` carries between blocks (`m_envelope`, `m_gain`, `m_detector`, `m_low`) is now reset there. `m_frequency`/`m_rate` are coefficient caches, not signal state, and stay. Compressor.cpp:595 already resets its state the same way.
- Collateral: no saved parameter, no model, no command changed; only transient DSP state is zeroed on a bypassed block. A project loads exactly as before. The audio path is unchanged while enabled.

Delegation: Haiku was started three times (a first build run that found configure is refused for sessions and the new target needed a build-triggered regeneration; a second build run that reported a test-side compile error; one throwaway no-op). DeepSeek ran one build attempt that found `processAudioBuffer` is protected on the plugin type. The final red/green/4b runs were done by me directly because they take seconds in this tree.

## Other findings

None noticed beyond the finding's 60 s vs 60 ms discrepancy.
