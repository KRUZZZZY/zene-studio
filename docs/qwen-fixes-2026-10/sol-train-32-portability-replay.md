# Train 32 portability repair consolidation

Base cd3f0853ab. Replay of a1deda8dc9ad failed at appended CMake and divergence-ledger entries introduced by the seven accepted candidates. All repair registration blocks now use stable pre-existing context instead of candidate-specific EOF entries. The replacement retains candidate registrations and earcon changes and reconciles all seven pending repairs, including their six separate compiler checks and original reports.

The shipping WASM pool uses the existing native worker wake (one waiter for work, all on shutdown), and every lane rotates through the active registry prefix; tests use predicate-based portable synchronization. Warp tests share TestWorkerThread. NAM timing uses one platform-selected implementation, with a try interface retaining the harness throwing failure policy and the accepted zero-on-error measurement interface. The original high-bit, unit conversion and failure regression now links the actual implementation. PCRE2 patch hashes, literal content, deployment floors, dependency pins, all assertions and gate baselines are preserved.

The unchanged Lizard 1.25.1 gate also reproduced CCN 12 in the new SDK recover method. Its initial connection selection is extracted without changing reconnect logic; see sol-train-32-sdk-complexity.md.

Verification commands and results are recorded below. Historical imported cluster reports describe their original CI evidence, not additional native verification in this checkout.

## Changed paths and registration

- `tests/CMakeLists.txt` and the fork/whole-tree manifests retain every accepted check and both sides of the candidate append conflict; the whole-tree list reproduces from its unchanged recipe. `tests/upstream-modifications.txt` records inherited-source reasons.
- `include/AudioWorkerWake.h`, `src/core/AudioWorkerWake.cpp`, `src/core/AudioEngine.cpp`, and `src/wasm/WasmWorkerPool.{h,cpp}` preserve the accepted macOS deployment-floor correction and one/all wake semantics.
- The affected `tests/src/` files retain their assertions and use `PortableSync.h` or `TestWorkerThread.h`; imports construct the identical QStringList with append calls. Unused alternate thread/rendezvous helpers were removed.
- `plugins/NeuralAmp/tests/` selects and links the OS clock source for both the harness and its including WAV test. `NamHarnessCpuClock.h` preserves the throwing harness interface; both it and the accepted zero-on-error probe API share the same implementation.
- `.github/vcpkg/ports/pcre2/portfile.cmake`, `tests/vcpkg-pcre2-gitlink-test.py`, and `tests/train-43-portability-check.py` preserve the original dependency and compiler contracts, adapting the clock fixture to link its real implementation.
- All six `cluster-ci-*.md` reports and six Python compiler checkers are retained. `tools/zene-session-sdk/src/live-session.ts` receives only the complexity extraction described in the companion report. No gate executable, threshold, exemption, baseline, platform floor or dependency pin changes.

## Commands

Configured the single private build with the owner options (including final WANT_WASM=ON):

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo -DCMAKE_EXE_LINKER_FLAGS:STRING=-Wl,--strip-debug -DUSE_WERROR:BOOL=ON -DWANT_ABLETON_LINK:BOOL=ON -DWANT_ALSA:BOOL=ON -DWANT_CALF:BOOL=ON -DWANT_CAPS:BOOL=ON -DWANT_CARLA:BOOL=ON -DWANT_CMT:BOOL=ON -DWANT_COLLAB:BOOL=ON -DWANT_DEBUG_CPACK:BOOL=ON -DWANT_GIG:BOOL=ON -DWANT_JACK:BOOL=ON -DWANT_LV2:BOOL=ON -DWANT_MP3LAME:BOOL=ON -DWANT_OGGVORBIS:BOOL=ON -DWANT_OSS:BOOL=ON -DWANT_PORTAUDIO:BOOL=ON -DWANT_PULSEAUDIO:BOOL=ON -DWANT_QT6:BOOL=ON -DWANT_RUBBERBAND:BOOL=ON -DWANT_SDL:BOOL=ON -DWANT_SF2:BOOL=ON -DWANT_SID:BOOL=ON -DWANT_SNDIO:BOOL=ON -DWANT_SOUNDIO:BOOL=ON -DWANT_STEM_SPLIT:BOOL=ON -DWANT_STK:BOOL=ON -DWANT_SUIL:BOOL=ON -DWANT_SWH:BOOL=ON -DWANT_TAP:BOOL=ON -DWANT_VST:BOOL=ON -DWANT_VST3_TEST_INSTRUMENT:BOOL=ON -DWANT_VST_32:BOOL=ON -DWANT_VST_64:BOOL=ON -DWANT_WASM:BOOL=ON -DWANT_WEAKJACK:BOOL=ON -DZENE_TELEMETRY:BOOL=ON -DZENE_WANT_FFMPEG:BOOL=ON -DZENE_WANT_LIBAAF:BOOL=ON -DWANT_COVERAGE:BOOL=OFF -DWANT_CPACK_TARBALL:BOOL=OFF -DWANT_DEBUG_ASAN:BOOL=OFF -DWANT_DEBUG_FPE:BOOL=OFF -DWANT_DEBUG_GPROF:BOOL=OFF -DWANT_DEBUG_MSAN:BOOL=OFF -DWANT_DEBUG_RTSAN:BOOL=OFF -DWANT_DEBUG_TSAN:BOOL=OFF -DWANT_DEBUG_UBSAN:BOOL=OFF -DWANT_WINMM:BOOL=OFF -DWANT_CLAP:STRING=ON -DWANT_VST3:STRING=ON -DLMMS_CLAP_FETCH=ON -DONNXRUNTIME_ROOT=/home/kruzzzzy/.local/zene-deps/onnxruntime-linux-x64-1.28.0 -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache '-DCMAKE_CXX_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete' '-DCMAKE_C_FLAGS=-isystem /usr/include/cairo -Wno-error=deprecated-declarations -Wno-error=unused-result -Wno-error=sfinae-incomplete'
```

Targeted build:

```sh
cmake --build build --target zene AudioWorkerWakeTest ModulationAudioViewTest NotePlaybackTest PluginPortsMigrationTest RemotePluginShmFifoTest ScriptDawModelRegistrationTest TempoMapTest WasmWorkerPoolTest WarpMarkersTest SampleClipWarpEditTest ImportTrackIoTest NeuralAmpWavInputTest NeuralAmpHarnessBlockCount AudioPluginTest nam_harness decimator_1202 -j12
```

SDK dependency setup: `npm ci --ignore-scripts --no-audit --no-fund --cache /tmp/sol45-npm-cache` from `tools/zene-session-sdk` exited 0. The first attempt used a pre-existing unwritable shared npm cache and exited 243; a private cache resolved it without changing the lockfile.

CTest (always from `build/tests`, no zero-match success):

```sh
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-sol-repairs/m0-train-32-1794711427470902978/build/tests
PATH=/tmp/sol45-lizard125/bin:$PATH PYTHONPATH=/tmp/sol45-lizard125 ctest -R '^(AudioWorkerWakeTest|ModulationAudioViewTest|NotePlaybackTest|PluginPortsMigrationTest|RemotePluginShmFifoTest|ScriptDawModelRegistrationTest|TempoMapTest|WasmWorkerPoolTest|WarpMarkersTest|SampleClipWarpEditTest|ImportTrackIoTest|NeuralAmpWavInputTest|NeuralAmpHarnessBlockCount|AudioPluginTest|NeuralAmpCompareRateTest|NeuralAmpWavRunSampleRateWarning|NeuralAmpHarnessRateValidation|NamLoaderTest_.*|Qt5QStringListAssignmentCheck|Qt5QStringListBraceCheck|Qt5QStringListAssignCheck|Qt5WorkerThreadCheck|MacosSyncAvailabilityCheck|NamHarnessWindowsClockCheck|Train43Portability|VcpkgPcre2Gitlink|ZeneSessionSdkTypecheck|ControlSessionSdk|FileLengthGate|NoteCanvasComplexityGate)$' -j1 --timeout 900 --no-tests=error --output-on-failure
```

Unchanged gates, run from the source root:

```sh
bash tests/no-upstream-regression-gate.sh
bash tests/fork-sources-gate.sh
bash tests/evidence-gate.sh
bash tests/unregistered-tests-gate.sh
PATH=/tmp/sol45-lizard125/bin:$PATH PYTHONPATH=/tmp/sol45-lizard125 bash tests/complexity-gate.sh --check
git diff HEAD --check
git diff --cached --check
```

The four registration/evidence gates and complexity check exited 0. Scope regeneration retains both new helpers, with zero stale entries; no exemptions were added. Lizard was installed only under `/tmp/sol45-lizard125` at pinned version 1.25.1. Initial complexity check failed on the candidate SDK recover method at CCN 12; extraction reduces it to 9 (helper 3), and the exact unchanged check passes.

Clock controls: `python3 /tmp/sol45-clock-controls.py` exited 0 after isolated temporary copies with a wrong tick divisor, missing high bits and an ignored API failure each failed the retained compiler/runtime fixture; the real Windows-model and native POSIX implementations then passed. No production file was mutated for these controls. The accepted compiler checks also retain their red/green model controls.

An early compiler-only CTest run preceded target autogen completion: seven checks passed, while MacosSyncAvailabilityCheck correctly refused translation units whose moc outputs had not yet been generated. Final verification runs after rebuilding. A misplaced root-directory SDK CTest invocation matched no tests and failed; the correctly located run matched and passed the strict typecheck plus package build.

Independent Luna reviews found no blocker in preservation, native wake behavior, portable synchronization, clock interfaces or the SDK extraction. Native hosted Windows/macOS links and the full train remain required.

## Runtime correction found during verification

The first full 37-entry run passed 35 entries but found two verification blockers: PluginPortsMigrationTest lacked its SWH Decimator fixture, and WasmWorkerPoolTest stalled in the first multiworker case. The stalled process was terminated for diagnosis rather than allowed to consume its entire 900-second timeout. No limit or assertion changed.

The native wake-one post may wake any parked lane. `drainOnce` previously rotated `(lane + i) % maxWorkers` for only `count` iterations, so with one worker only lane zero could reach slot zero; another woken lane spun while lane zero remained asleep. Rotating modulo the loaded active-prefix count lets every lane reach every registered slot, preserving reader pins, holes, bounds and wake-one behavior. A zero count skips the loop before any modulo operation.

The committed `testEveryLaneCanReachTheOnlyRegisteredWorker` stops a local pool's background threads and drives each possible lane offset against one real module registered at slot zero. The pointer is unpublished before assertions. Exact red/green commands:

```sh
cmake --build build --target zene WasmWorkerPoolTest -j12
./build/tests/WasmWorkerPoolTest testEveryLaneCanReachTheOnlyRegisteredWorker -v1
```

With the new regression and original scan, the test exited 1: lane 1 missed the only worker (2 passes, 1 failure). After the one-line scan correction and rebuild, it exited 0 (3 passes). The complete WasmWorkerPoolTest then passed, and this repeated command from build/tests passed all three runs (14.05 seconds total):

```sh
ctest -R '^WasmWorkerPoolTest$' --repeat until-fail:3 -j1 --timeout 900 --no-tests=error --output-on-failure
```

`cmake --build build --target zene decimator_1202 -j12` built the required SWH fixture; PluginPortsMigrationTest then passed with every original assertion intact. Its two existing SF2-dependent cases remain skipped because Sf2Player is unavailable in this local configuration; no skip policy was changed. The final full targeted command above also rebuilt all dependent test binaries after the pool correction. Free disk stayed above 118 GB throughout.

A further independent Luna review found no blocker in the active-prefix correction, zero-count handling, reader lifetime or deterministic regression cleanup.

## Final result

The complete targeted build exited 0, including `decimator_1202` and all dependent binaries. The exact combined CTest command above exited 0: **37/37 entries passed in 153.48 seconds**, including both unchanged metric gates, all six accepted CI compiler checks, the retained Train43/PCRE2 checks, the WASM runtime suite and both SDK checks. The three additional complete WASM runs also passed. All four registration/evidence gates and full whitespace checks passed. No commits, pushes, queue edits, baseline writes or gate changes were made; the runner should supersede all seven pending repair IDs with this combined delta.
