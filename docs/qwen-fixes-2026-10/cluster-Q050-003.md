IMPLEMENTED

Q050-003 VST3 host claims and output-bus documentation. Round-2 rework: the README VST3
editor sentence is now qualified to say the host opens the plug-in's own editor only when
the view supports X11 embedding (`X11EmbedWindowID`) and that this editor path is X11-only;
the third-party-editor-untested caveat is retained. The output-bus and instrument-latency
wording already matched the merged host. Docs only; no product code, no new commands, no
format change.

## Acceptance table (exact commands and EXIT codes seen)

| Criterion | Command (from the stated dir, unpiped, EXIT recorded) | Result |
|---|---|---|
| Build the proving targets (already current; docs-only change) | binaries present under `build/tests/` from the r1 build (`cmake --build build --target Vst3BusMapTest ControlPluginBusesTest InstrumentLatencyPdcTest PluginNativeEditorTest -- -j4`, with train-5 env: `unset LMMS_PLUGIN_DIR`, `QT_QPA_PLATFORM=offscreen`, ccache + PKG_CONFIG_PATH + LD_LIBRARY_PATH exports) | binaries built, EXIT=0 (r1) |
| AC1 README output-bus paragraph + third-party caveat retained | `ctest -R "Vst3BusMapTest\|ControlPluginBusesTest\|InstrumentLatencyPdcTest\|PluginNativeEditorTest" --output-on-failure > <logs>/test-1.log 2>&1; echo EXIT=$?` (from `build/tests`) — 4/4 passed: ControlPluginBusesTest 1.51s, InstrumentLatencyPdcTest 3.36s, PluginNativeEditorTest 3.69s, Vst3BusMapTest 0.05s | EXIT=0 |
| AC2 editor wording checked against merged host behavior (now X11-qualified) | same run, `PluginNativeEditorTest` — refused attach leaves no window, open window sized to view+header, reopen raises, detach once (offscreen); README now names the X11-embedding precondition enforced at `plugins/Vst3Effect/Vst3EditorSession.cpp:190-195` (`view->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID) != kResultTrue` -> refuse) | EXIT=0 |
| AC2 instrument-latency wording checked against merged host behavior | same run, `InstrumentLatencyPdcTest::routedOutputBusIsAlignedWithTheMainOutput` — mixer counts the instrument latency in the routed bus's target alignment | EXIT=0 |
| Stale wording gone, caveat kept, X11 caveat present | `rg -n "does not open\|no multi-out\|no preset management\|no instrument latency" README.md` — 0 matches; `rg -n "X11EmbedWindowID\|X11-only" README.md` — lines 65 and 67; `rg -n "third-party" README.md` — lines 76-77, 145 retain the caveat | seen, 0 stale hits |
| complexity gate | `bash tests/complexity-gate.sh --check > <logs>/gate-1-complexity.log 2>&1` | EXIT=1, pre-existing train-5 red (68 fns over CCN 10); none of this lane's files listed (grep for README/KNOWN-LIMITATIONS/Q050-003: NONE) |
| file-length gate | `bash tests/file-length-gate.sh --check > <logs>/gate-2-filelength.log 2>&1` | EXIT=1, pre-existing train-5 red (19 files over 500 lines); none of this lane's files listed (grep: NONE) |
| fork-sources gate | `bash tests/fork-sources-gate.sh > <logs>/gate-3-fork.log 2>&1` | EXIT=0 |
| all-sources reproduce | `bash tests/all-sources-reproduce.sh > <logs>/gate-4-allsources.log 2>&1` | EXIT=0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > <logs>/gate-5-noupstream.log 2>&1` | EXIT=0 |
| namespace check | `tests/scripted/check-namespace > <logs>/gate-6-namespace.log 2>&1` | EXIT=0 |
| no-tautology | `bash tests/no-tautology-gate.sh > <logs>/gate-7-notautology.log 2>&1` | EXIT=0 |

`<logs>` = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-003.r2/logs`.

## What was built (file:line)

- `README.md:62-79` — VST3 paragraph: the plug-in's **own editor opens** only "When the
  plug-in's editor view supports **X11 embedding** (`X11EmbedWindowID`)", next to the
  generated knob grid (instrument view's "Show plugin editor" button, `plugin.editor_open`),
  and "a view that does not offer X11 embedding is refused with a reason, so this editor
  path is **X11-only**"; output buses routable (`plugin.output_*`, Instrument Setup > Output
  Buses); latency reported (routed bus carries it, `InstrumentLatencyPdcTest`); still missing
  narrowed to plug-in preset lists + out-of-process hosting; third-party caveat retained.
- `README.md:81-86` — CLAP instrument editor sentence (`plugins/ClapInstrument/ClapInstrument.cpp:171`,
  X11-embedded only).
- `README.md:145` — "No patcher GUI (a hosted plug-in's own X11-embedded editor does open,
  see above; there is no editor path off X11)".
- `docs/KNOWN-LIMITATIONS.md:174-183, 214-222` — already carried the X11 limit and the
  dated Q050-003 correction naming `plugins/Vst3Instrument/Vst3Instrument.cpp:239`
  (`enableOutputBus`), `:262` (latency), `buses_test_instrument`, and
  `Vst3BusMapTest::testOutputBusesKeepInactiveOnes` (from r1).

Source of the X11 claim, checked at this tip: `Vst3EditorSession::open`
(`plugins/Vst3Effect/Vst3EditorSession.cpp:190`) refuses the editor when
`view->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID) != kResultTrue`, and
`KNOWN-LIMITATIONS.md:174-183` already stated the same ("createView, X11 embedding ... no
third-party plug-in's editor has been opened on a real X11 display").

## Known limits

- Real multi-output VST3 plug-in routing unproven (in-tree fixture has one output bus);
  effect multi-out not claimed (effect hosts flatten buses into the chain).
- Editor proof is one offscreen contract test; no third-party editor opened on a real
  display; the editor path is X11-embedded only.
- No product code changed, so no new realtime/portability surface; local TSan not run
  (unsupported on this box: runtime crash at thread creation).

## Files committed

- `README.md`
- `docs/KNOWN-LIMITATIONS.md`
- `docs/050-features/Q050-003.md`
- `docs/qwen-fixes-2026-10/cluster-Q050-003.md` (this file)
