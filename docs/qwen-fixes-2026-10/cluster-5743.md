FIXED

# Cluster 5743 - `rack.wrap` can reorder selected effects

## Verdict: REAL, FIXED
- Cause: `namedDevices` (`src/core/ControlCommandsRackWrap.cpp:56-86`) pushed devices in the order of the `plugins` argument.
  `wrap` (`:153`) captured state XML (`states << controlEffectStateXml(effect)`) and recreated the devices (`recreateDevices`) in that
  order, so `plugins: [B, A]` on a chain A,B produced a chain B,A. The file header (`:24-28`) and the command description
  (`:187-192`) promise "in order".
- Fix: at the end of `namedDevices` the resolved devices are `std::stable_sort`ed by their position in `target.chain->effects()`
  (the source chain). Every device is already verified to be in that chain, so the position lookup is always valid. With `plugins`
  left out the chain order is used as before; duplicate / foreign-chain refusals are unchanged. No command was added, no schema change,
  nothing on the audio thread (control-thread code only). Undo stays one step (`removeDevicesRecorded` receives the same set, now ordered
  like the chain, so the undo puts them back at their slots as before).
- Siblings checked: `plugin.copy` / `chain.copy` take their own argument paths (`ControlDeviceMoves.h`); this finding concerns rack.wrap only.

## Regression test
`tests/src/core/DeviceMovesUndoTest.cpp` `wrappingKeepsTheSourceChainOrder` (round 2: moved from `DeviceCopyTest.cpp`, which it had
pushed over the 500-line ratchet, 495 -> 514; `DeviceCopyTest.cpp` is back to 495). On a fresh channel it loads `amplifier` then
`bassbooster` by name (QVERIFY2 if a fixture is missing, so it cannot silently skip), wraps with `plugins: [bassbooster, amplifier]`, and
`QCOMPARE`s the new rack chain's device names with `{amplifier, bassbooster}`. Deterministic: no timing, sleeps or threads.
`tests/CMakeLists.txt`: `DeviceCopyTest` and `DeviceMovesUndoTest` depend on `amplifier`, and `DeviceMovesUndoTest` on `bassbooster`,
so a targeted build of those test targets has its fixtures.

## Commands and EXIT codes (round 2; logs in /home/kruzzzzy/.config/kads/zene/state/runs/5743.r2/logs)
- `bash tests/file-length-gate.sh --check` (flen-3.log) EXIT=0: "PASS (check mode: no regressions; baseline not written)".
- GREEN: `cmake --build build --target zene DeviceCopyTest DeviceMovesUndoTest -j4` (build-3.log) EXIT=0; from build/tests
  `ctest -R 'DeviceCopyTest|DeviceMovesUndoTest' --output-on-failure` (test-2.log) EXIT=0, "100% tests passed out of 2".
- RED (4b): `git diff 9913d36d53 9bb33dd3e9 -- src/core/ControlCommandsRackWrap.cpp > logs/fix.patch && git apply -R logs/fix.patch` EXIT=0;
  build (build-4.log) EXIT=0; ctest (test-3.log) EXIT=8, "1 tests failed out of 2" (DeviceMovesUndoTest):
  `FAIL! DeviceMovesUndoTest::wrappingKeepsTheSourceChainOrder() Compared lists differ at index 0. Actual "bassbooster", Expected "amplifier"`,
  `Loc: tests/src/core/DeviceMovesUndoTest.cpp(182)`.
- `git apply logs/fix.patch` EXIT=0; build (build-5.log) EXIT=0; ctest (test-4.log) EXIT=0, "100% tests passed out of 2"; `git status --short` empty.
- Round-1 logs (runs/5743.r1/logs) no longer exist; the round-1 RED/GREEN on `DeviceCopyTest.cpp` is superseded by the run above.

## Gates
`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (gates-1.log) EXIT=0: fork-sources, all-sources-reproduce,
no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity ("no new item in your files") and file-length ("no new item in
your files") all PASS; RESULT: PASS. No new file, so no registration entry is needed (`src/core/ControlCommandsRackWrap.cpp` is already in
`tests/fork-sources.txt`).

## Other findings
- `DeviceCopyTest::aChainIsBypassedAndCleaned` (`DeviceCopyTest.cpp:580`) hard-fails (not skips) when no effect module is built; with the new `amplifier` dependency it passes here.

## Self-check
- Red/green: with the `ControlCommandsRackWrap.cpp` hunk reversed the test fails (test-3.log, ctest EXIT=8, `DeviceMovesUndoTest.cpp:182`); with the fix it passes (test-2.log and test-4.log, EXIT=0).
- Deterministic: no timers, sleeps or threads. It drives `rack.wrap` through `ControlRegistry::invoke` on a fresh channel and tells the two devices apart by descriptor name.
- Cause and completeness: `ControlCommandsRackWrap.cpp` holds only `rack.wrap` (`registerRackWrapCommand`); the only path that resolves the device list is `namedDevices` (`:56-86`), so ordering there covers the named and the omitted-`plugins` cases. Other `rack.*` commands live in other files and share no code with this change.
- Collateral: no saved-data or schema change; duplicate and foreign-chain refusals are unchanged; callers that omit `plugins` or pass them in chain order get exactly the previous result (stable sort of an already ordered list). No new limit, nothing on the audio thread, no command added.
- Verdict is FIXED, not a not-real verdict, so no ALREADY-FIXED/FALSE-POSITIVE reference is needed.
- Gates: feature_check.py EXIT=0, RESULT: PASS (gates-1.log); file-length gate EXIT=0 (flen-3.log).
- Delegation: round 2: Haiku ran every build and ctest and the reverse/re-apply proof (build-3..5, test-2..4); I applied the test move, ran the file-length gate and feature_check, checked the logs' key lines, and wrote the report, manifest and commits. No Sonnet or DeepSeek use: the test was already written (leftover patch), so there was no engineering left to hand off.
