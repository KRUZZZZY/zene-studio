# Cluster 2466: failed instrument setup leaves a track in the shared test engine

Verdict: **FIXED**

## Evidence

The defect was in `tests/src/core/ReversibilityTestSupport.h`. Before this fix,
`addInstrumentTrackFor()` created a track with `track.add` and only then resolved and
loaded the device; on either failure path it returned an empty id but kept the track:

- `tests/src/core/ReversibilityTestSupport.h:101-108` (pre-fix): `const QString track = addTrack();` followed by `if (device.isEmpty()) { return QString(); }` and `return loaded.ok ? track : QString();`.
- A caller that treats the empty id as "skip" therefore leaked the track into the next test of the same binary. This is the reported failure path.

The fix is at the cause, in both failure paths:

- `tests/src/core/ReversibilityTestSupport.h:107`: the device is resolved **before** `addTrack()`. No loadable device means no track is created at all.
- `tests/src/core/ReversibilityTestSupport.h:114-124`: when `plugin.load` fails, `control.undo` reverses the `track.add` step this call just recorded, so the track is removed and no extra undo step is left behind. If no journal could take it back, `track.remove` is the fallback, so a track can never be left on the failure path.
- `tests/src/core/ReversibilityTestSupport.h:129`: `addInstrumentTrack()` delegates to the same implementation; its success-path result is unchanged.

### Callers of the changed helper (every one checked, none needed a change)

`revtest::addInstrumentTrackFor()` is called from `tests/src/core/ReversibilityTestSupport.h:129`
(`addInstrumentTrack()`, the entry point every caller uses) and from the two regression slots
in `tests/src/core/InstrumentLayerZonePublishTest.cpp`. `revtest::addInstrumentTrack()` is
called **31 times in 13 test files** - `ControlAutomationModesTest.cpp:105`,
`ControlAutomationScriptTest.cpp:108,421`, `ControlChainPresetTest.cpp` (6),
`InstrumentLayersCommandsTest.cpp:160`, `InstrumentLayerZonePublishTest.cpp:110`,
`ReversibilityUndoDeviceTest.cpp` (2), `ReversibilityUndoTest.cpp` (4),
`ScriptBindingsTest.cpp` (7), `SessionTuningTest.cpp` (2), `UndoHonestyStateTest.cpp:127,183`,
`AutomationEditorEditingTest.cpp:92`, `AutomationModeMenuTest.cpp:84`,
`InstrumentSetupUiTest.cpp:341`. All of them go through the one fixed implementation, so
all of them get both cleaned failure paths; the sibling helpers `addTrack()`,
`firstLoadableInstrument()`, `loadableDevices()` and `hasTrack()` are unchanged.

## Regression

`tests/src/core/InstrumentLayerZonePublishTest.cpp:81-103` - the two failure paths are now
**two independent QtTest slots**, as the round-1 QA review required. The previous version
put both paths in one slot, so its first `QVERIFY` returned from the slot and the
refused-load path never produced its own RED evidence.

| Slot | Path driven | Assertion |
| --- | --- | --- |
| `noDeviceInstrumentSetupLeavesNoTrack()` | `addInstrumentTrackFor(QString())` - no loadable device at all | empty id, and `track.list`'s count unchanged |
| `refusedDeviceLoadLeavesNoTrack()` | `addInstrumentTrackFor("dev-999999")` - a device id `plugin.load` refuses | empty id, and `track.list`'s count unchanged |

No timing, sleeps or races: the device id is injected, the observable is the track count
read before and after the call.

## Evidence table

All builds ran from the worktree root; all ctest runs ran from `build/tests`.
`LOGS` below is `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2466.r2/logs`.

| Check | Exact command and result |
| --- | --- |
| GREEN with the fix, split slots | `cmake --build build --target InstrumentLayerZonePublishTest -j20 > LOGS/build-1-split-green.log 2>&1; echo EXIT=$?` **EXIT=0**; `ctest -R '^InstrumentLayerZonePublishTest$' --output-on-failure > LOGS/test-1-split-green.log 2>&1; echo EXIT=$?` **EXIT=0** (1/1 CTest target passed) |
| Reversed-fix proof (RED) | `git apply -R LOGS/fix.patch; echo EXIT=$?` **EXIT=0**; `cmake --build build --target InstrumentLayerZonePublishTest -j20 > LOGS/build-2-reverted.log 2>&1; echo EXIT=$?` **EXIT=0**; `ctest -R '^InstrumentLayerZonePublishTest$' --output-on-failure > LOGS/test-2-reverted-red.log 2>&1; echo EXIT=$?` **EXIT=8**, **both slots fail independently**: `FAIL! noDeviceInstrumentSetupLeavesNoTrack()` (actual `trackCount()=1`, expected `0`, line 93) and `FAIL! refusedDeviceLoadLeavesNoTrack()` (actual `trackCount()=2`, expected `1`, line 102); `Totals: 3 passed, 2 failed` |
| Restored-fix proof (GREEN) | `git apply LOGS/fix.patch; echo EXIT=$?` **EXIT=0**; `cmake --build build --target InstrumentLayerZonePublishTest -j20 > LOGS/build-3-restored.log 2>&1; echo EXIT=$?` **EXIT=0**; `ctest -R '^InstrumentLayerZonePublishTest$' --output-on-failure > LOGS/test-3-restored-green.log 2>&1; echo EXIT=$?` **EXIT=0** (1/1 passed) |
| Affected family | `cmake --build build --target AutomationModeMenuTest DeviceOrganiserTest -j20 > LOGS/build-4-family.log 2>&1; echo EXIT=$?` **EXIT=0**; `ctest -R '^(InstrumentLayerZonePublishTest|AutomationModeMenuTest|DeviceOrganiserTest)$' --output-on-failure > LOGS/test-4-family.log 2>&1; echo EXIT=$?` **EXIT=0**, 3/3 targets passed |
| Header dependents | All 39 test targets whose sources include `ReversibilityTestSupport.h` rebuilt: `cmake --build build --target <39 targets> -j20 > LOGS/build-5-includers.log 2>&1; echo EXIT=$?` **EXIT=0**, no `error` line in the log |
| Gates (feature_check) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gate-1-feature-check.log 2>&1; echo EXIT=$?` **EXIT=0**, `RESULT: PASS`, 8/8 rows PASS (`complexity` and `file-length`: "no new item in your files") |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> 2466 > LOGS/submit-check-1.log 2>&1; echo EXIT=$?` **EXIT=0**, `RESULT: PASS - submit` (all rows PASS) |

The patch used for the reversed proof is `LOGS/fix.patch`: the diff that turns the
pre-fix helper (commit `391066c41`'s version of the file, i.e. the seam without the fix)
into the fixed helper. It was produced with `git diff` against the pre-fix blob, so
`git apply -R LOGS/fix.patch` removes the fix from the working tree (RED) and
`git apply LOGS/fix.patch` puts it back (GREEN). The regression stays in the tree during
both runs, so the reversed run is a real test failure (`EXIT=8`) and not a build failure.
This bug has no product-source hunk: the finding and the whole fix are in the shared test
support, so the patch is a test-support patch by necessity.

## Build and test commands

```text
# GREEN with the fix and the two independent slots
cmake --build build --target InstrumentLayerZonePublishTest -j20 > LOGS/build-1-split-green.log 2>&1; echo EXIT=$?  # EXIT=0
cd build/tests && ctest -R '^InstrumentLayerZonePublishTest$' --output-on-failure > LOGS/test-1-split-green.log 2>&1; echo EXIT=$?  # EXIT=0

# 4b: reverse the fix only (the regression stays), rebuild, run -> RED, both slots
git apply -R LOGS/fix.patch; echo EXIT=$?  # EXIT=0
cmake --build build --target InstrumentLayerZonePublishTest -j20 > LOGS/build-2-reverted.log 2>&1; echo EXIT=$?  # EXIT=0
cd build/tests && ctest -R '^InstrumentLayerZonePublishTest$' --output-on-failure > LOGS/test-2-reverted-red.log 2>&1; echo EXIT=$?  # EXIT=8 (2 failures)

# restore the fix, rebuild, run -> GREEN
git apply LOGS/fix.patch; echo EXIT=$?  # EXIT=0
cmake --build build --target InstrumentLayerZonePublishTest -j20 > LOGS/build-3-restored.log 2>&1; echo EXIT=$?  # EXIT=0
cd build/tests && ctest -R '^InstrumentLayerZonePublishTest$' --output-on-failure > LOGS/test-3-restored-green.log 2>&1; echo EXIT=$?  # EXIT=0

# affected family
cmake --build build --target AutomationModeMenuTest DeviceOrganiserTest -j20 > LOGS/build-4-family.log 2>&1; echo EXIT=$?  # EXIT=0
cd build/tests && ctest -R '^(InstrumentLayerZonePublishTest|AutomationModeMenuTest|DeviceOrganiserTest)$' --output-on-failure > LOGS/test-4-family.log 2>&1; echo EXIT=$?  # EXIT=0 (3/3)

# gates and completion check
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gate-1-feature-check.log 2>&1; echo EXIT=$?  # EXIT=0
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> 2466 > LOGS/submit-check-1.log 2>&1; echo EXIT=$?  # EXIT=0
```

## Round-2 changes (this round)

- `tests/src/core/InstrumentLayerZonePublishTest.cpp`: the combined
  `failedInstrumentSetupLeavesNoTrack()` slot is split into the two independent slots
  `noDeviceInstrumentSetupLeavesNoTrack()` and `refusedDeviceLoadLeavesNoTrack()`, so the
  refused-load path has its own RED evidence (the round-1 reversed run failed only in the
  no-device case, because the first `QVERIFY` returned from the slot).
- This report: the slot names, the caller count (31 call sites in 13 files, not "all six")
  and the log path are now correct.

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it.**
  `git apply -R LOGS/fix.patch` **EXIT=0**, build **EXIT=0**, ctest **EXIT=8** with two
  independent failures (`noDeviceInstrumentSetupLeavesNoTrack()` actual `1` expected `0`;
  `refusedDeviceLoadLeavesNoTrack()` actual `2` expected `1`); `git apply LOGS/fix.patch`
  **EXIT=0**, build **EXIT=0**, ctest **EXIT=0**. The fix is in
  `tests/src/core/ReversibilityTestSupport.h` (the only non-test file of this cluster is
  that shared test support - the finding itself is in the test support), so the reversed
  patch is that file's fix hunk.
- **The test is deterministic.** No wall-clock timing, sleeps or races. The seam is the
  device id argument of `addInstrumentTrackFor`: `QString()` drives the no-device path and
  `dev-999999` drives the `plugin.load` refusal. The observable is `track.list`'s count
  before/after, read in the same slot.
- **The fix is at the cause and complete.** `addInstrumentTrack()` - the entry point of
  all 31 call sites in 13 files - delegates to `addInstrumentTrackFor`, so every caller
  gets both cleaned failure paths. The success path still loads through the same
  `plugin.load` and returns the same track id. The two sibling helpers (`addTrack`,
  `firstLoadableInstrument`) and `hasTrack` are unchanged.
- **No collateral damage.** No product code, serialization or project-loading path
  changed. The helper only runs in tests. A successful setup is byte-for-byte the same
  sequence (`track.add`, then `plugin.load`); the only early return added is for a caller
  that had no device to load anyway. No new limit or check is applied to any caller.
- **Not an ALREADY-FIXED / FALSE-POSITIVE.** The pre-fix code at
  `tests/src/core/ReversibilityTestSupport.h:101-108` returns without removing the track
  on both failure paths; the RED run above measures the leak (`1` vs `0`, `2` vs `1`).
- **Undo / realtime.** The cleanup reverses exactly the one `track.add` step this helper
  recorded (one honest step, no extra step); it runs only in the test helper on the
  calling thread. Nothing touches the audio thread; no new control command and no
  allocation/lock/log on a realtime path were added.
