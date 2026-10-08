# M0 coverage tests - lane "core" (2026-10-08)

Branch `050/m0-cov-tests-core`, worktree `zene-bunny-cmds2`, from `28cc3a0b5`. 29 files from
`research/reviews/m0-2026-10-08/coverage-below-entry-floor-train1.txt`.

## How it was measured

A Debug + `WANT_COVERAGE=ON` + Qt6 build in `build-coverage` of this worktree, configured the way the gate's own
run was: no CLAP headers and no VST3 SDK (`-DLMMS_CLAP_PATH` / `-DLMMS_VST3_SDK_PATH` pointed at nothing, which is
what the default `build-coverage/clap` amounted to). Only this lane's tests and the existing tests that touch these
files were run (24 ctest entries, all passing, listed in the "skips" note below), then `lcov --capture` with the flags
of `tests/run-coverage.sh`. The gate's number also includes every other test and the Python socket transcripts, so it
can only be higher than the "now" column. "Before" is the gate's figure for the same file.

## Result

| File | Before | Now | Test(s) | Main paths covered / not coverable / bugs |
| --- | --- | --- | --- | --- |
| include/ProjectSection.h | 0.0 | 100 | ProjectSectionTest | registry idempotence, `clearAll`, save/load/clear through the real Song, reset-on-absence, virtual destructor |
| include/RackZoneFeed.h | 27.3 | 100 | RackZoneFeedRunTest | `run()` template over real note handles |
| include/ScriptDawBindings.h | 44.4 | 66.7 | ScriptDawEffectsTest | `LuaEffect`/`LuaEffectChain` inline accessors |
| plugins/InsertRecorder/InsertRecorder.h | 0.0 | 100 | ReceiverEffectTest | `controls()`, `recorder()` (a take recorded and its name saved/loaded) |
| plugins/Receiver/Receiver.cpp, Receiver.h | 25.0 / 33.3 | 100 / 100 | ReceiverEffectTest | descriptor, `processImpl` mix/replace/no-sidechain, arithmetic measured on real buffers |
| plugins/Receiver/ReceiverControls.cpp, .h | 38.5 / 40.0 | 100 / 80 | ReceiverEffectTest | load/save of level and replace, `controlCount`, `nodeName`; `createView` (a GUI dialog) not built |
| src/core/ControlCommandsBounceLand.cpp | 34.9 | 100 | ControlBounceRenderTest | land, range, mute/undo/redo, every refusal, failed render leaves the project untouched |
| src/core/ControlCommandsTrackFlatten.cpp | 32.3 | 96.9 | ControlBounceRenderTest | replace-in-place, undo/redo restoring index/name/clips, refusals; "too large to keep for undo" branch not reached |
| src/core/ControlCommandsRangeBounce.cpp | 26.8 | 100 | ControlBounceRenderTest | two tracks rendered together, `mute_sources`, undo/redo, no range, failed render |
| src/core/ControlCommandsSubprojects.cpp | 43.8 | 94.1 | ControlBounceRenderTest | add, list, stale, refresh, undo/redo, persistence, self and nested-loop refusal, failed render |
| src/core/ControlCommandsMidiReconnectShared.h | 25.0 | 97.5 | ControlMidiReconnectSharedTest | resolver (name, identity, none, several), assignment JSON, bound-port lookup, lost assignment |
| src/core/ControlCommandsPluginBuses.cpp | 39.2 | 99.5 | ControlPluginBusesTest | list, route, unroute, undo/redo, no-op route, inactive bus activated, refused bus, create-channels |
| src/core/ControlCommandsSessionRecordInternal.h | 11.5 | 94.9 | ControlSessionRecordLandTest | pairing incl. retrigger, unmatched stop, open start, capacity-full ring |
| src/core/ControlCommandsSessionRecordLand.cpp | 38.5 | 100 | ControlSessionRecordLandTest | empty ring, refusal that consumes nothing, landing, skipped columns, empty span, undo |
| src/core/ControlDeviceClap.cpp | 22.0 | 100 | ControlHostedDeviceCatalogueTest | listing, key building, every typed refusal (stand-in host modules, see below) |
| src/core/ControlDeviceVst3.cpp | 22.0 | 93.2 | ControlHostedDeviceCatalogueTest | same for VST3; the "no vst3 host" branch not reached (all VST3 stand-ins present) |
| src/core/McuSurface.cpp | 49.4 | 96.8 | McuSurfaceFeedbackTest | V-pot, solo, rec, select, transport, jog, inbox from the MIDI thread, dropped burst, motor/LED feedback, touched fader |
| src/core/PluginHostNotes.cpp | 45.5 | 100 | PluginHostMetadataTest | all counters incl. concurrent writers |
| src/core/PluginVendors.cpp | 0.0 | 100 | PluginHostMetadataTest | remember/lookup/replace/trim |
| src/core/PortMapping.cpp | 26.7 | 76.2 | PortMappingOrchestratorTest | gateway and LAN discovery, success/double-NAT message, renewal timing, UPnP failure message, PMP renewal against a loopback gateway. `start()` is NOT run: it would ask the real default gateway for a real port |
| src/core/PortMappingUpnp.cpp | 10.8 | 95.5 | UpnpMapperFakeRouterTest | SSDP, description, AddPortMapping/GetExternalIPAddress/Delete, renewal, 718, 725-then-permanent, non-IGD, unreadable description. The "no router answers" time-out is NOT tested (see below) |
| src/core/RackZoneFeed.cpp | 33.8 | 100 | RackZoneFeedRunTest | `noteOf`, claimed notes summed per chain, buffers released, unclaimed keep theirs, sample voices, wrong-size scratch |
| src/core/ScriptDawEffects.cpp | 15.1 | 96.3 | ScriptDawEffectsTest | chain/effect wrappers by position and id, parameters, invalid wrappers, wrapper kept across removal |
| tests/src/core/ControlDeviceCatalogueTest.cpp | 47.2 | 56.1 | itself (+ a new builtin-listing case) | the four hosted cases still skip: no CLAP/VST3 SDK in this configuration |
| tests/src/core/Lv2StateTest.cpp | 47.1 | 82.4 | itself (+ two new cases) | the stateful round trip still skips: no installed LV2 effect implements State |
| tests/src/core/SessionPlaybackRenderTest.cpp | 41.6 | 88.7 | itself | cause fixed (below); both render cases now run |
| tests/src/core/VideoTestClip.h | 13.8 | 100 | VideoTestClipTest | the in-process libavcodec encoder, read back (streams, duration, first frame red, last blue) |

## Causes found for the test files under the floor

* `SessionPlaybackRenderTest`: its target is given `LMMS_TEST_PLUGIN_DIR` by CMake but `initTestCase` never
  set `LMMS_PLUGIN_DIR`, so `tripleoscillator` was found only if the caller happened to export that variable
  (the machine rule is to export nothing). Both render cases skipped ("no sounding instrument"). Fixed in the test.
* `ControlDeviceCatalogueTest`: the coverage configuration has no CLAP headers and no VST3 SDK, so the four hosted
  cases skip by design. Not fixable inside the test; the hosted halves of the product code are now covered by
  `ControlHostedDeviceCatalogueTest` through stand-in host modules (`tests/src/plugins/FakeHostModule.cpp`), and the
  file was lifted over the floor by a builtin-listing case that uses the file's own `listedDevice` helper.
  (Alternatively `run-coverage.sh` could be given `-DLMMS_CLAP_PATH=.../buildtools/clap -DLMMS_VST3_SDK_PATH=...`.)
* `Lv2StateTest`: no installed LV2 effect on this box implements the State interface, so the round trip skips. Two
  cases that need no state were added (the `<lv2state>` element is written exactly when there is state; a second
  instance is found by URI).
* `VideoTestClip.h`: the machine has the `ffmpeg` program, so the in-process libavcodec encoder never ran.

## Findings (not fixed, product or other-lane tests)

1. `tests/src/core/W3EngineTest.cpp:83` `loadBuiltin` calls `plugin.load` with `{"plugin": id}`; the argument is
   `device`. The load fails, `postFaderInsertsAreAddressedAndSavedWithTheChannel` therefore skips ("this build loads no
   amplifier module") on every machine. With the argument corrected (tried, then reverted - not in this lane's files)
   the case runs and FAILS at line 250: after `plugin.bypass`, `control.undo`, a save/load of the post-insert chain on
   another channel and a second `control.undo`, the effect loaded onto `ch-N/post` is still there (an undo straight
   after the load does remove it). Possibly a real undo defect for post-fader inserts, possibly the test's step
   ordering; unverified.
2. `ScriptDawBindingTest::aScriptsLoadedEffectIsUndoneOnTheChannelItNames` skipped in the gate's coverage run with
   `libamplifier.so` built; here, with `amplifier` built, it does not skip. Not reproduced; `ScriptDawEffectsTest`
   does not depend on any plug-in module.
3. A hazard, measured: `UpnpMapper::map()` multicasts an SSDP search to the real LAN. With a silent fake router,
   the real router at 192.168.0.1 answered and received a real AddPortMapping (it replied UPnP error 501). That case
   was removed from the test; with the fake answering (microseconds) the mapper takes the fake's answer first.

## New test infrastructure

* `ControlBounceRenderTest` has its own `main()`: when its binary is run as `<exe> render <project> -o <out>` (which
  BounceInPlace and `project.subproject_add` do, because the render is a child process) it answers like the CLI
  renderer - a tone at 0.1 per audio track left unmuted - so the product's serialise / mute-others / trim / land code
  runs for real and the written level proves which tracks were audible.
* `tests/src/plugins/BusesTestInstrument.cpp` (instrument with extra output buses) and `FakeHostModule.cpp`
  (stand-ins for `clapeffect`, `vst3effect`, `vst3instrument`; deliberately no `clapinstrument`), both modules in
  directories of their own, never installed. No product code was changed anywhere in the lane.

Not run: the full suite or the gates beyond the six cheap ones (fork-sources, all-sources, unregistered-tests,
complexity, file-length, no-tautology), which passed before every commit.
