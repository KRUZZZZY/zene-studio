IMPLEMENTED

# Q050-267.1 - Zene Rack plug-in module and state bridge, stage 1/3

This stage adds `zenerack`, an installable VST3 effect bundle (`ZeneRack.vst3`) that an external VST3 host
discovers, loads and runs through one documented chain. It is not one of Zene's `BUILD_PLUGIN` modules and does not
link the `zene` executable: another DAW's VST3 host loads it directly.

The documented chain, in processing order:

1. **Amplifier** - linear gain.
2. **BassBooster** - the `FastBassBoost` recurrence (frequency 100 x sampleRate / 44100, ratio 2.0).
3. **WaveShaper** - input drive, then hard clip at -1..+1.

The module reports that chain through the ordered parameter names (`1 Amplifier Gain`, `2 BassBooster Gain`,
`3 WaveShaper Drive`), a read-only `Chain` parameter, an `IUnitInfo` program list with one program, and the
component state text. Product code: `plugins/ZeneRack/ZeneRack.cpp`. Build/install:
`plugins/ZeneRack/CMakeLists.txt` (wired from `plugins/CMakeLists.txt`; installs under `<libdir>/vst3/ZeneRack.vst3`).

## Acceptance

| Criterion | Test and command | What it proves | Exit |
| --- | --- | --- | --- |
| An external VST3 host discovers and loads the Rack module and processes audio through the documented chain | `ctest -R ZeneRackTest` (`testExternalHostDiscoversTheRack`, `testProcessesTheDocumentedChainInThatOrder`) | The test opens the built `ZeneRack.vst3` through the VST3 SDK's own hosting loader (`VST3::Hosting::Module::create`, not Zene's `Vst3Host`), finds the `Zene Studio` factory, the `Zene Rack` `Audio Module Class` in the `Fx` subcategory, creates and initializes an instance, prepares stereo at 48 kHz, and processes 256 frames. The render matches an independent model of Amplifier -> BassBooster -> WaveShaper within 1e-6 per sample and differs from the same stages in reverse order by more than 0.1 | **0** |
| The module reports or displays the supported chain and preserves its processing order | `ctest -R ZeneRackTest` (`testModuleReportsTheSupportedChainInOrder`, `testProcessesTheDocumentedChainInThatOrder`) | Parameter titles come back in the documented order; the read-only `Chain` parameter's string and the one program-list name are both `Amplifier > BassBooster > WaveShaper`; the component state contains the same string. The render proves order, not just labels: the reverse-order model does not match | **0** |

Supporting evidence for "installable":

```
cmake -P build/plugins/ZeneRack/cmake_install.cmake   # DESTDIR into the run log dir
EXIT=0
usr/local/lib/vst3/ZeneRack.vst3/Contents/x86_64-linux/ZeneRack.so
```

The bundle exports `ModuleEntry`/`ModuleExit`/`GetPluginFactory` (checked with `nm -D`), and the VST3 SDK loader
opens it in the test above.

## Negative control (definition of done 1)

The product change reversed here is the stage processing: `processChannel` was temporarily replaced by a
pass-through stub, rebuilding the bundle and the test:

```
cmake --build build --target ZeneRackTest zenerack -- -j4   # BUILD_EXIT=0
ctest -R ZeneRackTest --output-on-failure                   # TEST_EXIT=8
FAIL!  : ZeneRackTest::testProcessesTheDocumentedChainInThatOrder()
         'largestForwardError <= 1.0e-6' returned FALSE. (documented order mismatch: 0.827513)
```

The stub was then replaced by the committed source, rebuilt, and rerun:

```
cmake --build build --target ZeneRackTest zenerack -- -j4   # BUILD_EXIT=0
ctest -R ZeneRackTest --output-on-failure                   # TEST_EXIT=0 (1/1 Passed)
```

A full revert of the whole product diff removes the `zenerack` target, so the build fails instead (the test target
depends on it); the pass-through run above is the behavioural control that the test is not tautological.

## Other commands run

| Command | Exit |
| --- | --- |
| `python3 .../feature_check.py <worktree>` (all gates) | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/fork-sources-gate.sh` | 0 |
| `ctest -R ProjectOpenIntegrityTest --output-on-failure` (old-project load) | 0 |
| `cmake -P build/plugins/ZeneRack/cmake_install.cmake` (install layout) | 0 |

## Self-check

**1. Each new test FAILS with the product change reverted.** `ZeneRackTest` is the only new test. With the stage
processing replaced by a pass-through, `ctest -R ZeneRackTest` exited **8** and
`testProcessesTheDocumentedChainInThatOrder` failed on the forward render (`largestForwardError` 0.827513). With the
committed source restored, the same command exited **0**. The discovery and chain-report cases only pass when the
bundle and its parameter/program/state surfaces exist.

**2. Every acceptance criterion is proven by a test I ran.** Criterion 1: `ctest -R ZeneRackTest`, exit **0**
(`testExternalHostDiscoversTheRack` + `testProcessesTheDocumentedChainInThatOrder`). Criterion 2: the same test,
exit **0** (`testModuleReportsTheSupportedChainInOrder` + the order assertion in the render test).

**3. Each edit is ONE undo step, and every new or changed command has an honest reversibility class.** This stage
adds no `ControlRegistry` command: it is a plug-in loaded by another DAW, and its controls are the VST3 parameter
surface. Therefore there is no new undo class, no snapshot change and no A16 histogram line. The plug-in does not
write project data.

**4. Realtime.** The only audio-thread function added is `process()` and its helpers `processAllBuses`,
`processBus`, `processChannel`. They read three `std::atomic<float>` shadows with relaxed loads (the class
static-asserts `std::atomic<float>::is_always_lock_free`), read two `double` filter states owned by the instance and
reset in `setActive(true)`, walk the host's buffers, and write every output frame. The parameter queues use a fixed
`switch` and store atomics; `process()` never calls `Parameter::setNormalized`. There is no allocation, free, lock,
wait, I/O or Qt signal. The host-facing parameter objects are updated only from `setParamNormalized` (off the audio
thread). Local TSan does not run on this box; the cross-thread values are the atomics above and the DSP state is
owned by the plug-in instance.

**5. Old projects still load; new data round-trips.** No project serializer, model, engine or render path changed.
`ctest -R ProjectOpenIntegrityTest` exited **0**, and `feature_check.py` (which includes the no-upstream-regression
gate) exited **0**. The new data is the plug-in's own VST3 component state text, written by `getState()` and stored
by the host, not inside a `.mmp`/`.mmpz`. This stage only reports that state; restoring a fresh instance from host
state is stage 2 (Q050-267.2), so this stage does not claim that criterion.

**6. Completeness: real user path and agent path.** Real path: another DAW's VST3 scanner loads `ZeneRack.vst3`,
the host shows the ordered parameters and the chain program, and its audio graph receives the processed output;
the test reproduces that with the SDK's external loader rather than Zene's host. Agent path: none in this stage,
because no `ControlRegistry` command is added; the stage-3 bridge owns `rack.*` commands, and until then an agent has
nothing new to call. Edge cases exercised: a fresh instance from the class id, reversed chain order (must not match),
and the state report. Not exercised in this stage (documented limits): non-stereo layouts, 64-bit samples, host-state
restore, and a rack editor.

**7. GUI test drives the visible control.** Not applicable: this stage adds no Zene GUI control, menu or command. The
visible controls are the VST3 parameters and the program list, which the *host* draws; the test queries those same
host-facing surfaces (`getParameterInfo`, `getParamStringByValue`, `IUnitInfo::getProgramName`, `getState`) instead
of a Zene widget. There is no registered command for the GUI to bypass.

**8. Qt 5.15 / MSVC / macOS < 10.15 portability.** No Qt API is used in the product code at all (it is plain C++20
plus the MIT VST3 SDK), so there is no Qt-6-only call to guard. No `small`, `near`, `far`, `interface`, `min` or
`max` identifier is declared; no `std::filesystem`; no POSIX-only call outside the SDK/CMake platform branches. The
module lives in `namespace lmms::vst3` (check-namespace gate exit 0). The bundle layout and per-platform entry point
follow the existing `tests/data/vst3-chunk-probe` and `tests/data/vst3-test-effect` contracts (Linux
`Contents/<machine>-linux`, macOS `Contents/MacOS` + Info.plist + `macmain.cpp`, Windows
`Contents/<arch>-win` + `dllmain.cpp`), and CMake detects the target architecture for the Windows directory. CI
compile coverage for Qt5/MSVC/macOS is the hosted jobs; this box only builds Qt 6 Linux, which passed.

## Files

- `plugins/ZeneRack/ZeneRack.cpp`, `plugins/ZeneRack/CMakeLists.txt`, `plugins/ZeneRack/Info.plist.in`
- `plugins/CMakeLists.txt` (configures the bundle when the pinned VST3 SDK target exists)
- `tests/src/plugins/ZeneRackTest.cpp`, `tests/CMakeLists.txt`
- `tests/fork-sources.txt`, `tests/all-sources.txt`
- `docs/050-features/Q050-267.1.md`
- `data/manual/index.html`
