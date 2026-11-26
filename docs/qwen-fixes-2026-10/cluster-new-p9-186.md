FIXED

# new-p9-186: unrouting a bus leaves old audio in its delay line

## Verdict and evidence
REAL. `InstrumentOutputBuses::storeRoute` (src/core/InstrumentOutputBuses.cpp, around :160) only replaced a slot when it was absent or the block size changed. Publishing channel id -1 left the slot (and its `LatencyCompensation` delay ring) in place; `routedBuffer()` returns null for a -1 route so `send()` stopped advancing the ring, and a later route reused the ring with its old history. The same stale ring was kept when a route moved from one channel to another, and when a route's channel was deleted and the bus was routed elsewhere.

## Fix
`storeRoute` now also publishes a fresh slot whenever the channel id changes (`routeChanged`). The replacement is built before the model-change guard and installed under it, exactly like the existing size-change replacement, so nothing is allocated or freed on the audio thread. Re-publishing the same channel keeps the line. Siblings checked: `route()`, `clearRoutes()`, `loadSettings()` and `setLayout()` shrink all go through `storeRoute`, so they are covered; `send()`/`deliver()` unchanged. Saved format unchanged.

## Regression test
`InstrumentRoutingTest::outputBusReroutedDelayLineStartsEmpty` (tests/src/core/InstrumentRoutingTestParts1.h): channel with one period of PDC delay, deliver a loud block, unroute, reroute, deliver silence; the channel buffer must be 0. Deterministic, no timing.

## Commands (all logs in the run log folder)
- RED (fix absent): build-1.log EXIT=0; `ctest -R InstrumentRouting` test-red.log EXIT=8: `Actual (channel->m_buffer[frames - 1][0]): 0.5  Expected (0.0f): 0` at InstrumentRoutingTestParts1.h(211).
- GREEN: build-2.log EXIT=0; sibling targets build-3.log EXIT=0; `ctest -R "InstrumentRouting|InstrumentLatencyPdc|PdcMixer|InstrumentLayerZonePublish"` test-green2.log EXIT=0 (100% passed, 4 of 4).
- 4b (prove.sh, prove.out): `git apply -R fix.patch` EXIT=0; build-4 EXIT=0; test-red2 EXIT=8 (FAIL without fix); `git apply fix.patch` EXIT=0; build-5 EXIT=0; test-green3 EXIT=0 (PASS with fix).
- Gates: feature_check.py (gates.log) EXIT=0, RESULT: PASS (all 8 rows PASS).

## Audio thread / undo
Nothing new on the audio thread: the change is control-thread only, under the existing ChangeGuard. No commands added, no undo change. TSan not run locally; reasoning as above (same publication pattern as the existing slot swap).

## Other findings
None.

Delegation: Haiku ran the RED build/test, the GREEN build/test, and the proof script (revert/rebuild/reapply); I verified exit codes against the logs' summaries and the diff.
