FIXED

# cluster new-p9-290: re-enabling an EQ band resumed stale filter history

## Verdict: REAL
`applyChannelEq` (src/core/MixerStrip.cpp:122) skipped a band whose gain was 0 (`continue`) and returned early when all three were 0,
so the band's 8 biquad memories (`MixerChannel::m_eqState`, include/Mixer.h:147) kept the last audio they saw. Raising the gain later ran
that old history through the new coefficients: a burst unrelated to the current signal.

## Fix (src/core/MixerStrip.cpp:122-139)
A zero-gain band now clears its 8 floats (`std::fill_n`) instead of leaving them. The all-flat early return was dropped, since the
per-band loop now handles it (it only clears three fixed-size slices: no allocation, lock or log on the audio thread). Output for non-zero
bands is unchanged. `eqResponseDb` (the display-only response) is unaffected. The only caller is `applyOutput` (MixerStrip.cpp:185).

## Regression test
`ConsoleStripTest::aReEnabledEqBandStartsFromCleanHistory` (tests/src/core/ConsoleStripTest.cpp): builds history with low +12 dB, flattens
all bands and checks all 24 state floats are 0, re-enables low +12 dB on silence and requires exact-zero output. Deterministic, no timing.
(First placed in MixerToolsTest.cpp, which pushed that file over the 500-line gate, so moved.)

## Commands (EXIT codes from my logs, in /home/kruzzzzy/.config/kads/zene/state/runs/new-p9-290.r1/logs)
- RED (fix reverted via `git apply -R fix.patch`): `cmake --build build -j4 --target zene ConsoleStripTest` EXIT=0 (build-red2.log);
  `cd build/tests && ctest -R ConsoleStripTest --output-on-failure` EXIT=8 (test-red2.log): FAIL at ConsoleStripTest.cpp(414), Actual 0.5, Expected 0.
- GREEN (fix re-applied via `git apply fix.patch`): `cmake --build build -j4 --target zene ConsoleStripTest MixerToolsTest MixerStripTest MixerStripFeaturesTest` EXIT=0 (build-green2.log);
  `ctest -R "ConsoleStrip|MixerTools|MixerStrip" --output-on-failure` EXIT=0, 4/4 passed (test-green2.log).
- Gates: `python3 .../feature_check.py <worktree>` EXIT=0, RESULT: PASS (gates2.log). An earlier run (gates.log, EXIT=1) failed file-length on the first test placement; fixed by moving the test.
- An earlier RED/GREEN pair with the test in MixerToolsTest (test-red.log EXIT=8, test-green.log) agrees.

## Not verified
Local TSan does not run here; the change touches only mixer-worker-owned state (`m_eqState`, "mixer worker only"), same thread as before.

Delegation: Haiku ran every build and ctest run (RED and GREEN) and reported exit codes and failing lines; I read the logs' results and wrote the fix, test and report.

## Other findings
None.

## Self-check
- RED/GREEN: with the fix reverted the test fails (ctest EXIT=8, test-red2.log, ConsoleStripTest.cpp(414) Actual 0.5 vs 0); with it applied it passes (ctest EXIT=0, 4/4, test-green2.log).
- Deterministic: fixed buffers of constant 0.5 / 0.0, a direct call to `mixerstrip::applyChannelEq` with 8 frames and model values set by `setValue`; no clock, sleep, thread or random input.
- Cause and completeness: the only stateful reader of `m_eqState` is `runBand` via `applyChannelEq` (MixerStrip.cpp:131); the only caller is `applyOutput` (MixerStrip.cpp:185). `eqResponseDb` is stateless. Every zero-gain band, including the all-flat case, now clears its memories.
- Collateral: `m_eqState` is runtime-only (not saved; include/Mixer.h:147), so project load is unaffected. No limit or check added; output for non-zero bands is bit-identical. No control command added; no audio-thread allocation/lock/log (a `std::fill_n` on a fixed `std::array`).
- Verdict is FIXED, so the ALREADY-FIXED/FALSE-POSITIVE item does not apply.
