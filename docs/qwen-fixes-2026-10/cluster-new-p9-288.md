# Cluster new-p9-288

**Verdict: FIXED**

## Finding and evidence

The finding is real. In `src/core/MixerStrip.cpp:141-151`, `sendLevel()` multiplied the saved pre-fader peak by the channel fader for post-fader sends. When post-fader inserts are baked, the send source is already the post-fader output. `updateMeters()` now retains both the pre-fader source peak and the baked output peak (`src/core/MixerStrip.cpp:297-304`), and `sendLevel()` uses the baked peak times the route amount for baked, non-feedback post-fader routes (`src/core/MixerStrip.cpp:145-149`). The silence path clears the new readings and validity flag (`src/core/MixerStrip.cpp:316-327`). Pre-fader sends and feedback routes retain the prior calculation.

The regression at `tests/src/core/MixerStripTest.cpp:174-200` runs a deterministic gain effect after the fader and compares the send meter with the baked output peak times the route amount. It fails against the original product code and passes with the fix.

## Verification

`MixerStripTest` is the affected CTest family: one registered test ran. The valid RED run reported 11 Qt cases passed and the new assertion failed; the fixed GREEN run passed the registered CTest test.

| Purpose | Command and working directory | Exit | Log evidence |
| --- | --- | ---: | --- |
| Build the app and affected test | `cmake --build build --target zene MixerStripTest` (worktree) | 0 | `build-5.log` |
| RED before fix | `ctest -R '^MixerStripTest$' --output-on-failure` (`build/tests`) | 8 | `test-4.log`: the new assertion failed; 11 passed, 1 failed |
| Reverse-patch proof | `git diff -- include/Mixer.h src/core/MixerStrip.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-288.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-288.r1/logs/fix.patch` (worktree) | 0 | command result |
| RED with product patch reversed | `ctest -R '^MixerStripTest$' --output-on-failure` (`build/tests`, after `cmake --build build --target zene MixerStripTest`) | 8 | `test-6.log`: the same assertion failed; 11 passed, 1 failed |
| Reapply saved product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-288.r1/logs/fix.patch` (worktree) | 0 | command result |
| GREEN after fix | `ctest -R '^MixerStripTest$' --output-on-failure` (`build/tests`, after `cmake --build build --target zene MixerStripTest`) | 0 | `test-7.log`: 1/1 CTest tests passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` (worktree) | 0 | `complexity-gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` (worktree) | 0 | `file-length-gate-1.log` |

The final header-dependent test rebuild exited 0 (`build-11.log`). Its command was `cmake --build build --target VcaGroupTest ControlModulatorCommandsTest InstrumentRoutingTest DeviceToolsTest RackMenuTest RackMacrosTest ControlNoteExpressionCommandsTest MixerChannelPanTest ModulatorPanelTest XyPadPairTest ControlVcaCommandsTest FeedbackModeActionTest MixerListenTest DeviceDspTest CustomAccessTest SurroundTest PhaseFChannelScaleTest TrackFolderTest ClapEffectIntegrationTest RoutingGraphLiveTest RackTest RackZonesTest HardwareIoTest InstrumentLayersCommandsTest ControlVcaEditGroupsTest SampleAccurateAutomationTest ModulationLayerValueTest DeviceMovesUndoTest MixerConcurrencyTest ArrangingCatchUpTest DevicePanelTest MixerAbRegressionTest PhaseDSidechainTest VcaStripViewTest ModulationLayerTest ControlSuccessPathsTest UndoHonestyTest MixerToolsTest MixerSendsOnFadersTest VcaRefreshAtomicityTest MixingRoutingTest PdcMixerTest EngineRenderBenchmarkTest AudioBusHandleTest RackZoneRoutingTest DeviceCopyTest ScriptDawBindingTest PatcherCommandsTest RealtimeSnapshotTest DawProjectInterchangeRoundTripTest ControlVcaLockedEditsTest MixerStripTest InputMonitorTest MixerRoutingBackwardCompatTest W3EngineTest ModulationLayerProjectRoundTripTest ProjectOpenIntegrityTest MixerModesTest RecordToolsTest ProjectStateTest EffectHostingMenuTest RoutingGraphScheduleTest`.

The full build attempts `cmake --build build --target all` and `cmake --build build` both exited 97 without compiler output (`build-6.log`, `build-10.log`). The explicit `zene`/`MixerStripTest` builds and the 62 header-dependent test targets above succeeded.

An initial test draft also had a missing declaration include and targeted a channel not created by the fixture; those attempts are recorded in `build-1.log` and `test-1.log` and were corrected before the valid RED result.

## Files committed

- `include/Mixer.h`
- `src/core/MixerStrip.cpp`
- `tests/src/core/MixerStripTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-288.md`
