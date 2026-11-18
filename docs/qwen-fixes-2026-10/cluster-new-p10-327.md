# Cluster new-p10-327

Verdict: **FIXED**

## File and line evidence

- `src/core/Rack.cpp:391` accepts rack versions 1 and 2; invalid or newer versions are left uninterpreted and retained as opaque XML at `src/core/Rack.cpp:396`.
- `src/core/Rack.cpp:344` imports the retained rack into the destination document unchanged on save. `src/core/Rack.cpp:223` clears the retained element when the rack is explicitly cleared.
- `include/Rack.h:217` stores the unsupported rack element.
- `tests/src/core/RackTest.cpp:619` covers version, unknown attribute, unknown child, and refusal to load the future rack as a live version-2 rack; the round-trip assertions start at line 648.

An absent version attribute remains compatible with version 1. Unsupported or malformed versions keep their original rack element so a later build can read it.

## Regression evidence

The RED run was made before the production change. It failed on the saved version: actual `"2"`, expected `"3"`.

| Run | Exact command (run from `build/tests`) | Exit | Result |
|---|---|---:|---|
| RED before fix | `ctest -R '^RackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-327.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 8 | `RackTest::anUnsupportedRackVersionSurvivesSave` failed, actual version 2, expected 3. |
| Negative control with production patch reversed | `ctest -R '^RackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-327.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 8 | Same assertion and actual/expected values. |
| GREEN after restoring the patch | `ctest -R 'Rack' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-327.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 | 5/5 affected-family tests passed: RackMacrosTest, RackTest, RackZonesTest, RackZoneRoutingTest, RackMenuTest. |

The initial post-fix family run used the same `ctest -R 'Rack' --output-on-failure` command and passed 5/5 (`test-2.log`, exit 0). The negative control was performed by generating `fix.patch` from only the production files, applying it in reverse, rebuilding `zene` and `RackTest`, and then restoring the patch before the final rebuild.

The patch reversal command exited 0:

```sh
git diff -- include/Rack.h src/core/Rack.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-327.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-327.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

The restore command also exited 0:

```sh
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-327.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

## Build and gate commands

The regression target built before RED:

```sh
cmake --build build --target RackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-327.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

Exit: 0.

The negative-control rebuild was:

```sh
cmake --build build --target zene RackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-327.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

Exit: 0.

The final restored build command, which rebuilt `zene` and configured targets whose sources include `Mixer.h`, `Rack.h`, or the rack test support header, was:

```sh
cmake --build build --target zene ArrangingCatchUpTest AudioBusHandleTest ClapEffectIntegrationTest ControlSuccessPathsTest ControlVcaCommandsTest ControlVcaEditGroupsTest ControlVcaLockedEditsTest CustomAccessTest DawProjectInterchangeRoundTripTest DeviceCopyTest DeviceDspTest DeviceMovesUndoTest DevicePanelTest DeviceToolsTest EffectHostingMenuTest EngineRenderBenchmarkTest FeedbackModeActionTest HardwareIoTest InputMonitorTest InstrumentLayersCommandsTest InstrumentRoutingTest MixerAbRegressionTest MixerChannelPanTest MixerConcurrencyTest MixerListenTest MixerModesTest MixerRoutingBackwardCompatTest MixerSendsOnFadersTest MixerStripTest MixerToolsTest MixingRoutingTest ModulationLayerTest ModulatorPanelTest PatcherCommandsTest PdcMixerTest PhaseDSidechainTest PhaseFChannelScaleTest ProjectOpenIntegrityTest ProjectStateTest RackMacrosTest RackMenuTest RackTest RackZoneRoutingTest RackZonesTest RealtimeSnapshotTest RecordToolsTest RoutingGraphLiveTest RoutingGraphScheduleTest SampleAccurateAutomationTest ScriptDawBindingTest SurroundTest TrackFolderTest UndoHonestyTest VcaGroupTest VcaRefreshAtomicityTest VcaStripViewTest W3EngineTest XyPadPairTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-327.r1/logs/build-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

Exit: 0. The same target list with `build-3.log` also exited 0 before the negative control. A bare `cmake --build build` attempt exited 97 because the workspace guard requires named targets; no compiler error was reported.

The blocked full-build command was:

```sh
cmake --build build > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-327.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"
```

Exit: 97.

| Gate | Exact command | Exit |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-327.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-327.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |

## Files committed

- `include/Rack.h`
- `src/core/Rack.cpp`
- `tests/src/core/RackTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-327.md`
