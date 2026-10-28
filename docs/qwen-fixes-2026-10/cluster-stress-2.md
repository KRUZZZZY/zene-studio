FIXED

# Cluster stress-2: VST3 instruments rendered silence (Dexed, Vitalium, JC303, Surge XT, Vaporizer2)

**Verdict: REAL, FIXED.** The silence had three separate host causes in `plugins/Vst3Effect/Vst3Host.cpp`.
All three are fixed, and each has a deterministic fixture regression. With the fix, the five affected
instruments render a held note both through `HostedPlugin` and through the real product (`zene` +
`stress.py`). Stochas still renders silence, and that is correct: it is a MIDI step sequencer with no
audio of its own.

Log folders: the original fix's evidence is in `~/.local/share/zene-bunny-fix/qwen/runs/stress-2.r1/logs`
(the numbered logs in "Evidence table" and "Self-check"). The port onto `050/m0-train-21` has its own
folder, `~/.local/share/zene-bunny-fix/qwen/runs/stress-2.r3/logs` (see "Integration onto 050/m0-train-21").
Line numbers below are for this branch.

## Root causes (with evidence)

1. **The host never activated an audio bus** (JC303 and Vaporizer2 have only this cause).
   - The host only ever activated the event input (`plugins/Vst3Effect/Vst3MidiEvent.cpp:57-60`) and aux
     outputs from index 1 up (`Vst3HostReports.cpp:84`). It never called `activateBus(kAudio, ...)` for a
     main bus.
   - The SDK creates every bus with `active (false)` (`build/vst3sdk/public.sdk/source/vst/vstbus.cpp:26`).
   - `kDefaultActive` only asks the host to activate a bus: "The bus should be activated by the host per
     default on instantiation (activateBus call is requested)" (`ivstcomponent.h:105`, in-tree copy under
     `plugins/CarlaBase/carla/source/includes/vst3sdk/`).
   - JUCE starts every bus with `hostActive = false` (in-tree copy
     `plugins/CarlaBase/.../juce_audio_processors/format_types/juce_VST3Common.h:566`). It copies its audio
     only into host-activated buses (`:939`). All six affected plug-ins are JUCE builds, Vitalium included
     (DISTRHO-Ports).
   - Fix: `prepare()` activates every mapped audio bus after `setupProcessing()` and before
     `setActive(true)` (`Vst3Host.cpp:548-549`, helper `activateMappedAudioBuses` at `:233`).
2. **An instrument with no default-active output mapped nothing** (Vitalium).
   - Vitalium reports its only output bus with `kDefaultActive=0` (measured: log 07, "output bus 'Output'
     ... defaultActive=0").
   - `mapBuses` skips inactive buses (`Vst3BusMap.cpp:37`), so the layout had 0 outputs.
   - Fix: for instruments, the first output bus is used when none is on by default
     (`preferFirstOutputBus`, `Vst3Host.cpp:221`, applied at `:291`). This is the policy the event input
     already follows (`Vst3MidiEvent.cpp:52-54`).
3. **Every parameter's reported value was sent on the first block after each `prepare()`** (Dexed and
   Surge XT).
   - `lastParamValues` was reset to `lowest()` in `prepare()`, so `process()` (`Vst3Host.cpp:709`) treated
     every parameter as changed.
   - Dexed's controller reports 0.0 for Cutoff, Resonance, Output and MonoMode, both as
     `ParameterInfo::defaultNormalizedValue` and from `getParamNormalized()` (log 23 / the RAW lines of the
     experiment). Its patch plays other values, so the flush set Output and Cutoff to zero.
   - Measured with temporary experiments, all reverted:
     - Suppressing the flush made both play, 5/5 runs each: Dexed 0.1308; Surge XT 0.41-0.46.
     - The full flush was silent 5/5.
     - Flushing only Dexed's parameters 0-39 was silent; flushing 80+ played.
     - The mirror never went stale (0 stale of 2238/2855, log 23).
   - Fix: `load()` records the reported values as what the processor already holds (`Vst3Host.cpp:349`).
     `prepare()` no longer resets them. A change is sent only when the host's value moves, and a value the
     host set before `prepare()` is still delivered.

Sibling fix asked for by the owner: the first `prepare()` at load ignored its result in both plug-in
classes. A refusal is now logged: `Vst3Instrument.cpp:126-130`, `Vst3Effect.cpp:114-118` (GUI thread, constructor).

Not causes, checked: the note on/off construction (`Vst3MidiEvent.cpp:85-157`) and the event-bus choice.
An `IRunLoop` is offered only on the editor's plug frame (`Vst3EditorSession.cpp:54`). Surge XT's data path
was not the cause either: linking its data in a scratch HOME left it silent (log 11).

## Tests

All in `tests/src/plugins/Vst3InstrumentTest.cpp`, ctest `^Vst3InstrumentTest$`:

- `testTheHostActivatesTheAudioOutputBus` (:578). The fixture now renders only into an activated output bus
  (`tests/data/vst3-test-instrument/vst3-test-instrument.cpp:193`, `:437-438`; design note "BUS ACTIVATION").
  The test asserts that a held note renders 0.5 from both the stereo and the mono fixture.
- `testAnInstrumentWithItsOutputOffByDefaultStillPlays` (:602). The fixture declares its output without
  `kDefaultActive` while `VST3_TEST_INSTRUMENT_OUTPUT_OFF` is set at creation (`:297-299`). The test asserts
  `outputs == 2` and that a note is audible.
- `testTheHostSendsOnlyTheParameterChangesItMade` (:626). It uses the new `LMMS_TESTING` seam
  `HostedPlugin::lastParameterChangeCount()` (`Vst3Host.h:291`, `Vst3Host.cpp:909`) and asserts:
  - a value set before `prepare()` is sent once (1, then 0);
  - an untouched instance sends 0 changes, on the first block and after a re-prepare;
  - a later host change is sent once.
- `testThirdPartyJuceInstrumentsRenderANote` (:678, data-driven). It loads Surge XT, Dexed, Vitalium, JC303
  and Vaporizer2 from the farm (`ZENE_PLUGIN_FARM`, default `~/.cache/zene-plugin-farm/root`), plays a held
  note for 44 blocks and asserts peak > 0.001. Each row `QSKIP`s with a clear message when its plug-in is
  absent (log 43: 20 passed, 5 skipped). The three fixture tests above always run.
- `Vst3InstrumentFixtureProbe` (an independent raw-SDK host of the fixture) now activates the buses it
  drives, as a conforming host does (`Vst3InstrumentFixtureProbe.cpp:233-234`).

## Evidence table (commands run, exit codes seen)

Builds used `CCACHE_DISABLE=1` because ccache's temp dir is outside the write jail ("Permission denied",
log 01). ctest was run from `build/tests`.

| Step | Command | Result |
|---|---|---|
| RED, original code (tests only) | `ctest -R '^Vst3Instrument(Test\|FixtureProbe)$' --output-on-failure` (log 03) | EXIT=8; 11 passed, 12 failed; Surge XT/Dexed/JC303/Vaporizer2 peak 0.0000, Vitalium "outputs >= 2" FALSE |
| GREEN, final | `cmake --build build --target zene vst3effect vst3instrument Vst3HostTest Vst3ChunkProbeTest Vst3EffectIntegrationTest Vst3InstrumentTest Vst3InstrumentIntegrationTest Vst3InstrumentFixtureProbe Vst3BusMapTest -j4` (log 40) | BUILD_EXIT=0 |
| GREEN family | `ctest -R '^Vst3' --output-on-failure` (log 41) | EXIT=0, 7/7 tests: Vst3BusMapTest, Vst3HostTest, Vst3ChunkProbeTest, Vst3EffectIntegrationTest, Vst3InstrumentFixtureProbe, Vst3InstrumentTest, Vst3InstrumentIntegrationTest |
| GREEN detail | `./Vst3InstrumentTest` (log 42) | EXIT=0, 25 passed; peaks Surge XT 0.4528, Dexed 0.1308, Vitalium 0.3576, JC303 0.2261, Vaporizer2 0.1580 |
| 4b revert | `git apply -R .../logs/fix.patch` | REVERT_EXIT=0 |
| 4b RED | rebuild (log 30, BUILD_EXIT=0), `./Vst3InstrumentTest` (log 31) | TEST_EXIT=14; 11 passed, 14 failed. Includes `OutputOffByDefault` (outputs 0 vs 2), `OnlyTheParameterChanges` (2 vs 1), `ActivatesTheAudioOutputBus` (rendered 0) and all 5 third-party rows |
| 4b re-apply | `git apply .../logs/fix.patch` | APPLY_EXIT=0 |
| 4b GREEN | rebuild (log 32, BUILD_EXIT=0), `ctest -R '^Vst3'` (log 33) | TEST_EXIT=0, 7/7 |
| No farm | `ZENE_PLUGIN_FARM=<missing> ./Vst3InstrumentTest` (log 43) | EXIT=0, 20 passed, 5 skipped |
| Product | `stress.py run --only ...` against this worktree's `build/zene` (copy in `logs/stress-copy`, log 39) | render pass: Surge XT 0.971, Dexed 0.479, Vitalium 1.000, JC303 0.078, Vaporizer2 0.585 (all 0.0 on train-18); Stochas silent (expected) |
| Gates | `python3 logs/feature_check_logs_here.py logs/feature-check <worktree>`: `feature_check.py` unchanged except its logs dir, because the write jail refuses `<worktree>/../zene-qwen-stress-2-feature-check` (log 36 shows the PermissionError) (log 44) | EXIT=0, RESULT: PASS. fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS |

`fix.patch` is the product diff without the 7-line test seam (`lastParameterChangeCount`). That seam was
kept in both states, so the reverted build still compiles and fails on assertions, not on a missing
symbol. `fix-full.patch` holds the whole diff.

Product stress run against train-18 (the same `stress.py`):
- **state**: no regression. Vitalium improved from 0/4 to 3/4 parameters restored. JC303, Surge XT and
  Stochas fail exactly as before (MIDI CC proxies, FX slots), a separate finding.
- **Surge XT Effects**: rendered 0.0 before and 0.99997 now, because effects needed the bus activation too.
- **reopen**: the reopen renders for Surge XT (0.028) and Vaporizer2 (0.00058) are quieter than their first
  renders. The reopen step runs after `play_fuzz`, which writes random values into 32 parameters. Not
  measured further.

## Integration onto 050/m0-train-21 (round 3, `integration/stress-2-37eaf07557`)

The accepted fix (`9c37bd416`, base `7641e9493`) was applied onto the train tip `37eaf0755`. Every product
file applied cleanly. Two test files conflicted, and both were resolved by keeping both sides:

- `tests/src/plugins/Vst3InstrumentTest.cpp`: the train moved the two editor cases
  (`testTheEditorAttachesResizesAndDetaches`, `testTheEditorOpensAtTheDeviceScale`) into
  `tests/src/plugins/Vst3EditorTest.cpp` (Q050-490, `cdeb5d6e4d`). The resolution keeps that move, so the
  editor cases are neither here nor duplicated, and it adds the four stress-2 cases.
- `tests/upstream-modifications.txt`: the train's appended rows and the three stress-2 rows are both kept.
  The file ends with a newline.

I checked the clean-applied product hunks against the code the train changed. `lastParamValues` is still
read only at `Vst3Host.cpp:709`. The only `activateBus` callers are the new helper (`:238`), the event bus
(`Vst3MidiEvent.cpp:59`) and the aux outputs (`Vst3HostReports.cpp:88`), the same set as on the
original base.

Builds used `CCACHE_TEMPDIR=<log folder>/ccache-tmp`, because ccache's configured temp dir is outside the
write jail (log 01: "Permission denied"). ctest was run from `build/tests`. Logs are in the r3 folder.

| Step | Command | Result |
|---|---|---|
| Build | `cmake --build build --target zene vst3effect vst3instrument Vst3BusMapTest Vst3HostTest Vst3ChunkProbeTest Vst3EffectIntegrationTest Vst3EditorTest Vst3InstrumentFixtureProbe Vst3InstrumentTest Vst3InstrumentIntegrationTest -j4` (log 02) | EXIT=0 |
| GREEN family | `ctest -R '^Vst3' --output-on-failure` (log 03) | EXIT=0, 8/8 (Vst3BusMapTest, Vst3HostTest, Vst3ChunkProbeTest, Vst3EffectIntegrationTest, Vst3EditorTest, Vst3InstrumentFixtureProbe, Vst3InstrumentTest, Vst3InstrumentIntegrationTest) |
| GREEN detail | `ctest -R '^Vst3Instrument(Test\|FixtureProbe)$' -V` (log 04) | EXIT=0. Vst3InstrumentTest 23 passed, 0 failed, 0 skipped; peaks Surge XT 0.4179, Dexed 0.1308, Vitalium 0.3576, JC303 0.2261, Vaporizer2 0.1580. Probe "RESULT: PASS" |
| 4b revert | `git diff -- <product files> > logs/fix.patch && git apply -R logs/fix.patch` (fix.patch leaves out the 7-line test seam, as in r1; `fix-full.patch` holds the whole product diff) | REVERT_EXIT=0 |
| 4b RED | rebuild (log 05, BUILD_EXIT=0), `ctest -R '^Vst3InstrumentTest$' -V` (log 06) | TEST_EXIT=8. 9 passed, 14 failed: `ActivatesTheAudioOutputBus` (rendered 0), `OutputOffByDefault` (values differ), `OnlyTheParameterChanges` (values differ), all 5 third-party rows (silence; Vitalium "maps no stereo output bus"), and 6 older fixture cases that need an audible fixture |
| 4b re-apply | `git apply logs/fix.patch` | APPLY_EXIT=0; `git diff` empty afterwards (working tree equals the index) |
| 4b GREEN | rebuild (log 07, BUILD_EXIT=0), `ctest -R '^Vst3' --output-on-failure` (log 08) | TEST_EXIT=0, 8/8 |
| Gates | `python3 feature_check.py <worktree>` (log 09) | EXIT=1, `PermissionError` on `<worktree>-feature-check`, the write jail (see the Self-check) |
| Gates | `python3 logs/feature_check_logs_here.py logs/feature-check <worktree>` (log 10) | EXIT=0, RESULT: PASS. All 8 rows PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length |

Not run in round 3: the `stress.py` product render, which is covered in r1. The product hunks are
byte-identical to the accepted patch.

## Self-check

- **The regression fails with the product change reverted and passes with it.** Yes:
  - reverted: log 31, TEST_EXIT=14, three fixture tests plus five third-party rows failed with assertion
    messages;
  - re-applied: log 33, TEST_EXIT=0, 7/7.
- **Deterministic.** The fixture renders a constant level, and all three fixture tests assert exact sample
  values or exact counts, through:
  - the `lastParameterChangeCount()` test seam;
  - the fixture's creation-time variable `VST3_TEST_INSTRUMENT_OUTPUT_OFF`.

  There are no sleeps, no wall-clock timing and no threads. The third-party rows are offline `process()`
  calls with a fixed note and block count; repeated runs gave identical peaks for Dexed, JC303, Vitalium
  and Vaporizer2.
- **At the cause and complete.**
  - Every caller of `HostedPlugin::prepare()` gets the activation: the instrument's load and re-prepare
    and `enableOutputBus`, and the effect's load and re-prepare.
  - `activateOutputBuses` (MX-114) still activates aux buses and marks them active, so the next `prepare()`
    re-activates them consistently.
  - `outputBusesOf` reads the same descriptors, so the output-bus router maps the Vitalium bus as index 0.
  - The `lastParamValues` baseline is sized at `load()`, the only place `params` is filled. `process()` is
    the only reader, and `loadState()` updates only the mirror, as before.
  - Both plug-in classes now log a failed first `prepare()`.
- **No collateral damage.**
  - Saved projects and state chunks are not touched: no format change, and `saveState`/`loadState` are
    unchanged.
  - Plug-ins that ignore bus state (the SDK fixtures, `Vst3EffectIntegrationTest`, `Vst3ChunkProbeTest`)
    behave as before: 7/7.
  - The output fallback applies only to instruments that have no default-active output.
  - Parameters still reach the processor whenever the host changes them, including before
    `prepare()` (asserted).
  - Realtime: nothing new runs on the audio thread. `activateBus` runs in `prepare()` (GUI thread), and the
    baseline is filled in `load()`.
  - No control command was added (the ControlCommandsSnapshot canary is unchanged).
- **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not applicable: the verdict is FIXED, and log 03 shows
  the bug on the unmodified train-18 tree.
- **submit_check "gates (feature_check.py)" shows FAIL, and that is the sandbox, not a gate.**
  `submit_check.py:117` runs `feature_check.py <worktree>` directly. That script first does
  `logs.mkdir()` on `<worktree>/../zene-qwen-stress-2-feature-check` (`feature_check.py:70-71`), and this
  lane's write jail refuses the directory: `PermissionError: [Errno 13]`, logs 36 and the rerun after the
  commit. The same `feature_check.py` code, executed with only that directory pointed into the log folder
  (`logs/feature_check_logs_here.py`), ran every gate on the committed tree: EXIT=0, all 8 rows PASS
  (log 44). Outside this sandbox the unmodified command needs no change.
- **Round 3 (integration).** Logs 06 and 08 above repeat the RED and GREEN proof on the train-21 tip:
  TEST_EXIT=8 reverted, TEST_EXIT=0 with the fix re-applied. The gates ran on this tree with only the log
  directory redirected: log 10, EXIT=0, all rows PASS. The unmodified command fails here with the same
  `PermissionError` (log 09).
