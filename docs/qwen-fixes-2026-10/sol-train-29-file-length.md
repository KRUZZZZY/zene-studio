# Train 29 Gate 7 repair (failures 37–38)

At d16023e93b the unchanged `bash tests/file-length-gate.sh --check` exits 1
with 27 diagnostics across 26 unique paths (ControlShutdownHookTest is listed
twice in the source manifest). Local diagnostics reproduce hosted CI exactly.
The existing CI issue is blocked on obsolete Gate 4 evidence; no accepted
file-length repair was present. This repair addresses the source lengths here.

## Changes and reasons

- VST3 MIDI and parameter methods move into Vst3HostParameters.cpp. Both plugin
  modules and every direct-source test target compile the new translation unit.
- Process-lifetime registry shutdown hooks move together into ControlRegistryShutdown.cpp;
  the same function-local stores retain their lifetime and registration ordering.
- Take-lane XML read/write methods move into TakeLanePersistence.cpp.
- Wasm module loading moves into WasmSandboxModules.cpp, with the existing private
  Impl shared through WasmSandboxInternals.h. Target-specific testing macros are
  retained in both ordinary and directly compiled sandbox test targets.
- The 22 oversized QTest fixtures retain their original slot declarations, access
  sections and execution order. Complete method definitions move into adjacent
  implementation headers included once after the fixture declaration. They remain
  in the same translation unit, preserving anonymous helpers and test-local state.
  Every header is registered in both source manifests and is measured by the
  unchanged length and complexity gates. No excluded fragments or .inc files are used.
- Source lists and provenance declarations register the new files and build wiring.
  No gate executable, baseline, exemption, test assertion or resource limit changes.

The following existing files are changed for the split and wiring described above:

- `plugins/Vst3Effect/CMakeLists.txt`
- `plugins/Vst3Effect/Vst3Host.cpp`
- `plugins/Vst3Instrument/CMakeLists.txt`
- `src/core/CMakeLists.txt`
- `src/core/ControlRegistry.cpp`
- `src/core/TakeLane.cpp`
- `src/wasm/CMakeLists.txt`
- `src/wasm/WasmSandbox.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/fork-sources.txt`
- `tests/src/core/ArrangeToolsTest.cpp`
- `tests/src/core/CollabTransportTest.cpp`
- `tests/src/core/ControlAutomationScriptTest.cpp`
- `tests/src/core/ControlDeviceCatalogueTest.cpp`
- `tests/src/core/ControlEditCommandsTest.cpp`
- `tests/src/core/ControlShutdownHookTest.cpp`
- `tests/src/core/CustomAccessTest.cpp`
- `tests/src/core/DataFileSaveIntegrityTest.cpp`
- `tests/src/core/DeviceDspTest.cpp`
- `tests/src/core/DeviceToolsTest.cpp`
- `tests/src/core/DrumSynthTest.cpp`
- `tests/src/core/HardwareIoTest.cpp`
- `tests/src/core/InstrumentRoutingTest.cpp`
- `tests/src/core/LinkAudioStreamTest.cpp`
- `tests/src/core/NotePlaybackTest.cpp`
- `tests/src/core/ProjectContainerSaveTest.cpp`
- `tests/src/core/RackMacrosTest.cpp`
- `tests/src/core/RecordToolsTest.cpp`
- `tests/src/core/RevisionTimelineTest.cpp`
- `tests/src/core/UpnpMapperFakeRouterTest.cpp`
- `tests/src/core/VideoTrackTest.cpp`
- `tests/src/gui/ArrangerTimelineTest.cpp`

New measured implementation files:

- `plugins/Vst3Effect/Vst3HostParameters.cpp`
- `src/core/ControlRegistryShutdown.cpp`
- `src/core/TakeLanePersistence.cpp`
- `src/wasm/WasmSandboxInternals.h`
- `src/wasm/WasmSandboxModules.cpp`
- `tests/src/core/ArrangeToolsTestParts1.h`
- `tests/src/core/CollabTransportTestParts1.h`
- `tests/src/core/CollabTransportTestParts2.h`
- `tests/src/core/ControlAutomationScriptTestParts1.h`
- `tests/src/core/ControlAutomationScriptTestParts2.h`
- `tests/src/core/ControlDeviceCatalogueTestParts1.h`
- `tests/src/core/ControlDeviceCatalogueTestParts2.h`
- `tests/src/core/ControlEditCommandsTestParts1.h`
- `tests/src/core/ControlEditCommandsTestParts2.h`
- `tests/src/core/ControlShutdownHookTestParts1.h`
- `tests/src/core/ControlShutdownHookTestParts2.h`
- `tests/src/core/CustomAccessTestParts1.h`
- `tests/src/core/CustomAccessTestParts2.h`
- `tests/src/core/DataFileSaveIntegrityTestParts1.h`
- `tests/src/core/DataFileSaveIntegrityTestParts2.h`
- `tests/src/core/DeviceDspTestParts1.h`
- `tests/src/core/DeviceDspTestParts2.h`
- `tests/src/core/DeviceToolsTestParts1.h`
- `tests/src/core/DrumSynthTestParts1.h`
- `tests/src/core/HardwareIoTestParts1.h`
- `tests/src/core/HardwareIoTestParts2.h`
- `tests/src/core/InstrumentRoutingTestParts1.h`
- `tests/src/core/LinkAudioStreamTestParts1.h`
- `tests/src/core/LinkAudioStreamTestParts2.h`
- `tests/src/core/NotePlaybackTestParts1.h`
- `tests/src/core/NotePlaybackTestParts2.h`
- `tests/src/core/ProjectContainerSaveTestParts1.h`
- `tests/src/core/ProjectContainerSaveTestParts2.h`
- `tests/src/core/RackMacrosTestParts1.h`
- `tests/src/core/RackMacrosTestParts2.h`
- `tests/src/core/RecordToolsTestParts1.h`
- `tests/src/core/RecordToolsTestParts2.h`
- `tests/src/core/RevisionTimelineTestParts1.h`
- `tests/src/core/RevisionTimelineTestParts2.h`
- `tests/src/core/RevisionTimelineTestParts3.h`
- `tests/src/core/UpnpMapperFakeRouterTestParts1.h`
- `tests/src/core/VideoTrackTestParts1.h`
- `tests/src/core/VideoTrackTestParts2.h`
- `tests/src/gui/ArrangerTimelineTestParts1.h`
- `tests/src/gui/ArrangerTimelineTestParts2.h`
- `tests/src/gui/ArrangerTimelineTestParts3.h`
- `tests/src/gui/ArrangerTimelineTestParts4.h`

## Verification

Configure uses the full owner command in `/tmp/sol26-configure-command.txt`:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo -DCMAKE_EXE_LINKER_FLAGS:STRING=-Wl,--strip-debug -DUSE_WERROR:BOOL=ON -DWANT_ABLETON_LINK:BOOL=ON -DWANT_ALSA:BOOL=ON -DWANT_CALF:BOOL=ON -DWANT_CAPS:BOOL=ON -DWANT_CARLA:BOOL=ON -DWANT_CMT:BOOL=ON -DWANT_COLLAB:BOOL=ON -DWANT_DEBUG_CPACK:BOOL=ON -DWANT_GIG:BOOL=ON -DWANT_JACK:BOOL=ON -DWANT_LV2:BOOL=ON -DWANT_MP3LAME:BOOL=ON -DWANT_OGGVORBIS:BOOL=ON -DWANT_OSS:BOOL=ON -DWANT_PORTAUDIO:BOOL=ON -DWANT_PULSEAUDIO:BOOL=ON -DWANT_QT6:BOOL=ON -DWANT_RUBBERBAND:BOOL=ON -DWANT_SDL:BOOL=ON -DWANT_SF2:BOOL=ON -DWANT_SID:BOOL=ON -DWANT_SNDIO:BOOL=ON -DWANT_SOUNDIO:BOOL=ON -DWANT_STEM_SPLIT:BOOL=ON -DWANT_STK:BOOL=ON -DWANT_SUIL:BOOL=ON -DWANT_SWH:BOOL=ON -DWANT_TAP:BOOL=ON -DWANT_VST:BOOL=ON -DWANT_VST3_TEST_INSTRUMENT:BOOL=ON -DWANT_VST_32:BOOL=ON -DWANT_VST_64:BOOL=ON -DWANT_WASM:BOOL=ON -DWANT_WEAKJACK:BOOL=ON -DZENE_TELEMETRY:BOOL=ON -DZENE_WANT_FFMPEG:BOOL=ON -DZENE_WANT_LIBAAF:BOOL=ON -DWANT_COVERAGE:BOOL=OFF -DWANT_CPACK_TARBALL:BOOL=OFF -DWANT_DEBUG_ASAN:BOOL=OFF -DWANT_DEBUG_FPE:BOOL=OFF -DWANT_DEBUG_GPROF:BOOL=OFF -DWANT_DEBUG_MSAN:BOOL=OFF -DWANT_DEBUG_RTSAN:BOOL=OFF -DWANT_DEBUG_TSAN:BOOL=OFF -DWANT_DEBUG_UBSAN:BOOL=OFF -DWANT_WINMM:BOOL=OFF -DWANT_CLAP:STRING=ON -DWANT_VST3:STRING=ON -DLMMS_CLAP_FETCH=ON -DONNXRUNTIME_ROOT=/home/kruzzzzy/.local/zene-deps/onnxruntime-linux-x64-1.28.0 -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache '-DCMAKE_CXX_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete' '-DCMAKE_C_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete'
```

- Configure: exit 0, 7.8 seconds configuring and 60.8 seconds generating.
- `bash tests/file-length-gate.sh --check`: originally exit 1 with the hosted
  diagnostics; after all splits exit 0, no baseline written.
- `python3 -m pip install --target build/ci-python lizard==1.25.1`: exit 0.
- `PYTHONPATH="$PWD/build/ci-python" bash tests/complexity-gate.sh --check`:
  exit 0, no baseline written; matches hosted analyzer version.

- Independent review caught misplaced exception helpers and a conditional UPnP
  include in the draft. The first build also rejected the inaccessible helpers.
  Exception helpers now remain in the original anonymous namespace, with only
  process-lifetime shutdown state moved to its new unit.
- The parent audit found a truncated timeline fixture. All 22 fixture shells
  were reconstructed from HEAD, replacing only the moved method definitions with
  their original declarations. This preserves fields, private helpers, conditional
  branches, main/moc wiring and default arguments. The complete implementation
  headers are included before each original test main, inside the original branch.
- `python3 build/reconstruct-test-declarations.py`: exit 0 (temporary verification
  driver; source edits were reviewed afterward). `python3 build/verify-split-bodies.py`:
  exit 0; all 611 original fixture method bodies and method sets match HEAD,
  token for token excluding comments/formatting, with literals preserved.
- New paths are intent-to-add only so the documented git-ls-files derivation can
  see them before the runner commits. tests/all-sources.txt was regenerated by
  its unchanged header recipe into sorted order. No recipe or gate was changed.
- `bash tests/fork-sources-gate.sh`: exit 0 after regeneration, all new files measured.
- `bash tests/file-length-gate.sh --check`: exit 0 again after the fixture corrections.
- `FileLengthGate` is registered in CTest and invokes the unchanged length gate
  with --check, so runner verification cannot silently update the baseline.


## Recovery on promoted base 779829655039 (failure 38)

The prior session ended before submission. Its uncommitted draft was recovered
into the new authorized checkout by a three-way file merge. The promoted base
already contains all pending Windows/Linux/MinGW and NoteCanvas repairs; those
fixes and their reports remain unchanged, with no supersession required.

Additional verification wiring follows moved code without relaxing assertions:
- `tests/complexity-regression-check.py` follows `loadModuleBytes` into its new
  measured unit, retaining its target, CCN limit and red/green scanner fixtures.
- `tests/src/plugins/Vst3HostTest.cpp` reads the new parameter unit for its MIDI
  counter increment/read assertions and still checks initialization in the host.
- `tests/CMakeLists.txt` builds the Patman, NES, TripleOscillator and WASM modules
  required by existing assertions when building the affected test targets.
- The recording test uses a `QTemporaryDir` for recovery-file reservations and a
  scope guard restores the original working directory even when an assertion
  returns early. Existing user takes/journals are neither removed nor modified.

The initial 37-check run on the prior checkout passed 29 and failed 8: three
missing module fixtures, two moved-source checks, recording recovery namespace
exhaustion, the then-pending ruler repair, and collaboration snapshot loading.
All draft compile errors (misplaced exception helpers, two Windows-only method
conditions, and duplicated timeline method fragments) were corrected before the
successful 37-target build. The final timeline source received separate review.

On the current checkout:
- The configured build uses the full owner command above, with one build and
  `cmake --build build --target` all affected targets `-j12`.
- `bash tests/file-length-gate.sh --check`: exit 0, no baseline written.
- `bash tests/fork-sources-gate.sh`: exit 0, unchanged whole-tree recipe reproduces.
- `bash tests/no-upstream-regression-gate.sh`: exit 0.
- `bash tests/evidence-gate.sh`: exit 0, all newly registered files scanned.
- A token audit found 611 fixture methods, preserving method sets and 609 bodies
  byte-for-token apart from comments/formatting; the two intentional body changes
  isolate recovery storage and joined collaboration snapshots. All 588 moved definitions retain their original
  preprocessor branch context. Verification drivers are temporary build artifacts.
- Fresh independent Luna review found no semantic, assertion, registration or
  build-integration blocker by source inspection; build and runtime checks follow.


## Runtime verification and final adjustments

The fresh 38-target build exited 0 (1717 Ninja steps), including Zene, both VST3
modules, the 22 split suites and the additional host/core/portability test targets.
Disk remained above 77 GiB free.

`PYTHONPATH="$PWD/../ci-python" ctest --output-on-failure -R` the anchored
42-name regex, run from `build/tests`, passed 41/42. The only failure was the
unchanged collaboration test. The original train executable, invoked directly
with `QT_QPA_PLATFORM=offscreen ZENE_CONTROL_CHECK_RESULTS=1` and the case name
`throttledUntrackedEditIsRetried`, reproduced tempo 163 vs 140 (exit 1).
The sol-test wrapper itself could not create its train-build LastTest.log.
Temporary diagnostic logging identified refusal to write the joined snapshot
in the user's projects directory. That logging was removed before final testing.

`CollabTransportTest::throttledUntrackedEditIsRetried` now also installs its
existing `QTemporaryDir` as ConfigManager's working directory for the test and
restores the previous setting with a scope guard. This isolates the joined copy
as well as the host/resync fixtures, instead of writing into user projects.
Tempo, snapshot counts, retry timing, and all existing assertions are unchanged.
`tests/src/core/CollabTransportTest.cpp` includes ConfigManager/QScopeGuard for
this isolation; its moved body remains in the measured implementation header.

`cmake --build build --target zene CollabTransportTest -j12`: exit 0 (4 steps).
The full collaboration suite is rerun below. All original QTest slots and their
conditional compilation remain registered; no baseline, exemption, assertion,
release gate or resource limit is weakened.


Final targeted build command (exit 0):

```sh
cmake --build build --target zene ArrangeToolsTest CollabTransportTest ControlAutomationScriptTest ControlDeviceCatalogueTest ControlEditCommandsTest ControlShutdownHookTest CustomAccessTest DataFileSaveIntegrityTest DeviceDspTest DeviceToolsTest DrumSynthTest HardwareIoTest InstrumentRoutingTest LinkAudioStreamTest NotePlaybackTest ProjectContainerSaveTest RackMacrosTest RecordToolsTest RevisionTimelineTest UpnpMapperFakeRouterTest VideoTrackTest ArrangerTimelineTest ControlRegistryTest TakeLaneTest TakeLaneCompTest WasmSandboxTest WasmWorkerPoolTest WasmSandboxHostFuelTest Vst3HostTest Vst3ChunkProbeTest Vst3EffectIntegrationTest Vst3EditorTest Vst3InstrumentTest Vst3InstrumentIntegrationTest vst3effect vst3instrument FollowActionEditorTest -j12
```

Exact selected-test command, from `build/tests`:

```sh
PYTHONPATH="$PWD/../ci-python" ctest --output-on-failure -R '^(ArrangeToolsTest|CollabTransportTest|ControlAutomationScriptTest|ControlDeviceCatalogueTest|ControlEditCommandsTest|ControlShutdownHookTest|CustomAccessTest|DataFileSaveIntegrityTest|DeviceDspTest|DeviceToolsTest|DrumSynthTest|HardwareIoTest|InstrumentRoutingTest|LinkAudioStreamTest|NotePlaybackTest|ProjectContainerSaveTest|RackMacrosTest|RecordToolsTest|RevisionTimelineTest|UpnpMapperFakeRouterTest|VideoTrackTest|ArrangerTimelineTest|ControlRegistryTest|TakeLaneTest|TakeLaneCompTest|WasmSandboxTest|WasmWorkerPoolTest|WasmSandboxHostFuelTest|Vst3HostTest|Vst3ChunkProbeTest|Vst3EffectIntegrationTest|Vst3EditorTest|Vst3InstrumentTest|Vst3InstrumentIntegrationTest|FollowActionEditorTest|FileLengthGate|NoteCanvasComplexityGate|complexity\-regression\-check|Qt5OverloadedSignalCheck|Qt5SignalOverloadCheck|Qt5SignalOverloadArmCheck|ArrangerZoomNoGestures)$'
```

After the single collaboration isolation correction and rebuilt target,
`ctest --output-on-failure -R '^CollabTransportTest$'`: exit 0, 1/1 suites passed
(13.02 seconds). Thus all 42 selected checks have passing final results across
the main run and focused rerun. The failed run and baseline comparison are
retained above, rather than represented as an initially clean result.

`PYTHONPATH="$PWD/../ci-python" ctest --output-on-failure -R
'^(FileLengthGate|NoteCanvasComplexityGate|complexity-regression-check)$'`:
exit 0, 3/3 passed after the final source changes, using hosted Lizard 1.25.1
installed only under `build/ci-python`. The same runner command is checked
without that environment override against the system Lizard 1.24.0 below.

Final registration/provenance/evidence reruns all exited 0; Gate 12 also passes
(794 test sources scanned). `git diff --check`: exit 0. A separate narrow Luna
review confirmed both temporary-directory guards restore safely, and all tempo,
snapshot, timing, arm/disarm and duplicate-id assertions remain intact. The final
body/condition audits retain all 611 methods and 588 original branch contexts;
609 method bodies are unchanged, with only the two storage-isolation changes.
No files in the gate executable, baseline or exemption sets differ from HEAD.
Full train and native hosted CI verification remain required.

Runner-environment gate rerun (system Lizard 1.24.0, no PYTHONPATH override):
exit 0, 3/3 checks passed in 22.83 seconds. Hosted-version Lizard 1.25.1
was verified separately above. All 42 selected checks therefore have passing
final results, with the full collaboration-suite rerun replacing its prior failure.


## Verification retry: LinkAudioStreamTest teardown

The runner's full 42-check verification (repair-check-1794618903933673034.log)
passed every assertion in LinkAudioStreamTest but reported SIGSEGV after the
QtTest summary. The prior passing run had not exposed this shutdown race.

Reproduction from `build/tests`:

```sh
QT_QPA_PLATFORM=offscreen ZENE_CONTROL_CHECK_RESULTS=1 gdb -batch \
  -ex run -ex 'thread apply all bt 12' --args ./LinkAudioStreamTest
```

GDB reproduced the crash on `AudioDummy` after all 22 cases passed. The stack
was `AudioEngineWorkerThread::drainDeferredPlayHandleEdits` →
`startAndWaitForJobs` → `AudioEngine::renderStageInstruments` →
`renderNextPeriod` → `AudioDummy::run`. Worker threads were still active during
process static destruction. Fixture init calls `Engine::init(true)`; its cleanup
had only disabled the Link stream and never destroyed the engine.

Only `tests/src/core/LinkAudioStreamTestParts1.h` changes for the correction:
retain stream disable, mark control not ready, and call `Engine::destroy()` to
stop/join the fixture's render threads before static worker containers disappear.
Cleanup now also asserts successful stream disable and a null engine pointer.
Both assertions run after teardown so an early assertion return cannot leave the
engine alive. No product code, existing test body/assertion, baseline, gate or
resource limit is removed or weakened. The header's existing manifest entries
continue to measure this change.

Failing-then-passing regression:
- Add the null-engine cleanup assertion without teardown; rebuild with
  `cmake --build build --target zene LinkAudioStreamTest -j12` (exit 0).
- From `build/tests`, `ctest --output-on-failure -R '^LinkAudioStreamTest$'`:
  exit 8; cleanup deterministically fails `Engine::audioEngine() == nullptr`
  (21 passed, 1 failed), rather than depending on a crash schedule.
- Add teardown and rebuild using the same command: results recorded below.

An independent Luna read-only audit corroborated the still-running AudioDummy
thread, missing fixture teardown, and `Engine::destroy()` ordering. This is a
fixture lifetime correction; the backtrace does not implicate PCM stream behavior.


Post-fix results:
- `cmake --build build --target zene LinkAudioStreamTest -j12`: exit 0,
  rebuilt the fixture and linked its executable.
- From `build/tests`, `ctest --output-on-failure -R '^LinkAudioStreamTest$'
  --repeat until-fail:10`: exit 0; all ten full-suite processes passed and exited
  cleanly (62.38 seconds total). The original runner failure occurred after the
  QtTest summary; these checks include the process exit, not just assertions.
- Re-ran unchanged fork-source, upstream-regression, evidence and non-writing
  file-length gates: all exit 0. `git diff --check`: exit 0.
- Rechecked all 22 fixtures against pre-repair SHA 779829655039: all 611 methods
  retained, 608 bodies unchanged; only the two storage-isolation fixtures and
  this explicit engine teardown differ. No existing test case is excluded.
- A second independent source review confirmed the teardown order and no
  assertion weakening; its request for explicit post-fix build/results evidence
  is satisfied by these results. The exact runner's 42-check command follows.


Exact runner-command verification, from `build/tests`, using the same anchored
42-test regex printed above:

```sh
ctest -R '^(ArrangeToolsTest|CollabTransportTest|ControlAutomationScriptTest|ControlDeviceCatalogueTest|ControlEditCommandsTest|ControlShutdownHookTest|CustomAccessTest|DataFileSaveIntegrityTest|DeviceDspTest|DeviceToolsTest|DrumSynthTest|HardwareIoTest|InstrumentRoutingTest|LinkAudioStreamTest|NotePlaybackTest|ProjectContainerSaveTest|RackMacrosTest|RecordToolsTest|RevisionTimelineTest|UpnpMapperFakeRouterTest|VideoTrackTest|ArrangerTimelineTest|ControlRegistryTest|TakeLaneTest|TakeLaneCompTest|WasmSandboxTest|WasmWorkerPoolTest|WasmSandboxHostFuelTest|Vst3HostTest|Vst3ChunkProbeTest|Vst3EffectIntegrationTest|Vst3EditorTest|Vst3InstrumentTest|Vst3InstrumentIntegrationTest|FollowActionEditorTest|FileLengthGate|NoteCanvasComplexityGate|complexity\-regression\-check|Qt5OverloadedSignalCheck|Qt5SignalOverloadCheck|Qt5SignalOverloadArmCheck|ArrangerZoomNoGestures)$' -j1 --timeout 900 --no-tests=error --output-on-failure
```

Exit 0: **42/42 passed**, 104.46 seconds, including LinkAudioStreamTest's clean
process exit. This replaces the earlier runner failure; no test was removed from
its regex. Native hosted and full train verification remain the runner's next step.
