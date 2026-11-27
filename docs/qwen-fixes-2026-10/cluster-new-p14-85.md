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
the new function is a straight-line body (CCN 1); the new test file is 134 lines; the new test file and this report are in tests/fork-sources.txt (file ends with a newline);
DeEsser.h was already in fork-sources.txt; tests/CMakeLists.txt was already in tests/upstream-modifications.txt.
The registration this missed - tests/src/plugins/DeEsserBypassTest.cpp in tests/all-sources.txt - was added by the QA
review (`qa-fix(new-p14-85)`); the manifest now reproduces from its own recipe (`bash tests/all-sources-reproduce.sh`
EXIT 0) and `bash tests/fork-sources-gate.sh` EXIT 0.
Local TSan does not run on this box; the race argument above is by design.

## Self-check

Current state (round 4, logs in /home/kruzzzzy/.config/kads/zene/state/runs/new-p14-85.r4/logs): `feature_check.py` now runs to the end,
EXIT=0, all eight rows PASS (feature-check-1.log); the completion check is recorded in the Round 3/4 section below.
(Rounds 1-2: feature_check.py could not finish, its baseline `git checkout --detach` was refused, exit 97; that is no longer the case.)

- Test fails reverted / passes with fix: reverted product change -> ctest EXIT=8 (test-red-2.log, `gainReductionDb()` 11.99998 vs 0); with fix -> EXIT=0 (test-green-2.log).
- Deterministic: fixed 4410-frame 10 kHz sine blocks computed in the test, engine fixed at 44100 Hz, no timers, sleeps or threads; the bypass is driven by `setEnabled(false/true)` between blocks.
- Cause and completeness: the cause is that `processBypassedImpl` reset only the meter; all state that `processImpl` carries between blocks (`m_envelope`, `m_gain`, `m_detector`, `m_low`) is now reset there. `m_frequency`/`m_rate` are coefficient caches, not signal state, and stay. Compressor.cpp:595 already resets its state the same way.
- Collateral: no saved parameter, no model, no command changed; only transient DSP state is zeroed on a bypassed block. A project loads exactly as before. The audio path is unchanged while enabled.

Delegation: Haiku was started three times (a first build run that found configure is refused for sessions and the new target needed a build-triggered regeneration; a second build run that reported a test-side compile error; one throwaway no-op). DeepSeek ran one build attempt that found `processAudioBuffer` is protected on the plugin type. The final red/green/4b runs were done by me directly because they take seconds in this tree.

## Other findings

None noticed beyond the finding's 60 s vs 60 ms discrepancy.

## Round 2 (sent back for tests/all-sources.txt)

- The new test was missing from tests/all-sources.txt; the reviewer's commit 7004f29f72 registered it. Re-run this round (logs in
  /home/kruzzzzy/.config/kads/zene/state/runs/new-p14-85.r2/logs): `bash tests/all-sources-reproduce.sh` EXIT=0 (all-sources-1.log),
  `bash tests/fork-sources-gate.sh` EXIT=0 (fork-gate-1.log).
- `feature_check.py` still cannot finish: EXIT=1, its baseline `git checkout --detach` is refused by bunny-guard (exit 97, feature-check-1.log). Not worked around.
- 4b again: product diff 9913d36d53..HEAD for plugins/DeEsser reverted -> build EXIT=0, `ctest -R DeEsserBypassTest` EXIT=8 (test-red-1.log);
  re-applied -> build EXIT=0, ctest EXIT=0 (test-green-1.log).
- Note from review, accepted: the regression passes on the envelope and gain resets alone; the two `reset()` calls on the biquads are an extra, harmless reset of
  signal state (so a bypass round-trip also starts the filters from rest), and they run on every bypassed block (four float stores and two 2-element fills, no allocation),
  unlike Compressor's `m_cleanedBuffers` guard.
- Delegation (round 2): none; the gate re-runs and the red/green builds are seconds-long here and I ran them directly.

## Round 3/4 (diagnostic re-verification; round 3 committed nothing, so this section is the round 4 record)

No product or test code changed after round 2 (HEAD a980bb872c when verified). All logs in
/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-85.r4/logs.

| step | command | EXIT |
|---|---|---|
| patch | `git diff 9b3d59e2a2~1 HEAD -- plugins/DeEsser/ > fix.patch` (only the DeEsser.h `processBypassedImpl` hunk) | 0 |
| green build | `cmake --build build --target zene DeEsserBypassTest -j4` (build-1.log) | 0 |
| green run | `cd build/tests && ctest -R DeEsser --output-on-failure` (test-1-green.log, 1/1 passed) | 0 |
| 4b revert | `git apply -R fix.patch` | 0 |
| red build | `cmake --build build --target zene DeEsserBypassTest -j4` (build-2-red.log) | 0 |
| red run | `ctest -R DeEsserBypassTest --output-on-failure` (test-2-red.log): `quietSignalAfterBypassIsNotAttenuated`, `gainReductionDb()` 11.99998 vs 0 at DeEsserBypassTest.cpp(122) | 8 |
| 4b re-apply | `git apply fix.patch`; `git status --short` empty | 0 |
| green build | (build-3-green.log) | 0 |
| green run | `ctest -R DeEsserBypassTest --output-on-failure` (test-3-green.log, 1/1 passed) | 0 |
| gate | `bash tests/all-sources-reproduce.sh` (gate-all-sources.log, REPRODUCES) | 0 |
| gate | `bash tests/fork-sources-gate.sh` (gate-fork-sources.log, PASS) | 0 |
| gate | `bash tests/file-length-gate.sh --check` (gate-file-length.log, PASS) | 0 |
| gates | `python3 .../feature_check.py <worktree>` (feature-check-1.log): fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS | 0 |

Review items, all resolved or non-blocking (zero open blocking entries):
- Resolved: tests/all-sources.txt registration (7004f29f72); both source gates EXIT 0 above.
- Resolved: MSVC `M_PI` concern; a980bb872c uses a local `Pi` literal (tests/src/plugins/DeEsserBypassTest.cpp:46).
- Corrections (wording only): the test file is now 137 lines (not 127/134), and the red assertion is at DeEsserBypassTest.cpp:122 (was 119 before a980bb872c).
- Non-blocking: the regression is carried by the envelope/gain resets; the two biquad `reset()` calls are extra signal-state resets in the same function.
- Non-blocking: the reset runs on every bypassed block (Compressor guards with `m_cleanedBuffers`); four float stores and two 2-element fills, realtime-safe.

Self-check answers (round 4):
- Fails reverted / passes with fix: red EXIT=8 (test-2-red.log), green EXIT=0 (test-3-green.log).
- Deterministic: fixed 4410-frame 10 kHz sine blocks generated in the test at 44100 Hz; bypass driven by `setEnabled(false/true)` between blocks; no timers, sleeps or threads.
- Cause and completeness: every piece of state `processImpl` carries between blocks (`m_envelope`, `m_gain`, `m_detector`, `m_low`) is reset in `processBypassedImpl` (plugins/DeEsser/DeEsser.h:85-92); `m_frequency`/`m_rate` are coefficient caches and correctly stay; sibling Compressor already resets in its `processBypassedImpl`.
- Collateral: no model, saved parameter or control command changed; enabled processing is unchanged; projects load as before.
- ALREADY-FIXED / FALSE-POSITIVE: not applicable (verdict FIXED).

Delegation (round 3/4): Haiku ran the full build / red-green / gate / feature_check sequence twice (r3 and r4 log folders) and reported exit codes; I checked the log endings and `git status` myself and wrote this report. No Sonnet or DeepSeek: no code to write.
