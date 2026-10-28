BLOCKED

Q050-491 (Host Audio Unit (AUv2) plug-ins on macOS): the code is written and committed, and the parts Linux can run
pass. The macOS half has **not been compiled or run**, and most of the acceptance can only be proved there. Two things
are missing, and a lane cannot supply either:

1. **A hosted macOS run.** Criteria 2, 3 (the state round trip), 4 and most of 1 are proved by `AuHostingTest`, which
   runs against Apple's AUBandpass, AUDistortion and DLSMusicDevice. Those cases skip off macOS. This box has no macOS
   toolchain, and lanes are never pushed, so no hosted macos-x86_64 / macos-arm64 job has built `aueffect` or
   `auinstrument` or run those cases.
   - **Compile check done here, syntax only:**
     - the seven plain C++ files of the two modules: GCC, with the real headers and `clapeffect`'s flags;
     - `AuHost.cpp` (Objective-C++): clang, against stub Apple headers written from the SDK signatures;
     - both are clean.
   - The first hosted macOS run of the merged line is the build proof.
2. **The Cocoa view itself (criterion 4).** The hosted jobs run tests under Qt's `offscreen` platform, where a window
   id is not an NSView. The host refuses there by name, and the test asserts that refusal. Opening the unit's view
   needs a real Mac under the `cocoa` platform: the owner's checklist is in `docs/KNOWN-LIMITATIONS.md` (Q050-491).

Branch `feat/050-Q050-491-oc`, cut from `050/m0-train-21` (`37eaf07557`); six commits. Feature record:
`docs/050-features/Q050-491.md`. Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-491.r1/logs/`. Every run below was
on this worktree on Linux (CachyOS, GCC, Qt 6, `QT_QPA_PLATFORM=offscreen`).

## What was built

- **Every build:**
  - `include/AuComponent.h` + `src/core/AuComponent.cpp`: component keys, per-component scan-cache paths, the
    scan-cache record and quarantine rule, and typed errors with OSStatus names;
  - `src/core/ControlDeviceAu.cpp`: the `au` half of `plugin.list` / `plugin.load`. It is a no-op off macOS, where a
    hand-made AU device is refused, typed;
  - `plugin.list` and `plugin.prefer_format` offer `au` only where it is hosted;
  - `plugin.load` of an instrument carries the host's refusal reason.
- **macOS only:**
  - `plugins/AuEffect` (`aueffect`) and `plugins/AuInstrument` (`auinstrument`), on the CLAP hosts' model:
    - registry discovery into the scan cache;
    - `AudioUnitRender` with a render callback;
    - `MusicDeviceMIDIEvent` fed from the VST3 host's lock-free MPSC queue;
    - parameters as `FloatModel`s, which `plugin.param_*` and automation find;
    - ClassInfo state saved as a binary plist;
    - the unit's CocoaUI view, or `AUGenericView`, in the editor window's NSView.
  - The GUI paths: the effect dialog's knobs plus "Show plugin editor", the instrument view's, and the device picker's
    "Audio Unit" rows.

## Acceptance

| # | Criterion | Test - what it proves | Command | Exit |
| --- | --- | --- | --- | --- |
| 1 | `au` reported on macOS builds only | `AuHostingTest::theAuFormatIsOfferedOnlyWhereAudioUnitsAreHosted`: the `plugin.list` format enum, its counts and the organiser formats carry `au` iff `LMMS_BUILD_APPLE`; here `format:"au"` is refused | `ctest -R '^(AuComponentTest\|AuHostingTest)$'` (`test-9-restored.log`) | Linux 0; macOS job: not observed |
| 1 | registry discovery, scan-cache record, the same quarantine rules | Rules: `AuComponentTest` (`aScanRecordsEveryComponentAndTheQuarantineHidesOne`, `theRecordsAndTheQuarantinePersistInTheCacheFile`, `eachComponentHasItsOwnAbsoluteScanCachePath`). The real registry through `plugin.scan_cache_quarantine_add`/`_remove`: `AuHostingTest::theRegistryIsListedRecordedAndQuarantined` | same | Linux 0 (12/12, `test-4-direct.log`); registry case: macOS job, not observed (SKIP here) |
| 2 | AU instrument plays MIDI; AU effect processes audio via the device/chain model | `AuHostingTest::anInstrumentPlaysMidi`, `...anEffectProcessesAudioAndItsParametersAreReachable` | same | macOS job: not observed (SKIP here, `test-3-direct.log`) |
| 2 | parameters via `plugin.param_get/param_set` and automation | the effect case: `param_set` / `param_get`, the value read back from the unit, `automation.add_point` creates the clip | same | macOS job: not observed |
| 3 | `plugin.state_save/state_load` and project save/reopen round-trip ClassInfo | `AuHostingTest::theStateRoundTripsThroughAFileAndAProject` | same | macOS job: not observed |
| 3 | a component that fails to instantiate is refused with a typed error | `AuHostingTest::aComponentThatDoesNotInstantiateIsRefusedTyped` (`Refused`, the reason by name); `AuComponentTest::theRefusalNamesTheComponentTheStepAndTheStatus`, `aFailedStatusIsNamed` | same | Linux 0 (the "built on macOS only" branch and the message rules); the macOS branch ("...registry has no such component"): not observed |
| 4 | editor opens in an NSView | `AuHostingTest::theEditorButtonOpensTheCocoaViewOrNamesWhyNot`: clicks "Show plugin editor" and runs `plugin.editor_open`. Under `cocoa` both open; under `offscreen` both are refused with "...has no NSView (window API cocoa)" | same | macOS job: not observed; a real Mac for the view itself |
| 5 | hosted macOS tests against AUBandpass / AUDistortion / DLSMusicDevice; non-Apple builds compile the format out; real-machine checks listed | `AuHostingTest` is in `LMMS_TESTS` with `add_dependencies` on the AU modules where they exist. Linux builds neither module (both `CMakeLists.txt` return before `BUILD_PLUGIN`), and `plugin.list` has no `au` (row 1). The checks are in `docs/KNOWN-LIMITATIONS.md` | `cmake --build build --target zene AuComponentTest AuHostingTest` (`build-9-restored.log`) | Linux 0; hosted run: not observed |
| GUI | device picker lists AU rows | `AuHostingTest::theDevicePickerListsAudioUnits`: types in the search field; the row's format reads "Audio Unit" | same | macOS job: not observed |

Neighbours run after the change (`test-10-neighbours.log`, `test-11-catalogue.log`, `test-12.log`), all exit 0:
`ControlDeviceCatalogueTest`, `ControlDeviceVendorTest`, `ControlHostedBusLayoutTest`, `MissingDevicesPreserveTest`,
`MissingDevicesRound2Test`, `ReversibilityContractTest`, `DeviceOrganiserTest`, `EffectPickerOrganiserTest`,
`PluginNativeEditorTest`. `ControlDeviceCatalogueTest` first failed in its Patman case only because the unrelated `patman`
module was not built; it passed once `patman` was built (the same note as Q050-490).

## Self-check

- **Each new test FAILS with the product change reverted and PASSES restored.**
  - **All non-test hunks reversed** (`git diff 37eaf07557 HEAD -- . ':(exclude)tests' | git apply -R`, rebuild):
    - `cmake --build build --target AuComponentTest`: exit 2, "AuComponent.h: No such file" (`build-6-reverted.log`);
    - `--target AuHostingTest`: exit 2, `controlAuHostingBuilt` / `ControlDeviceEntry::component` undeclared
      (`build-7-reverted.log`);
    - the ctest run in the same step used the stale binaries from before, so I do not count it.
  - **Behaviour only, declarations kept** (the `au` dispatch removed from `controlDeviceModule`, the quarantine check in
    `recordScan` disabled):
    - `./AuComponentTest`: exit 2, two quarantine cases FAIL (`test-7-mutated-component.log`);
    - `./AuHostingTest`: exit 1, the refusal case got `InvalidArgs` and not `Refused` (`test-8-mutated-hosting.log`).
  - **Restored (patch re-applied, rebuilt):** `ctest -R '^(AuComponentTest|AuHostingTest)$'` exit 0
    (`test-9-restored.log`; again in `test-12.log` after `376b214f35`, the last commit that changes code Linux builds - `8999fb0ff9` touches only the macOS-only `AuHost.cpp`, re-checked with the clang syntax pass).
  - **Limit:** the macOS-only cases have never run, so their revert proof does not exist yet.
- **Every acceptance criterion proven by a test I ran.** **No.**
  - **Ran and passed (Linux):** criterion 1's off-macOS half and its scan-cache and quarantine rules; criterion 3's
    typed refusal (off-macOS branch, and the message rules); criterion 5's compile-out.
  - **Written but never run:** criterion 2, criterion 3's state round trip, criterion 4, and the macOS halves of 1
    and 3. Hence BLOCKED.
- **One undo step per edit; honest reversibility.**
  - No command was added. AU devices go through the existing ones with their existing classes:
    - `plugin.load` of an effect: one structural undo step;
    - an instrument replacement: snapshot, `reversible:false`, as before;
    - `plugin.param_set`: a journal checkpoint;
    - `plugin.state_load`: its existing class;
    - `plugin.list`: not mutating.
  - `ReversibilityContractTest` exit 0 (`test-10-neighbours.log`). The A16 histogram is unchanged (no new id).
- **Realtime.** Audio-path functions added:
  - `AuEffect::processImpl` and `AuInstrument::processImpl`: no allocation; on failure they fill/copy into the given
    buffers.
  - `AuInstrument::handleMidiEvent` (audio or MIDI thread) and `HostedUnit::queueMidi`: one bounded push into the
    preallocated MPSC ring (`Vst3MidiQueue.h`).
  - `HostedUnit::setParam`: two atomic stores. It is also what a model's `dataChanged` calls when automation moves a
    parameter on the audio thread.
  - `HostedUnit::process`, which calls `Impl::applyPendingParameters` (atomic exchange, then `AudioUnitSetParameter`),
    `Impl::deliverMidi` (ring pop, then `MusicDeviceMIDIEvent`), `Impl::pointOutputs` (pointer writes into the
    `AudioBufferList` built in `prepare()`), `AudioUnitRender`, and `memcpy` only when the unit answered with its own
    buffers.
  - `Impl::renderInput`: `memcpy` from pointers set by the same thread.
  - Allocations happen in `load()` / `prepare()` on the GUI thread: the parameter atomics, the `AudioBufferList`, the
    scratch blocks. `prepare` and `loadState` run with the engine held (`requestChangeInModel`, a recursive mutex) or
    before the device is in a chain.
  - No Qt signal is emitted on the audio path. The only non-host calls are Apple's three render-path APIs, whose
    insides belong to the unit.
  - TSan cannot run here. The reasoning is in the feature record's Realtime section. One race I found was fixed:
    commit `8999fb0ff9` (no MIDI-queue reset on re-prepare).
- **Old projects; new data round-trips.**
  - No existing project element changed. The `MissingDevices::formatOf` table maps every old module name as before:
    `MissingDevicesPreserveTest` and `MissingDevicesRound2Test` exit 0.
  - An AU device's data (the key's `component` and `name`; `<state>`; `<parameters>`) round-trips through
    `theStateRoundTripsThroughAFileAndAProject`. That test has not run (macOS job).
- **Portability (Qt 5.15 / MSVC / macOS < 10.15).**
  - Core code:
    - no Qt-6-only API (`QString::fromUtf8(ptr, int)`, `QMap`/`QHash` initializer lists, `QJsonArray::contains`
      and `QTest::keyClicks` all exist in 5.15);
    - no `min`/`max`/`small`/`near`/`far`/`interface` identifiers;
    - no `std::filesystem` and no POSIX-only call.
  - The AU modules are built on macOS only. They use AudioToolbox/AudioUnit/CoreAudioKit APIs available since 10.6–10.7,
    and `QString::fromCFString` (Qt ≥ 5.2).
  - `AuHost.cpp` is compiled as Objective-C++ through a per-source `COMPILE_OPTIONS -xobjective-c++`. It keeps the
    `.cpp` name so the manifests and gates see it.
  - Not compiled on any of those toolchains here.
- **Gates.**
  - `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` cannot run from this lane. It creates its
    log directory beside the worktree (`../zene-feat-q050-491-feature-check`), and the write-jail refuses that:
    `PermissionError: [Errno 13]`.
  - I ran a copy whose only changes are that log directory (`/tmp/auchk/fc-logs`) and the `ROOT` path. Diff: two lines.
  - Result: `RESULT: PASS`. All six simple gates exit 0. Complexity and file-length: no new item in my files.
  - On the first run it reported Gate 6 (the two new `logo.png` files, now registered like
    `plugins/Vst3Instrument/logo.png`) and three complexity items. Commit `376b214f35` fixed all of them.
- **Completion check WARN "canary figures (README) updated - new command ids: au.mmp, automation.add_point, ...".** These
  are file names and existing command ids in test strings, not new commands. The live snapshot keeps 871 ids / 76 groups,
  and `python3 tests/doc-figures-gate.py` exit 0.
- **Not done:**
  - `tests/fork-sources.txt` entries were inserted by hand in sorted place: its header records that its recipe does not
    parse. `tests/all-sources.txt` was re-derived with its recipe, and `bash tests/fork-sources-gate.sh` exit 0.
  - `cmake build` (an explicit regenerate) was refused by the lane guard. The build regenerated itself.
