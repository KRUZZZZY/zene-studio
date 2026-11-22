FIXED

# Cluster 5104: monitoring an unavailable wide-input channel fell back to the stereo left

## Verdict: REAL
`copyInput` (src/core/InputMonitor.cpp, old line 70) tested `wideFrames > 0 && channel < wideChannels`. With wide capture active
and a route on a channel the capture does not carry (e.g. channel 2 of a 2-channel capture), the condition was false and the
code fell through to the stereo bus, reading its left side (`side = channel == 1 ? 1 : 0` -> 0). The test failed on that
before the fix (test-4b-red.log: passed = 2, expected 0).

## Fix
- include/InputMonitor.h: `MonitorInputSource` and `copyMonitorInput()` (pure seam; the anonymous `copyInput` became it, `sourceOf(engine)` builds the source).
- src/core/InputMonitor.cpp `copyMonitorInput`: while `wideFrames > 0`, a channel outside `[0, wideChannels)` returns 0 frames (the buffer stays the silence `play()` already filled). The stereo bus is read only while wide capture is inactive. Inactive behaviour is unchanged (channel 1 = right, anything else = left).
- Realtime: no allocation, lock or log added; the function is `noexcept` and reads the same engine accessors as before.
- Other callers: `copyInput` had one caller (`InputMonitorHandle::play`). `framesPassed` now counts 0 for the silent case. No command, save format or schema changed.

## Regression test
tests/src/core/InputMonitorTest.cpp `anUnavailableWideChannelIsSilenceNotTheStereoLeft` (pure, no timing or device thread): covers wide channel read, unavailable channel -> silence, and stereo path when wide capture is inactive.

## Commands (logs in /home/kruzzzzy/.config/kads/zene/state/runs/5104.r1/logs)
- RED (seam committed with the old behaviour, f40c27117b): `cmake --build build --target zene InputMonitorTest -j4` EXIT=0; `cd build/tests && ctest -R InputMonitorTest --output-on-failure` (test-red.log) EXIT=8.
- GREEN: same build EXIT=0; `ctest -R InputMonitor` (test-green.log) EXIT=0.
- 4b: `git diff -- src/core/InputMonitor.cpp include/InputMonitor.h > fix.patch && git apply -R fix.patch` EXIT=0; rebuild EXIT=0; `ctest -R InputMonitorTest` (test-4b-red.log) RED_EXIT=8; `git apply fix.patch` EXIT=0; rebuild EXIT=0; ctest (test-4b-green.log) GREEN_EXIT=0.
  (fix.patch holds only the behaviour change; the seam is in the earlier commit.)
- Header includers rebuilt: `cmake --build build --target zene InputMonitorTest ArrangingSpacerReferenceTest ArrangingCatchUpTest -j4` EXIT=0 (a full `cmake --build build` is refused by the bunny-guard, so named targets only).
- Family: `ctest -R "InputMonitor|ArrangingSpacerReference|ArrangingCatchUp"` (test-family.log) EXIT=8: InputMonitorTest and ArrangingSpacerReferenceTest pass; ArrangingCatchUpTest fails in newDocumentsCarryTheirTypesTemplate (cannot write ~/.cache/ArrangingCatchUpTest/.../mastering.mpt) and queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie (tripleoscillator loads as dummyinstrument). Both look environmental and unrelated to monitoring; I did not run that test at baseline, so that is unverified.
- Gates: `python3 .../feature_check.py <worktree>` (gates.log) EXIT=0, RESULT: PASS (all 8 rows PASS).

## Self-check
- Red/green: with the product change reverted the test fails (RED_EXIT=8), with it applied it passes (GREEN_EXIT=0); see Commands.
- Deterministic: the test calls the pure `copyMonitorInput` seam with injected arrays; no device thread, timers or sleeps.
- Complete: the only caller of the old `copyInput` was `InputMonitorHandle::play` (src/core/InputMonitor.cpp); the stereo-inactive path is unchanged, and the channel is also guarded against negative values.
- No collateral: no save format, schema or command touched; the stereo fallback with wide capture inactive behaves as before (asserted in the test); no limit added.
- Verdict is FIXED, not ALREADY-FIXED/FALSE-POSITIVE.
- Sibling commands: I changed no command files (only InputMonitor.h/.cpp and the test), so the listed app.*/arrangement.* commands are not affected.

## Other findings
- ArrangingCatchUpTest fails in this environment (above); not touched.
