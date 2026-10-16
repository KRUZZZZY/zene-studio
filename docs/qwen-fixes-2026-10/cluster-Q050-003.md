IMPLEMENTED

Q050-003 VST3 host claims and output-bus documentation: README's VST3 paragraph and the
KNOWN-LIMITATIONS instrument-hosting bullet now match the merged host (own editor opens,
instrument output buses routable, latency reported), with the third-party-untested caveat
retained. Docs only; no product code, no new commands, no format change.

## Acceptance table (exact commands and EXIT codes seen)

| Criterion | Command (from the stated dir, unpiped, EXIT recorded) | Result |
|---|---|---|
| Build the proving targets | `cmake --build build --target Vst3BusMapTest ControlPluginBusesTest InstrumentLatencyPdcTest PluginNativeEditorTest -- -j4 > <logs>/build-1.log 2>&1; echo EXIT=$?` (worktree root, with the train-5 env: `unset LMMS_PLUGIN_DIR`, `QT_QPA_PLATFORM=offscreen`, ccache + PKG_CONFIG_PATH + LD_LIBRARY_PATH exports) | EXIT=0 |
| AC1 README output-bus paragraph + caveat; AC2 editor and latency wording | `ctest -R "Vst3BusMapTest\|ControlPluginBusesTest\|InstrumentLatencyPdcTest\|PluginNativeEditorTest" --output-on-failure > <logs>/test-1.log 2>&1; echo EXIT=$?` (from `build/tests`) — 4/4 passed: ControlPluginBusesTest 1.21s, InstrumentLatencyPdcTest 3.05s, PluginNativeEditorTest 2.82s, Vst3BusMapTest 0.04s | EXIT=0 |
| Stale wording gone, caveat kept | `rg -n "does not open\|no multi-out\|no preset management\|no instrument latency" README.md` — 0 matches; `rg -n "third-party" README.md` — lines 74-75, 143 retain the caveat | seen, 0 stale hits |
| complexity gate | `bash tests/complexity-gate.sh --check > <logs>/gate-1-complexity.log 2>&1` | EXIT=1, pre-existing train-5 red (68 fns over CCN 10); none of this lane's files listed |
| file-length gate | `bash tests/file-length-gate.sh --check > <logs>/gate-2-filelength.log 2>&1` | EXIT=1, pre-existing train-5 red (19 files over 500 lines); none of this lane's files listed |
| fork-sources gate | `bash tests/fork-sources-gate.sh > <logs>/gate-3-fork.log 2>&1` | EXIT=0 |
| all-sources reproduce | `bash tests/all-sources-reproduce.sh > <logs>/gate-4-allsources.log 2>&1` | EXIT=0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > <logs>/gate-5-noupstream.log 2>&1` | EXIT=0 |
| namespace check | `tests/scripted/check-namespace > <logs>/gate-6-namespace.log 2>&1` | EXIT=0 |
| no-tautology | `bash tests/no-tautology-gate.sh > <logs>/gate-7-notautology.log 2>&1` | EXIT=0 |

`<logs>` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-003.r1/logs`.

## What was built (file:line)

- `README.md:62-76` — VST3 paragraph: own editor opens (`plugin.editor_open`), output
  buses routable (`plugin.output_*`, Instrument Setup > Output Buses), latency reported
  (routed bus carries it, `InstrumentLatencyPdcTest`); still missing narrowed to plug-in
  preset lists + out-of-process hosting; third-party caveat retained.
- `README.md:76-79` — CLAP instrument editor sentence corrected
  (`plugins/ClapInstrument/ClapInstrument.cpp:171`, X11-embedded only).
- `README.md:143` — "No patcher GUI (a hosted plug-in's own editor does open, see above)".
- `docs/KNOWN-LIMITATIONS.md:214-222` — dated Q050-003 correction naming
  `plugins/Vst3Instrument/Vst3Instrument.cpp:239` (`enableOutputBus`), `:262`
  (latency), `buses_test_instrument`, `Vst3BusMapTest::testOutputBusesKeepInactiveOnes`.

Verified at this tip before writing: `Vst3Instrument::openNativeEditor` (`:188`),
`enableOutputBus` (`:239`), `cacheLatency` (`:262`); "Show plugin editor" button
(`src/gui/PluginNativeEditor.cpp:252`, VST3 view `:85`); no `ProgramList`/`unitInfo`
under `plugins/Vst3Instrument/` or `plugins/Vst3Effect/`; in-tree VST3 fixture declares
one audio output (`tests/data/vst3-test-instrument/vst3-test-instrument.cpp:258`).

## Known limits

- Real multi-output VST3 plug-in routing unproven (fixture has one bus); effect multi-out
  not claimed (effect hosts flatten buses into the chain).
- Editor proof is one offscreen contract test; no third-party editor opened on a real display.
- No product code changed, so no new realtime/portability surface; local TSan not run
  (unsupported on this box).

## Files committed

- `README.md`
- `docs/KNOWN-LIMITATIONS.md`
- `docs/050-features/Q050-003.md`
- `docs/qwen-fixes-2026-10/cluster-Q050-003.md` (this file)
