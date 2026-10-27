FIXED — (a) read-only parameters are reported and refused (round 1). (b) After a successful plug-in state load, the host now restores each writable parameter's saved value that the plug-in did not bring back, on VST3 and CLAP (round 2). Every VST3 case the stress run lost now round-trips. The two Surge XT CLAP rows still differ from the check's expectation; the host's own saved file shows why, and that part is the plug-in's doing (see "Surge XT CLAP" below).

# Cluster stress-4 — plug-in state round-trip "loses" parameters; read-only parameters offered as settable

Branch `bugs/qwen-stress-4`. Round 1: `c43eee968`, `25ba5ce22` (+ the workflow's `4bd87be29`). Round 2 is on top.
Logs: round 1 `~/.local/share/zene-bunny-fix/qwen/runs/stress-4.r1/logs/` (cited `r1 log NN`); round 2 `.../stress-4.r2/logs/` (cited `log NN`).

## Round 2 — what the review sent back, and what changed

The review's blocking point: the writable round trip still failed. CLAP saved no host values at all. VST3 applied its saved `<parameters>` only when the chunks failed or were absent (`Vst3Instrument.cpp:522-530` before this round). So a successful chunk load that did not carry a parameter lost it.

### (b) Fix 1 — the host's saved value comes back after a successful load (VST3 and CLAP)

**VST3** — `restoreSavedParameters` (`plugins/Vst3Effect/Vst3Parameter.cpp:60`, declared at `Vst3Parameter.h:85`; the file is compiled into both `vst3effect` and `vst3instrument`). It replaces the two identical `applyParameterFallback` copies:
- It is called after every load, successful or not (`Vst3EffectControls.cpp:152`, `Vst3Instrument.cpp:506`).
- It writes only a parameter that is writable and whose restored value differs from the saved one by more than 1e-6. A plug-in whose chunk agrees with the host sees no write at all.
- With missing or failed chunks it writes every differing value: the same result as the old fallback (`testParameterFallbackWithoutStateChunks` still passes, in both test files).

**CLAP** — `ClapParamModelList::saveValues` / `restoreValues` (`plugins/ClapEffect/ClapParameter.cpp:162,177`; the file is compiled into both `clapeffect` and `clapinstrument`):
- The effect (`ClapEffectControls.cpp:88,100`) and the instrument (`ClapInstrument.cpp:370,379`) now also save `<parameters>`: each writable parameter's host value, as 17 significant digits.
- After the plug-in's `state.load`, a saved value that differs from the plug-in's is put into the host snapshot (`setParamPlain`, delivered on the next block). The model then shows it.
- A document written before this change has no `<parameters>`, so it loads exactly as before.

### (b) Fix 2 — a CLAP rescan read-back no longer overwrites a host value the plug-in has not received

Reproduced on Surge XT Effects (CLAP) on a silent track, before this fix (log 35):
- The host restored the saved 1 ("after load: 1").
- 1.5 s later it was 0. Surge asks for a value rescan after it loads, and `mirrorParamValues` read Surge's old value back over the host's restored one, which had not been delivered yet. The effect had not processed, so Surge had never been sent it.

The fix:
- Each `ParamSlot` gets an atomic `delivered` revision (`plugins/ClapEffect/ClapHostInternals.h`, the `ParamSlot` struct). The audio thread publishes it when it hands the plug-in a change (`ClapHost.cpp:384`): one relaxed atomic store, no allocation, no lock.
- The rescan read-back (`serviceRescan` → `mirrorParamValues(true)`, `ClapHostParams.cpp:176`; the skip is at `ClapHostInternals.h:456`) skips a slot whose host change is still pending.
- The read-back inside `loadState` stays forced, so a loaded state still overrides any pending edit, as before.
- After the fix, the same run keeps 1 (log 50).
- This also resolves the round-1 "open item 4": edits to a CLAP effect that has not yet heard audio were lost. They are now saved in `<parameters>`, restored on load, and not overwritten by the plug-in's rescan.

### Changed assertion in an existing test (made stricter, not weaker)

`Vst3InstrumentIntegrationTest::testStateSurvivesTheProjectFile` (`tests/src/plugins/Vst3InstrumentIntegrationTest.cpp:722`) asserted that right after a reload the host's Level model reads the fixture's default 0.5, while the next assertion requires the instrument to render the saved 0.75.

The fixture's controller does not read the component state back. That is exactly the Odin2/Vitalium pattern from the stress run (r1 log 15). Fix 1 restores the host's saved 0.75 over the stale controller value, so the model now equals what is rendered, and the assertion checks that (`WrittenLevel`).

That stale-value assertion was introduced by `8f40a4f80` (cluster new-p8-265, "keep chunk restoration authoritative"). This round deliberately changes that contract, as the review asks:
- The chunks are still applied first and are still authoritative for every value they restore.
- The host's saved value now wins only where the plug-in's restored value disagrees with it.
- **Owner-visible:** an existing VST3 project whose saved `<parameters>` disagree with its chunk now loads the host's saved value for that parameter (the value the host showed when the project was saved).

### Regressions — each fails without the round-2 product change and passes with it

| Test | What it pins |
|---|---|
| `Vst3InstrumentIntegrationTest::testAValueTheChunksDoNotCarryIsRestoredFromTheHost` (`:1061`) | Level is set and saved before any block or poll carried it, so the chunk holds 0.5 and `<parameters>` hold 0.75. Restore gives model 0.75, and the render is 0.75. |
| `Vst3EffectIntegrationTest::testAValueTheChunksDoNotCarryIsRestoredFromTheHost` (`:455`) | The same on the effect path: model 0.3, processed 0.5 → 0.15. |
| `ClapEffectIntegrationTest::testAValueTheStateDoesNotCarryIsRestoredFromTheHost` (`:360`) | Gain 0.3 is saved with no block run (the plug-in's state still holds 1.0), so the state load leaves 1.0. Restore gives model 0.3, processed 0.15. |
| `ClapInstrumentViewTest::aValueTheStateDoesNotCarryIsRestoredFromTheHost` (`:151`) | The CLAP instrument path: Gain 0.4 in both the model and the value sent to the plug-in. |
| `ClapHostTest::testARescanKeepsAHostValueNotYetDelivered` (`:810`) | The fixture's info rescan (`CLAP_TEST_GAIN_RESCAN=info`) leaves the undelivered 0.3, and the next block applies it. |
| `Vst3InstrumentIntegrationTest::testStateSurvivesTheProjectFile` (`:722`) | The host model equals what the instrument renders after a project reload. |

All use in-tree fixtures only and call `saveSettings`/`loadSettings`/`process` directly. No sleeps or timers, and the event loop is never spun.

### Real plug-ins, this build, the stress script's own `run_subject` (load, render, play, state), parameters exercised

| Plug-in | Before (stress / round 1) | Round 2 | Log |
|---|---|---|---|
| Surge XT (VST3 instrument) | 2/4: `FX A1 -`, `FX A2 -` lost | **4/4** (`M1: -`, `FX A1 -`, `FX A2 -`, `FX B2 -` all exercised) | 17 |
| Surge XT Effects (VST3) | 4/4 | 4/4 | 12 |
| Odin2 (VST3 instrument) | 0/4 | **4/4** | 16 |
| Vitalium (VST3 instrument) | 0/4 | **4/4** | 14 |
| Kars (VST3, MIDI-CC proxies) | 0/4 | **4/4** | 15 |
| ZamComp (VST3, `Current Program`) | 3/4 | **4/4** | 13 |
| Surge XT Effects (CLAP), silent track, one parameter | lost (log 35) | **kept** | 50 |
| Surge XT Effects (CLAP), stress conditions | 1/4 | 1/4 — see below | 10, 51 |
| Surge XT (CLAP instrument), stress conditions | 2/4 | 2/4 — see below | 11, 52 |

### Surge XT CLAP — why the remaining two rows are the plug-in's own values

I kept each saved state file (`scratch/repro4.py` copies it at `plugin.state_save`; the files are in the run folder's `scratch/`). These are the host's own saved `<parameters>` entries:
- **Surge XT Effects, `surgefx-state2.xml` (log 51).**
  - The checked parameters are model indexes 5/9/13/17. The first 5 indexes are Zene's own generic controls (`plugin_compat_steps.py:90`), so these are `<parameters>` entries 0/4/8/12.
  - The host saved Pre-EQ Gain **0.5**, Distortion Model **0.5**, Post-EQ Frequency **0**, FX Type **1**.
  - The check expected 0/1/1 for the first three. When FX Type was set last, Surge switched the effect and reset those parameters, and it told the host before the save.
  - The load gives back exactly what was saved (0.5, 0.5, 0), so the round trip itself is exact. The values the check expected no longer existed in the plug-in when it saved.
- **Surge XT CLAP instrument, `surge-inst-state2.xml` (log 52).**
  - The host saved **1** for `FX A1 -` and `FX A2 -`, and restores 1 ("after load: 1", logs 32 and 33, the latter with no move at all).
  - After that value is delivered, Surge's own patch load zeroes the parameters of its empty (Off) FX slots and reports 0 back. Fix 2 already protects undelivered values, so a change after delivery can only come from the plug-in.
  - Active-slot parameters (`FX B2 AD Clip - Mode`) come back.
  - The VST3 build does not report that reset and keeps 1 (log 17).
- **Why the host doesn't override this:** re-asserting a value after the plug-in has reported changing it would be the host fighting the plug-in's own state. That is the "host re-applies cached values over the restored state" defect this finding asked me to rule out. So I left these two rows as they are, and they are the only rows still failing.

## Round 1 — (a) read-only parameters (unchanged; verified by the review)

VST3 `kIsReadOnly` / `CLAP_PARAM_IS_READONLY` parameters (DPF Buffer Size / Sample Rate, meters, outputs):
- They carry a mark (`include/ParameterReadOnly.h`; set in `Vst3Parameter.cpp:43`, `ClapParameter.cpp:63,72`).
- They are reported `read_only:true` by `controlParameterJson` (`ControlDeviceSupport.cpp:348`; schema `ControlCommandsPluginParams.cpp:106`).
- They are refused by `plugin.param_set` with `refused` (`ControlCommandsPluginParams.cpp:216,265`), and skipped by `plugin.randomise`/`plugin.blend` (`ControlCommandsDeviceParams.cpp:281`).
- The harness skips them (`tests/plugin_compat_steps.py:104`).
- Regressions: `Vst3InstrumentIntegrationTest::testReadOnlyParametersAreReportedAndRefusedOverControl` and `ClapEffectIntegrationTest::testReadOnlyParametersAreReportedReadOnly`.
- Real plug-ins: AIDA-X VST3/CLAP, DPF 3 Band EQ, ZamComp CLAP 4/4 (r1 logs 24-29).

## Commands and exit codes (round 2)

All builds used `CCACHE_TEMPDIR=<run>/ccache-tmp` (the lane's jail refuses ccache's default temp dir).

| Step | Command | EXIT |
|---|---|---|
| fix.patch | `git diff -- plugins/ClapEffect/ClapEffectControls.cpp plugins/ClapEffect/ClapParameter.cpp plugins/ClapEffect/ClapParameter.h plugins/ClapEffect/ClapHost.cpp plugins/ClapEffect/ClapHostInternals.h plugins/ClapEffect/ClapHostParams.cpp plugins/ClapInstrument/ClapInstrument.cpp plugins/Vst3Effect/Vst3EffectControls.cpp plugins/Vst3Effect/Vst3Parameter.cpp plugins/Vst3Effect/Vst3Parameter.h plugins/Vst3Instrument/Vst3Instrument.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| RED build | `cmake --build build --target zene vst3instrument vst3effect clapeffect clapinstrument Vst3InstrumentIntegrationTest Vst3EffectIntegrationTest ClapEffectIntegrationTest ClapInstrumentViewTest ClapHostTest -j4 > logs/40-build-red.log` | 0 |
| RED test | `ctest -R '^(Vst3InstrumentIntegrationTest\|Vst3EffectIntegrationTest\|ClapEffectIntegrationTest\|ClapInstrumentViewTest\|ClapHostTest)$' -V > logs/41-test-red.log` | **8** — 0/5 pass; the six assertions in the table above fail |
| GREEN | `git apply logs/fix.patch`, the same build `> logs/42-build-green.log` | 0 |
| GREEN test | the same ctest `> logs/43-test-green.log` | **0** — 33/33, 17/17, 4/4, 8/8, 19/19, 0 skipped |
| An earlier RED/GREEN of Fix 1 alone | logs 01-04 | 8 / 0 |
| family | `cmake --build build --target <33 tests> -j3 > logs/44-build-family.log`; `ctest -j3 -R '^(ChainPresetMenuTest\|ControlAutomationModesTest\|ControlAutomationScriptTest\|ControlChainPresetTest\|ControlRegistryTest\|DelayControlsTest\|DeviceAbUndoTest\|DeviceCopyTest\|DeviceDspTest\|DevicePanelTest\|DeviceToolsChainTest\|DeviceToolsTest\|InstrumentLayersCommandsTest\|PluginControlDialogsTest\|ReversibilityUndoDeviceTest\|ScriptDawEffectsTest\|SfzPlayerTest\|Vst3HostTest\|Vst3ChunkProbeTest\|Vst3InstrumentTest\|Vst3EffectIntegrationTest\|ClapHostTest\|ClapInstrumentViewTest\|Vst3InstrumentIntegrationTest\|ClapEffectIntegrationTest\|ClapTestInstrumentUnitTest\|ClapBusMapTest\|Vst3BusMapTest\|InstrumentSetupUiTest\|MissingDevicesPreserveTest\|InstrumentLatencyPdcTest\|ControlHostedDeviceCatalogueTest\|ControlDeviceCatalogueTest)$' > logs/45-test-family.log` | 0, **0** — 32/32 |
| (environment) | `ControlDeviceCatalogueTest` failed in log 08 with "Patman plugin is unavailable": the module was not built in this tree. After `--target patman`, it passes (log 09). | 8 → 0 |
| gates | the run-folder copy of `feature_check.py` (only `ROOT` and its log dir changed, because the original writes beside the worktree, outside the lane's jail) `> logs/46-feature-check.log` | **0** — 8/8 PASS |
| real plug-ins | `python3 scratch/repro3.py '<subject>' load,render,play,state` (logs 10-17); `repro4.py` (keeps state, logs 30, 31, 51, 52); `repro2.py` single parameter (logs 32-35, 50) | 0 each |

## Self-check

- **Fails without the product change, passes with it.** `git apply -R logs/fix.patch` + rebuild → ctest EXIT=8 (log 41, all six assertions); `git apply logs/fix.patch` + rebuild → EXIT=0 (log 43). Round 1's (a) proof is r1 logs 20/22 (EXIT 8/0), confirmed by the review.
- **Deterministic.** The tests call `saveSettings`/`loadSettings`/`saveState`/`restoreState`, `process()` and `serviceMainThread()` directly, on the in-tree fixtures. The rescan is triggered through the fixture's own `CLAP_TEST_GAIN_RESCAN=info` switch, serviced synchronously. There is no `qWait`, no sleep, and no event-loop spin in the new code.
- **At the cause and complete.**
  - **VST3:** both load paths (effect `Vst3EffectControls::loadSettings` and instrument `Vst3Instrument::loadSettings`) use the one shared `restoreSavedParameters`. Both save paths already wrote `<parameters>`.
  - **CLAP:** both save paths and both load paths (`ClapEffectControls`, `ClapInstrument`) use the shared `ClapParamModelList::saveValues`/`restoreValues`.
  - **Read-back callers:** the only two are `loadState` (forced) and `serviceRescan` (keeps pending values). Plug-in output events (`outEventsTryPush`) are produced in the same process() call that delivered the host's changes, so they cannot overwrite an undelivered one.
  - **Presets and chains:** `plugin.preset_load`, `plugin.state_load`, chain presets and undo all reach the same `loadSettings`.
- **No collateral damage.**
  - CLAP documents without `<parameters>` load exactly as before.
  - VST3 documents load as before wherever the chunk agrees with the saved `<parameters>`. Where they disagree, the host's saved value now wins; the deliberate contract change is described above.
  - Realtime: one relaxed atomic store per delivered parameter change on the audio thread, no allocation and no lock.
  - Undo: loading takes the same single step it always did.
  - No command, schema or save element is removed. CLAP gains a `<parameters>` child, which older builds ignore (they read only `<state>`).
- **ALREADY-FIXED / FALSE-POSITIVE:** not claimed.
- **Not in this round's scope:** the farm's copy of the harness (`~/.cache/zene-plugin-farm/harness/plugin_compat_steps.py`, read-only here) needs refreshing from the tree before a stress rerun.
