# Bar 5 regressions on the M0 integration line (2026-10-12)

Branch `bugs/m0-fix-bar5-regressions`, starting at train 2's tip `190242990`. Base ctest bar: 12 failing
tests of 503 (`train2bar5/ctest.log`). Final full ctest on this branch: **507 of 508 passed**; one
executable fails, `ScriptDawRemovalUndoTest`, with one case BLOCKED (row 8 below).

Red and green exits are ctest / test-binary exits (8 = failures, 0 = pass). Logs are in the scratch
folder `scratchpad/luna-bar5-fix/`. Reds were produced by reverting only the product change of the
row's commit, rebuilding, and running; greens after restoring and rebuilding.

## Rows

| # | Test(s) | Root cause | Fix commit | Red exit | Green exit |
| --- | --- | --- | --- | --- | --- |
| 1 | AudioBusPlayHandleQueueTest (5 cases: `capacityDeferredHandleIs*`, `queuedRemovePlayHandleCancelsCapacityDeferredHandle`) | `AudioEngine::disposePlayHandle` and `clearNewPlayHandles` downcast every handle typed `SamplePlayHandle` and read `isPooledVoice()`. `DeferredTrackHandle` (test) and `SampleRecordHandle` (`src/core/SampleRecordHandle.cpp:96`) carry that type tag but are not `SamplePlayHandle` objects, so the read was out of bounds. The deferred handle was recycled into the pool instead of destroyed (test assertion at `AudioBusPlayHandleQueueTest.cpp:460`). Old sites: `src/core/AudioEngine.cpp:259` and `:1005`. | `28d0b9552` | 8 (`redA-clean.log`, pool hunk reverted; test binary rebuilt) | 0 (`ubonly2.log`, test binary rebuilt; full run) |
| 2 | RecordingRealtimeTest (`!clip->isRecord()` cases, `loopWrapMakesEachPassATakeOnALane` and others) | Same cause as row 1: a recording's `SampleRecordHandle` was read through the `SamplePlayHandle` downcast, so it was recycled and never finished, and the clip stayed `isRecord()`. Test lines: `RecordingRealtimeTest.cpp:435`, `:476`, `:500`. | `28d0b9552` | 8 (`redA-clean.log`) | 0 (`mix.log`; full run) |
| 3 | RecordingLatencyTest (`aTakeRecordedThroughTheLoopbackLandsOnThePlayedTransient` and 4 more) | Same cause as row 1 (`RecordingLatencyTest.cpp:184`, `:148`, `:126`). | `28d0b9552` | 8 (`redA-clean.log`) | 0 (`mix.log`; full run) |
| 4 | ControlFeedbackCommands (`pdc.report` byte-for-byte baseline) | The engine-owned sample voice pool bus is an `AudioBusHandle` in `engine->audioBusHandles()`, so `trackInputReport()` (`src/core/ControlCommandsPdc.cpp:105`) listed it as a track input. Confirmed by `uboonly.log`: with row 1 applied and this change reverted, the test still fails. | `b5f7ca71c` | 8 (`redA-clean.log`, row 1 and this change reverted) | 0 (`mix.log`; full run) |
| 5 | ControlSessionLaunch, ControlSessionLifecycleTranscript, ControlSessionApiProof, ControlSuccessPaths | `InstrumentTrack::play()` (`src/tracks/InstrumentTrack.cpp:835`, old) called `getClip()` for a pattern the track holds no clip for. `getClip()` builds a `MidiClip` on the audio thread; its automatable models fail registration in `RealtimeScope` (`src/core/AutomatableModel.cpp:99` throws `std::bad_alloc`), and the audio thread terminates. A gdb `catch throw` run (`gdb-bt.txt`) shows the chain `AudioDummy::run` -> `renderNextPeriod` -> `Song::processNextBuffer` -> `PatternStore::play` -> `InstrumentTrack::play` -> `getClip` -> `InstrumentTrack::createClip` -> `MidiClip` -> `AutomatableModel`. | `ce82cfbd6` | 8 (`csl.log`, ControlSessionLaunch aborts; `red-or-current.log` shows all four failing before this change) | 0 (`ctl4.log`, all four; full run) |
| 6 | MixerListenTest::afterAndBeforeTheFader | **Test harness, not product code.** The automation prepass (`AudioEngine::prepareAutomatableModelBuffers`, `src/core/AudioEngine.cpp:533`, run per period at `:802`) consumes a setter's old/new ramp. `MixerListenTest::period()` skipped that step, so the first measured period replayed the fader ramp: the diagnostic run showed the first frame at 0.25 and the last at 0.1255 (`MixerListenTest.cpp:131`). Design intent is unchanged. | `7db8d1e1e` | 8 (`red-or-current.log`, before the harness change) | 0 (`run-MixerListenTest.log`; full run) |
| 7 | HardwareIoTest (4 cases: `aRoutedChannelPlaysOnItsPairAfterItsFader`, `theMasterMovesToItsOwnPair`, `aCueMixSumsItsSendsAtItsLevel`, `aCueFollowsSoloAndListen`) | **Test harness, not product code.** Same cause as row 6: `HardwareIoTest.cpp` `period()` skipped the prepass (test lines `:159`, `:184`, `:198`, `:214`). | `7db8d1e1e` | 8 (`red-or-current.log`) | 0 (`run-HardwareIoTest.log`; full run) |
| 8 | ScriptDawRemovalUndoTest::aRecordedDeviceIsBuiltWithoutTheAudioChangeLock | **BLOCKED.** The test requires building a recorded device (plug-in load plus settings restore) not to wait for the audio change lock. Since the automation prepass, both model construction and setters take `m_changeMutex`: `registerAutomatableModel` (`src/core/AudioEngine.cpp:439`, called from `AutomatableModel.cpp:99`) and `modelChangeGuard()` (`src/core/AutomatableModel.cpp:54-59`, used by setters such as `:387`). The test's holder thread keeps that lock for up to 10 s, so the build waits until the bound runs out (`ScriptDawRemovalUndoTest.cpp:500`). Not isolated which of the two lock sites holds the case. Fixing it needs a design decision: either model registration and setters on not-yet-attached models become lock-free (the automation design publishes old/new pairs lock-free, but the prepass still reads registered models), or the test's contract changes. Neither is a smallest correct change, so no fix was made and no test was weakened. | none | 8 (`full-ctest.log`; this case only) | n/a |
| 9 | DeviceToolsChainTest (3 cases: `theDeltaIsWhatTheDeviceChanges`, `aClipIsPrintedThroughAChain`, `aDeviceIsReplacedInItsSlot`) | **Test harness, not product code.** `runSteady()` processed blocks without the prepass or period advance, so the Volume ramp from `plugin.param_set` replayed into the first block (`DeviceToolsChainTest.cpp:232`, `:260`, `:277`). | `7db8d1e1e` | 8 (`red-or-current.log`) | 0 (`dtc-green.log`; full run) |

## Notes on the fixes

- Row 1-3: `find()` is a bounded scan of the pool's own slot array (no allocation, no lock), used in
  `disposePlayHandle` and `clearNewPlayHandles`. Reviewers should confirm the scan cost is acceptable on
  the render path; the pool is fixed at compile time.
- Rows 6, 7, 9 change harnesses, not assertions. The product behaviour they exercise is the designed one
  (a setter's value is ramped in the next serial prepass). The brief asked for product-code fixes; these
  three cases are harness drift, and the owner should accept this classification.
- Earlier uncommitted attempts in this worktree (from a cut-off run) were reviewed before use. Their
  Mixer.cpp latency filters (`isSampleVoicePoolBus` at two call sites) were not needed by any listed test
  (`mix.log`), so they were dropped. `isSampleVoicePoolBus()` is kept for the PDC report.

## Full-suite run

`cd build/tests && ctest -j6 --output-on-failure` on the tree at `7db8d1e1e`, after a full
`cmake --build build -j6` (exit 0). Result: **99% tests passed, 1 tests failed out of 508**, total
time 358.28 s. The only failure is `ScriptDawRemovalUndoTest` (row 8); its other 12 cases pass.
