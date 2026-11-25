FIXED

# Cluster 5743 - `rack.wrap` can reorder selected effects

## Verdict: REAL, FIXED
- Cause: `namedDevices` (`src/core/ControlCommandsRackWrap.cpp:56-80`) pushed devices in the order of the `plugins` argument.
  `wrap` (`:147`) captured state XML (`states << controlEffectStateXml(effect)`) and recreated the devices (`recreateDevices`) in that
  order, so `plugins: [B, A]` on a chain A,B produced a chain B,A. The file header (`:24-28`) and the command description
  (`:181-186`) promise "in order".
- Fix: at the end of `namedDevices` the resolved devices are `std::stable_sort`ed by their position in `target.chain->effects()`
  (the source chain). Every device is already verified to be in that chain, so the position lookup is always valid. With `plugins`
  left out the chain order is used as before; duplicate / foreign-chain refusals are unchanged. No command was added, no schema change,
  nothing on the audio thread (control-thread code only). Undo stays one step (`removeDevicesRecorded` receives the same set, now ordered
  like the chain, so the undo puts them back at their slots as before).
- Siblings checked: `plugin.copy` / `chain.copy` take their own argument paths (`ControlDeviceMoves.h`); this finding concerns rack.wrap only.

## Regression test
`tests/src/core/DeviceCopyTest.cpp` `wrappingKeepsTheSourceChainOrder`: loads two effects on a fresh channel, bypasses the first, wraps with
`plugins: [second, first]`, asserts the new rack chain's device 0 is the bypassed one (the first) and device 1 is enabled. Deterministic: no timing.
`tests/CMakeLists.txt`: `DeviceCopyTest` now depends on the `amplifier` module target. Without it a build of just `zene DeviceCopyTest`
loads no effect and every effect test (including this one) SKIPs (seen in `test-1.log`: 10 skipped).

## Commands and EXIT codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/5743.r1/logs)
- `cmake --build build --target zene DeviceCopyTest -j4` (build-1.log) EXIT=0; `ctest -R DeviceCopyTest` from build/tests (test-1.log) EXIT=8: no amplifier module built, new test SKIPped, unrelated `aChainIsBypassedAndCleaned` failed at `DeviceCopyTest.cpp:580` (`loadAnEffect` empty) - same missing-module cause.
- `cmake --build build --target amplifier -j4` (build-2.log) EXIT=0; `ctest -R DeviceCopyTest` (test-2.log) EXIT=8: RED, before the fix: `wrappingKeepsTheSourceChainOrder` FAIL at `DeviceCopyTest.cpp:460` (`!wrappedEffects[0]->isEnabled()` FALSE; 15 passed, 1 failed, 1 skipped).
- GREEN: `cmake --build build --target zene DeviceCopyTest -j4` (build-3.log) EXIT=0; `ctest -R DeviceCopyTest` (test-3.log) EXIT=0.
- 4b: `git diff -- src/core/ControlCommandsRackWrap.cpp > .../fix.patch && git apply -R .../fix.patch` EXIT=0; build (build-4.log) EXIT=0; ctest (test-4.log) EXIT=8, FAIL at `DeviceCopyTest.cpp:460` (the same test).
  `git apply .../fix.patch` EXIT=0; build (build-5.log) EXIT=0; ctest (test-5.log) EXIT=0.

## Gates
`python3 .../feature_check.py <worktree>` (gates-1.log) EXIT=1: BLOCKED, not a code result. The script runs
`git -C .../gate-baselines/tree checkout -q --detach 9913d36d53...` and the lane's bunny-guard refuses it
("is not allowed in a Bunny lane"), so it died with a CalledProcessError before printing a table. I did not work around the guard.
Manual checks: no new file (so no tests/fork-sources.txt entry needed; `src/core/ControlCommandsRackWrap.cpp` is already listed at
`tests/fork-sources.txt:1866`); the only new function logic is two small lambdas in `namedDevices` (CCN well under 10).

## Other findings
- `DeviceCopyTest::aChainIsBypassedAndCleaned` (`DeviceCopyTest.cpp:580`) hard-fails (not skips) when no effect module is built; with the new `amplifier` dependency it passes here.

## Self-check
- Red/green: with `ControlCommandsRackWrap.cpp` reverted the test fails (test-4.log, `ctest` EXIT=8, `DeviceCopyTest.cpp:460`); with the fix it passes (test-3.log and test-5.log, EXIT=0).
- Deterministic: no timers, sleeps or threads. It drives `rack.wrap` through `ControlRegistry::invoke` on a fresh channel and tells the two devices apart by a bypass flag set via `plugin.bypass`.
- Cause and completeness: `ControlCommandsRackWrap.cpp` holds only `rack.wrap` (`registerRackWrapCommand`); the only path that resolves the device list is `namedDevices` (called once, from `devicesToWrap`, from `wrap`), so ordering there covers the named and the omitted-`plugins` cases. The `rack.*` commands the check lists (add_chain, get_state, layer_*, macro_*, remove_chain) live in other files, share no code with this change, and are untouched.
- Collateral: no saved-data or schema change; duplicate and foreign-chain refusals are unchanged; callers that omit `plugins` or pass them in chain order get exactly the previous result (stable sort of an already ordered list). No new limit.
- Verdict is FIXED, not a not-real verdict, so no ALREADY-FIXED/FALSE-POSITIVE reference is needed.
- Unresolved: the `gates (feature_check.py)` item stays FAIL: the script's internal `git checkout --detach` is refused by bunny-guard (see Gates), so I could not produce its table. Unverified: complexity/file-length gate numbers (checked by hand only).
- Delegation: Haiku ran every build and ctest (build-1..5, test-1..5) and the revert/re-apply proof; I read the logs' key lines, the diff stat and wrote the test, fix, report and manifest.
