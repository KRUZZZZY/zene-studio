IMPLEMENTED

# Q050-214 - Plug-in editor gesture undo

Details and design: `docs/050-features/Q050-214.md`. Limits: `docs/KNOWN-LIMITATIONS.md`, "Plug-in editor gesture undo".

## Acceptance

Command for both rows: `cd build/tests && ctest -R '^PluginGestureUndoTest$' --output-on-failure`. Four subjects: CLAP effect, CLAP instrument,
VST3 effect, VST3 instrument, on the real main window and the product's hosting modules.

| Criterion | Proof | Green | Red |
| --- | --- | --- | --- |
| Change a plug-in value with begin/end gesture events; one undo restores it and redo reapplies it | `checkOneGestureOneStep`, `checkWhatMakesAStep` | exit 0 | exit 8, all product hunks reversed: 4 of 4 subjects fail on the step count ("Compared values are not the same") |
| GUI and agent history against the same journal checkpoint | `checkOneGestureOneStep` (Edit menu action, `control.undo`/`redo`, panel buttons clicked, on one step), `checkTheAgentsEdits`, `checkAnUndoInTheMiddleOfADrag` | exit 0 | exit 8, only the `control.undo` hunk reversed: 4 of 4 subjects fail with "EVICTED" |

Full-reversal red and the green were run on the committed tree: `test-red3.log` (RED_EXIT=8, 4 of 4 subjects) and `test-final.log` (FINAL_EXIT=0).
The `control.undo`-only red (`test-red2.log`, RED2_EXIT=8) was run before the test was split into smaller functions, with the same assertions, and not repeated
after. Logs are in `runs/Q050-214.r1/logs/`.

`test-final.log`: 23 of 23 passed, FINAL_EXIT=0 - the new test and the neighbours listed in the feature doc. An earlier run (`test-8.log`, exit 8) failed three of them because
the modules they load were not built; after building the modules they pass. The whole suite was not run (the brief forbids it).

## Self-check

- **Each new test fails with the product change reverted, then passes restored.** Yes. `git apply -R` of the product diff, rebuild, `ctest`: RED_EXIT=8 (`test-red3.log`); restored, rebuilt: FINAL_EXIT=0 (`test-final.log`). Only the `control.undo` hunk: RED2_EXIT=8; restored: GREEN2_EXIT=0.
- **Every acceptance criterion is proven by a test I ran.** Yes: `PluginGestureUndoTest`, exit 0, both rows above.
- **Each edit is one undo step; reversibility classes are honest.** Yes: the test asserts `undoDepth()` after every step. No new command and no class changed. `control.undo` changed its description and one refusal; snapshot regenerated live (SNAP exit 0), `doc-figures` exit 0, `ControlCommandsSnapshot` and `ReversibilityContractTest` pass.
- **Nothing added on the audio thread allocates, frees, locks, waits, does I/O or emits a signal.** `HostedPlugin::Impl::outEventsTryPush` and `markGesture` (CLAP, from `process()`): `indexOfParam`, the same lookup the value path there already did, and `PluginParamGesture::begin()`/`end()`, an atomic increment and a fence. VST3 `beginEdit`/`endEdit` run on the UI thread per the SDK, and do the same two things in any case. Journal and model work is only in `PluginGestureHistory::pull`, on the GUI poll. No allocation-counter test was added. Local TSan does not run here; the ordering is reasoned in the feature doc.
- **Old projects load and render exactly as before; new data round-trips.** No format change and no new data: nothing is saved. No render path changed. `Vst3EffectIntegrationTest`, `ClapEffectIntegrationTest`, `Vst3InstrumentIntegrationTest` and `ClapInstrumentViewTest` (state round trips, renders) pass; no separate render comparison was run. `poll()`'s value path differs only in the journal flag during a gesture.
- **Portability.** No Qt-6-only API added; no `small`/`near`/`far`/`interface`/`min`/`max` identifiers; no `std::filesystem`; no POSIX call. Built only on this Linux box with this Qt: hosted CI was not run. The fixtures' scripted gesture reads the environment, as the fixtures already do.
- **GUI path WARN ("no GUI file changed").** The GUI entry points already exist and are unchanged: the plug-in's own editor is where the gesture happens, and Edit > Undo/Redo and the Undo History panel take it back. The test triggers the menu's actions and clicks the panel's buttons.
- **`feature_check.py` FAIL in `submit_check.py`.** It stops with PermissionError before running a gate: it writes logs to `../zene-feat-q050-214-feature-check`, outside this session's write-jail. I ran the same gates through its own functions with logs in the run folder (`logs/gates3`, after the code's last commit): fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures exit 0; complexity and file-length exit 1 as on the train, with no new item in my files.
- **Build note.** ccache's configured temp dir refused writes here, so builds ran with `CCACHE_TEMPDIR=/tmp/q214-ccache-tmp`.
