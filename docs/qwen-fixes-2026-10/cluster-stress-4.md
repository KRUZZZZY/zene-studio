FIXED — cause (a), read-only parameters offered as settable, is fixed with fixture regressions. Cause (b) was investigated on the farm's plug-ins: the Surge XT rows named in the finding are the plug-in's own parameter semantics, not a host restore bug. The rest of (b) is listed below as open.

# Cluster stress-4 — plug-in state round-trip "loses" parameters; read-only parameters offered as settable

Branch `bugs/qwen-stress-4`, base `5c1c6715d`. Round 1. Round 0 left no commit on this branch, so this round starts from the base.
Logs: `~/.local/share/zene-bunny-fix/qwen/runs/stress-4.r1/logs/` (numbered; cited as `log NN`).

## The finding, separated

The stress run's state check (`tests/plugin_compat_steps.py` `state_roundtrip`) works like this: pick 4 ranged parameters, set each to an end, `plugin.state_save`, move them, `plugin.state_load`, then compare. It failed on 53 VST3/CLAP plug-ins. The failures fall into two groups.

### (a) Read-only parameters were listed and accepted writes — REAL, fixed

DPF's `Buffer Size` / `Sample Rate`, the DPF/ZAM output parameters (`Gain Reduction`, `Output Level`, `Out`, `Control Gain`, `Final Position`) and AIDA-X's `Meter Out` are declared read-only by their plug-ins (VST3 `ParameterInfo::kIsReadOnly`, `CLAP_PARAM_IS_READONLY`). The hosts already read the flag:
- `plugins/Vst3Effect/Vst3Host.cpp:298`
- `plugins/ClapEffect/ClapHostParams.cpp:82`

The CLAP host already drops writes to them (`ClapHostParams.cpp:218`). The control surface never saw the flag, though. `controlParameterJson` (`src/core/ControlDeviceSupport.cpp`) reported such a parameter with a plain range, and `plugin.param_set` accepted the write and reported the new value. The plug-in then overwrote it, so a state round-trip "lost" it.

Fix:
- **The mark** (`include/ParameterReadOnly.h`, new, header-only). Each model carries the flag as a QObject dynamic property:
  - `markParameterReadOnly` is set by `Vst3ParamModel` (`plugins/Vst3Effect/Vst3Parameter.cpp:43`; this file is also compiled into `vst3instrument`).
  - It is set by `ClapParamModel` at construction and on a descriptor rescan (`plugins/ClapEffect/ClapParameter.cpp:63,72`; this file is also compiled into `clapinstrument`).
  - A property was used so that core code, which sees only `AutomatableModel`, can read the flag without a new virtual on the upstream `AutomatableModel`.
- **Reported:** `controlParameterJson` adds `read_only: true` for such a parameter (`src/core/ControlDeviceSupport.cpp:348`).
  - It is written only when true, so every other parameter object is byte-identical to before.
  - It reaches `plugin.param_get`, `plugin.param_set` and `dsp.get_state` (`controlParameterList`).
  - The `plugin.param_*` result schema gains the optional `read_only` boolean (`src/core/ControlCommandsPluginParams.cpp:106`). The replies are schema-checked under `ZENE_CONTROL_CHECK_RESULTS`, which `Vst3InstrumentIntegrationTest` runs with.
- **Refused:** `plugin.param_set` refuses such a parameter with the typed `refused` error and changes nothing. There is no undo step and no transaction (`src/core/ControlCommandsPluginParams.cpp:216,265`).
- **Sibling write path:** `plugin.randomise` and `plugin.blend` choose their models with `isSetting`, which already excluded meter models. It now excludes read-only parameters too (`src/core/ControlCommandsDeviceParams.cpp:281`).
  - The other callers of the parameter list don't write values: `param_rename` sets an alias, `RemoteControls` and `RackMacros` look up names, and W3Engine resolves a builtin's "Level".
- **Harness (accepted only together with the host flag, per the brief):** `ranged_parameters` skips a parameter reported `read_only` (`tests/plugin_compat_steps.py:104`).

### (b) Writable parameters that did not come back — investigated per plug-in

All runs below used this worktree's `build/zene` against the farm's packages (read-only). The scripts are in the run folder's `scratch/`, and `repro3.py` drives the stress script's own `run_subject`.

- **Surge XT FX (CLAP) — the plug-in's semantics.** Under the stress script's own conditions, the four checked parameters were `Modulation Waveform`, `Stages Count`, `Stages Feedback` and `FX Type` (log 09).
  - `FX Type` is set last, and changing it makes Surge switch the effect, which resets and renames the other parameters.
  - The host reported the plug-in's real values after the load.
  - With `FX Type` left out (log 40), 3 of 3 come back.
  - A single parameter set while the effect processes comes back too (log 07).
  - "Did the host re-apply cached values over the restored state?" No. After `state.load`, the CLAP host mirrors the plug-in's values without bumping revisions (`ClapHostInternals.h` `mirrorParamValues`, `ClapHostParams.cpp:279`), and `ClapEffectControls::loadSettings` pulls them into the models with echo suppressed.
  - "Was it normalisation?" No. 0.5 is Surge's value for the newly selected effect's parameter, not a 0/1 step normalised wrongly.
- **Surge XT (CLAP instrument) — the plug-in's semantics.** The lost `FX A1 -` and `FX A2 -` are the unnamed (`-`) parameters of empty (Off) FX slots, which Surge resets when it loads a patch (log 10).
  - The active-slot `FX B2 AD Clip - Mode` comes back (logs 10, 41).
  - The VST3 build of Surge, through a separate host implementation, loses exactly the same two (stress `results.jsonl`).
- **Surge XT Effects (VST3):** 4 of 4 come back (log 42).
- **Surge XT VST3 instrument (log 43): inconclusive.** My scratch exclusion filter (names ending ` -`) removed all four picked parameters. The script then fell back to its "no parameter to move" byte-identity check, which a filter artefact triggered and which says nothing about the stress row. That row's two lost parameters are the same empty-slot `FX A1 -` / `FX A2 -` as the CLAP build (stress `results.jsonl`).
- **Read-only rows after the fix:** they are skipped, and these rows now pass on real plug-ins:
  - AIDA-X VST3 4/4 (log 24)
  - DPF 3 Band EQ VST3 4/4 (log 25)
  - AIDA-X CLAP 4/4 (log 28)
  - ZamComp CLAP 4/4 (log 29)

## Open, not fixed here (with evidence)

1. **DPF `Current Program` (VST3).** ZamComp VST3 still fails on it, `1 -> 0` (log 26).
   - It is DPF's program-change parameter, which is writable. After `setState`, the plug-in's controller reports program 0 and the host restores that value (`Vst3Host.cpp:474-487`).
   - The CLAP build of the same plug-in has no such parameter and passes (log 29).
   - Not shown to be host-side.
2. **DPF `MIDI Ch. n CC m` proxy parameters (Kars, Nekobi; `MIDI CC 0|n` on JUCE builds).** These are the plug-ins' MIDI-CC input parameters (VST3 `IMidiMapping`), not part of their saved state. They are not declared read-only, so they stay in the check (Kars 0/4, log 27).
   - Skipping them would need another flag (hidden / MIDI-mapped), which belongs to a separate change.
3. **Odin2 / Vitalium (VST3 instruments).**
   - A host edit does reach the plug-in: its component state changes after `param_set` (log 14).
   - Right after `component->setState` + `controller->setComponentState` of the edited state, the controller reports the pre-edit value. The component state then equals the pre-edit state again (log 15).
   - Dexed and Surge, also JUCE-built, round-trip through the same host.
   - Not proven host-side. The one host behaviour that could contribute: after a load, the VST3 host's audio thread sends every restored value that differs from the last one it sent (`Vst3Host.cpp:664-674`), whereas the CLAP host suppresses that echo. Changing this needs its own design and test.
4. **New defect found while reproducing (not part of any stress row): a CLAP effect that has not yet heard audio never receives host parameter edits.**
   - Steps: load Surge XT FX (CLAP) on a silent track, `param_set`, wait, `state_save` (or save the project), then load. The edit is gone (logs 04, 06: 0 of 4; single parameter `1 -> 0`).
   - With the transport playing, the same edit round-trips (log 07).
   - Cause: `Effect::processAudioBuffer` returns early while the effect is asleep (`src/core/Effect.cpp:308-330`). `AudioPlugin::processAudioBuffer` is `final` (`include/AudioPlugin.h:246`). The CLAP host delivers parameter events only in `process()` (`ClapHost.cpp:378-398`), and it calls `flush` from the main thread only while inactive (`ClapHostParams.cpp:296`).
   - CLAP requires `flush` on the audio thread while active, so the fix belongs in the core sleep path. That is a design change to every effect's auto-quit, and is left for its own item.
   - The stress rows are not affected: the stress script's `play` step wakes every effect before `state`.

## Regression tests (fail without the fix, pass with it)

- `tests/src/plugins/Vst3InstrumentIntegrationTest.cpp:999` `testReadOnlyParametersAreReportedAndRefusedOverControl`. It uses the in-tree VST3 fixture's `kIsReadOnly` "Meter":
  - `plugin.param_get` reports `read_only`, and the writable "Level" doesn't.
  - `plugin.param_set` on it is `refused` and the value stays.
  - `plugin.randomise` leaves it.
  - "Level" still writes.
- `tests/src/plugins/ClapEffectIntegrationTest.cpp:630` `testReadOnlyParametersAreReportedReadOnly`. It uses the in-tree CLAP fixture with `CLAP_TEST_GAIN_READONLY`: the read-only Bypass is reported `read_only`, and Gain isn't.
- Both use in-tree fixtures only, so they always run. No third-party plug-in is needed.

## Commands and exit codes

All builds used `CCACHE_TEMPDIR=<run>/ccache-tmp`. The first build failed on ccache's temp-dir permission in the jail (log 02, EXIT=2), not on code.

| Step | Command | EXIT |
|---|---|---|
| host modules | `cmake --build build --target clapeffect clapinstrument vst3effect vst3instrument tripleoscillator -j4 > logs/03-build-hosts.log` | 0 |
| fix.patch | `git diff -- plugins/ClapEffect/ClapParameter.cpp plugins/Vst3Effect/Vst3Parameter.cpp src/core/ControlCommandsDeviceParams.cpp src/core/ControlCommandsPluginParams.cpp src/core/ControlDeviceSupport.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| RED build | `cmake --build build --target zene vst3instrument vst3effect clapeffect Vst3InstrumentIntegrationTest ClapEffectIntegrationTest -j4 > logs/16-build-red.log`; CLAP fixture `--target clap-test-gain clap-test-instrument > logs/19-build-clap-fixture.log` | 0, 0 |
| RED test | `ctest -R '^(Vst3InstrumentIntegrationTest\|ClapEffectIntegrationTest)$' --output-on-failure > logs/20-test-red.log` | **8** (2 of 2 failed: `read_only` missing, at the new assertions) |
| GREEN | `git apply logs/fix.patch`; `cmake --build build --target zene vst3instrument vst3effect clapeffect clapinstrument Vst3InstrumentIntegrationTest ClapEffectIntegrationTest -j4 > logs/21-build-green.log` | 0 |
| GREEN test | same ctest `> logs/22-test-green.log` (verbose: log 23 — Clap 16/16, Vst3Instrument 18/18 passed, 0 skipped) | **0** |
| family build | `cmake --build build --target <25 tests + fixtures> -j3 > logs/31-build-family.log` (log 30: a zsh quoting slip, EXIT=2, nothing built) | 0 |
| family test | `ctest -j3 -R '^(ChainPresetMenuTest\|ControlAutomationModesTest\|ControlAutomationScriptTest\|ControlChainPresetTest\|ControlRegistryTest\|DelayControlsTest\|DeviceAbUndoTest\|DeviceCopyTest\|DeviceDspTest\|DevicePanelTest\|DeviceToolsChainTest\|DeviceToolsTest\|InstrumentLayersCommandsTest\|PluginControlDialogsTest\|ReversibilityUndoDeviceTest\|ScriptDawEffectsTest\|SfzPlayerTest\|Vst3HostTest\|Vst3ChunkProbeTest\|Vst3InstrumentTest\|Vst3EffectIntegrationTest\|ClapHostTest\|ClapInstrumentViewTest\|Vst3InstrumentIntegrationTest\|ClapEffectIntegrationTest)$' > logs/32-test-family.log` | 0 — 25/25 passed. Log 33: the named family Vst3HostTest/Vst3ChunkProbeTest/Vst3InstrumentTest and the others run with 0 skipped |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/34-feature-check.log` | 1 — `PermissionError`: the script makes its log dir beside the worktree, outside this lane's write jail |
| gates | the same script copied to `<run>/scratch/fc/`, with only `ROOT` (pinned to the qwen dir) and the logs dir (run folder) changed, `> logs/35-feature-check.log` | **0** — all 8 PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |
| real plug-ins after the fix | `python3 scratch/repro3.py '<subject>' load,render,play,state` (logs 24-29, 40-43) | 0 each |

## Definition of done

- **Realtime:** nothing on the audio thread changed. The property is written on the GUI thread (model construction, `ClapParamModelList::sync`) and read by control handlers on the GUI thread.
- **Undo:** a refused write takes no step and records no transaction. A writable parameter's write is unchanged.
- **Commands:** no command added, and the id set is unchanged. Two descriptions and the `plugin.param_get` / `plugin.param_set` result schemas changed (an optional `read_only` field). The committed MCP snapshot (`tools/mcp-zene-control/zene_control/commands_snapshot.json`) still holds the old schema text; the train regenerates it live. The `ControlCommandsSnapshot` test compares id sets.
- **Saved data:** no save format changed. `Vst3EffectControls`/`Vst3Instrument::saveSettings` already skip read-only parameters in the fallback.
- **Registration:** `include/ParameterReadOnly.h` was added to `tests/fork-sources.txt` in byte order. The other changed sources are already fork-registered.
- **The farm's harness copy:** `~/.cache/zene-plugin-farm/harness/plugin_compat_steps.py` is a separate copy (read-only for this lane). A rerun of the stress run needs it refreshed from the tree to skip `read_only` parameters.

## Self-check

- **The regression fails with the product change reverted and passes with it.** Yes.
  - `git apply -R logs/fix.patch`, rebuild (log 16, EXIT=0, plus the CLAP fixture in log 19, EXIT=0), then `ctest -R '^(Vst3InstrumentIntegrationTest|ClapEffectIntegrationTest)$'` gives **EXIT=8** (log 20). Both new tests fail at the `read_only` assertion.
  - `git apply logs/fix.patch`, rebuild (log 21, EXIT=0), then the same ctest gives **EXIT=0** (log 22; verbose log 23: 16/16 and 18/18 passed, 0 skipped).
  - `fix.patch` holds the five product files. The new header was untracked, so it stayed on disk during RED, but nothing in the reverted tree includes it.
- **Deterministic:** yes.
  - No timing, no sleep, no event loop is spun, and both use in-tree fixtures only.
  - The VST3 test drives `ControlRegistry::invoke` synchronously, on a track made with `std::make_unique<InstrumentTrack>(Engine::getSong())`. Its `plugin.randomise` call passes a fixed `seed: 7`.
  - The CLAP test reads `controlParameterJson` directly. The fixture is made read-only through its own `CLAP_TEST_GAIN_READONLY` switch, as `testReadOnlyParametersGetNoKnob` already does.
- **At the cause and complete.** The flag was already read by both hosts and lost only at the generic model boundary, so it is now carried on the model and read at that boundary: `controlParameterJson`, which serves `plugin.param_get`, `plugin.param_set` and `dsp.get_state`. Callers checked:
  - The models come from `Vst3Parameter.cpp` (shared by `vst3effect` and `vst3instrument`) and `ClapParameter.cpp` (shared by `clapeffect` and `clapinstrument`, including the rescan path `setDescriptor`).
  - The writers of device parameter values on the control surface are `plugin.param_set` (refuses) and `plugin.randomise` / `plugin.blend` through `isSetting` (skip).
  - `plugin.param_rename` (alias only), `RemoteControls`, `RackMacros` and W3Engine's builtin "Level" lookup resolve names and write no value.
  - The GUI already hides read-only parameters (`Vst3EffectControlDialog.cpp:65`, `Vst3InstrumentView.cpp:67`, `ClapKnobGrid.h:62`).
  - The state fallbacks already skip them (`Vst3EffectControls.cpp:146`, `Vst3Instrument.cpp:501`).
  - The harness skip in `tests/plugin_compat_steps.py:104` comes only together with the host flag.
  - Cause (b) is evidenced plug-in by plug-in above. The items not fixed are listed under "Open", each with its evidence.
- **No collateral damage:**
  - Nothing in the save or load path changed, so old projects and state files load byte-for-byte as before.
  - `read_only` is written only when true, so every writable or non-plug-in parameter's JSON object is unchanged.
  - The refusal applies only to models that a VST3 or CLAP host marked from the plug-in's own read-only flag. Builtin, LADSPA, LV2 and other models carry no mark, and `isParameterReadOnly` returns false for them.
  - 25 related ctest entries pass (log 32). The 8 gates pass in log 35, the run-folder copy of `feature_check.py` described in the table.
  - `submit_check.py`'s own gates line fails inside this lane because it runs `feature_check.py`, which makes its log directory beside the worktree. The lane's write jail refuses that (`PermissionError`, log 34). The same script, with only the log directory and `ROOT` moved, passes.
- **ALREADY-FIXED / FALSE-POSITIVE:** not claimed. The verdict is FIXED for (a), and the Surge rows of (b) are traced to the plug-in with log evidence.
