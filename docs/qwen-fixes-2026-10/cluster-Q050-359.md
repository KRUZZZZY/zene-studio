IMPLEMENTED

# Q050-359 - Carla Rack VST3 opening stability (round 2)

Branch `feat/050-Q050-359-oc`, worktree `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-Q050-359`.
Logs and scratch: `/home/kruzzzzy/.config/kads/zene/state/runs/Q050-359.r2/logs`.

Round 1 fixed the crash (the two Carla host modules decline when this build links the weak stand-in, whose native
descriptor is nullptr) and was sent back for a data-loss finding: a **refused** Carla Rack replacement still destroyed the
musician's instrument before finding out, and the next save persisted the `dummyinstrument` placeholder with no undo
entry. Round 2 moves the fix into the seam and inverts the regression test.

- `2e56348fb6` - `fix(l10): keep the instrument when a load is refused`: `InstrumentTrack::loadInstrument` is
  trial-then-swap (candidate instantiated and judged first; a refusal deletes it and returns null with the previous
  instrument, its state, the track name and `m_preservedInstrument` untouched), and the callers that dereferenced the
  result unconditionally now report the refusal (`TrackContainerView.cpp` drop paths through a new
  `InstrumentLoaderThread::refused` signal into `arrangerdrop::reportRefusedInstrument`, `FileBrowser.cpp`,
  `instrument/InstrumentTrackWindow.cpp`, `PresetPreviewPlayHandle.cpp`).
- `1bf95db103` - `test(l10): prove a refused Carla Rack load changes nothing`: inverted `CarlaRackNoRuntimeTest`, the two
  fixtures that relied on the old dummy fallback, `ScriptBindingsTest`'s replacement case, and the ledger entries.
- this report commit - manual and feature record.

## Acceptance

| Criterion | Evidence (command, exit) |
|---|---|
| 1. Repeat the exact Carla Rack/VST3 load sequence under a crash trace, or record a bounded not-reproduced result | **Reproduced (round 1, unchanged product path)**: the first step of the sequence - putting Carla Rack on a track so a plug-in can be loaded inside it - killed the shipped binary over its control socket with `signal=SIGSEGV(11) fault_addr=0x48` inside `CarlaInstrument::CarlaInstrument`, and the test reproduced the same frame with the product hunks reversed. **Round 2 re-ran the whole test with the seam reverted**: `git apply -R logs/red-product.diff`, rebuild `zene CarlaRackNoRuntimeTest carlarack carlapatchbay tripleoscillator` -> **exit 0** (`logs/build-red-7.log`), then `ctest -R '^CarlaRackNoRuntimeTest$' --output-on-failure` -> **exit 8**, `Totals: 2 passed, 4 failed` (`logs/test-red-8.log`): `pluginLoadRefusalKeepsTheInstrument` (line 272), `theDeviceCardRefusalKeepsTheInstrument` (324), `carlaPatchbayRefusalKeepsTheInstrument` (353) and `aSavedCarlaRackProjectKeepsItsPlaceholder` (399), with the reverted code reporting `The plugin "dummy" wasn't found ...` where the track should still hold `tripleoscillator`. Restored (`git apply logs/red-product.diff`), rebuilt -> **exit 0** (`logs/build-green-9.log`), `ctest -R '^CarlaRackNoRuntimeTest$'` -> **exit 0**, 5 cases pass (`logs/test-green-10.log`). **Bounded, not reproduced**: the step *inside* Carla (opening Carla's window, loading the VST3 in a real Rack) cannot run here - no Carla runtime exists in this build (`pkg-config carla-native-plugin` finds nothing), building one is `tools/build-carla-runtime.sh`, and Carla's Rack adds plug-ins through its own native window that a headless QtTest cannot drive (feature record, Known limits). |
| 2. If reproduced, fix the host path or route VST3 to the native host, and add a regression test | Host path fixed at both places the null descriptor and the destroyed instrument entered: the two modules decline with a typed reason that names the missing runtime and points a VST3 at Zene's own VST3 host, and `loadInstrument` no longer destroys the instrument before it knows the replacement exists. Regression test proven both ways: **exit 8 without the product hunks, exit 0 with them** (`logs/test-red-8.log`, `logs/test-green-10.log`). The live socket probe agrees: refused `plugin.load` of carlarack on a TripleOscillator track whose parameter was moved - track still `TripleOscillator`, parameter still the moved value, one report of the reason, shell screenshot rendered (`logs/gui-refusal-1.log`, exit 0; `logs/gui-refusal.png`, sha256 `80b1decf5c1cbaa9144994f51d7ba2ab54e70d9edd054758798c82e9b214568b`). |

Commands, from the worktree and `build/tests`, `QT_QPA_PLATFORM=offscreen`, `LMMS_PLUGIN_DIR` unset, usual `LD_LIBRARY_PATH`:

```
git diff -- include src > logs/red-product.diff && git apply -R logs/red-product.diff
cmake --build build --target zene CarlaRackNoRuntimeTest carlarack carlapatchbay tripleoscillator -- -j4   # 0  logs/build-red-7.log
ctest -R '^CarlaRackNoRuntimeTest$' --output-on-failure                                                    # 8  logs/test-red-8.log
git apply logs/red-product.diff
cmake --build build --target zene CarlaRackNoRuntimeTest carlarack carlapatchbay tripleoscillator -- -j4   # 0  logs/build-green-9.log
ctest -R '^CarlaRackNoRuntimeTest$' --output-on-failure                                                    # 0  logs/test-green-10.log

# neighbours of the seam, and the metadata gates
cmake --build build --target MissingDevicesPreserveTest MissingDevicesRound2Test InstrumentLayersCommandsTest ReversibilityUndoTest ReversibilityUndoDeviceTest InstrumentTrackRenderPathTest ArrangerFileDropTest CarlaPlayImplTest ScriptEngineTest HybridTrackInstrumentRoleTest ProjectStateTest PartialLoadTest LegacyVst2ProjectTest -- -j4   # 0  logs/build-5-neighbours.log
ctest -R '^(MissingDevicesPreserveTest|MissingDevicesRound2Test|InstrumentLayersCommandsTest|ReversibilityUndoTest|ReversibilityUndoDeviceTest|InstrumentTrackRenderPathTest|ArrangerFileDropTest|CarlaPlayImplTest|ScriptEngineTest|HybridTrackInstrumentRoleTest|ProjectStateTest|PartialLoadTest|LegacyVst2ProjectTest|ScriptBindingsTest|CarlaRackNoRuntimeTest)$' --output-on-failure   # 0, 15/15  logs/test-green-4-neighbours.log
ctest -R '^(ControlCommandsSnapshot|ReversibilityContractTest|PluginFormatsDocsGate)$' --output-on-failure     # 0, 3/3  logs/test-green-13-metadata.log
python3 tests/doc-figures-gate.py                                                                             # 0, PASS  logs/gate-doc-figures-1.log
bash tests/fork-sources-gate.sh                                                                               # 0, PASS  logs/gate9-fork-sources-2.log
bash tests/no-upstream-regression-gate.sh                                                                     # 0, PASS  logs/gate6-upstream-2.log
bash tests/all-sources-reproduce.sh ; tests/scripted/check-namespace ; bash tests/no-tautology-gate.sh        # 0 / 0 / 0
bash tests/file-length-gate.sh --check                                                                        # 0, no regressions
```

## What round 2 changed, and why it is the whole finding

1. **The seam.** `InstrumentTrack::loadInstrument` now instantiates the requested module with the track as parent first and
   judges it the way `ControlCommandsPlugin.cpp:232` and `ControlCommandsAudioSlice.cpp:227` already read a refusal (no
   descriptor, a descriptor name other than the one requested, or a non-empty `unavailableReason()`). A refusal deletes the
   candidate and returns `nullptr` without silencing notes, changing the model, clearing `m_preservedInstrument` or
   creating an undo step; only a real device is adopted, through the original sequence. A track with no instrument at all
   keeps the `DummyInstrument` placeholder, which is what a project naming an unavailable device already opens with. The two
   module guards from round 1 stay - they are what stops the SIGSEGV during the trial.
2. **The callers.** `InstrumentLoaderThread::run` (arranger drop) null-guards the load and emits the new
   `refused(plugin, reason)`; `dropEvent` reports it and takes back the empty track it created; the file-drop branch does
   the same inline; `FileBrowser::handleFile` and `InstrumentTrackWindow::dropEvent` report the refusal instead of
   dereferencing a null descriptor; `PresetPreviewPlayHandle` tolerates a placeholder. The five call sites that ignore
   the result (`ControlCommandsClipImportFile.cpp:247`, `ControlCommandsDocuments.cpp:116`,
   `ControlCommandsTrackRole.cpp:342`, `ControlTrackRouting.cpp:104`, `Song.cpp:1934/1937`) never dereference it, so a
   refused load there simply leaves the new track holding the same placeholder it held before.

3. **The tests.** `CarlaRackNoRuntimeTest` is inverted, not extended: a configured `tripleoscillator` must survive a
   refused replacement over the agent path, the device card's Replace Instrument... with the real picker, and Carla
   Patchbay, with `nodeName()`, the track name, the track's parameter value and the journal count unchanged and the save
   taken afterwards canon-identical to the one before; the saved-project placeholder and its XML round-trip case is kept,
   including a `plugin.load` of carlarack onto that track.
4. **The fixtures.** Two inherited fixtures had been passing on the fallback this unit removes. `firstLoadable("instrument")`
   now answers `firstLoadableInstrument()` (the catalogue lists `carlapatchbay` first alphabetically and cannot know it
   declines), and `InstrumentTrackRenderPathTest`'s helper asserts *both* halves of the new contract - the refusal and the
   placeholder - where its five slots asserted only a non-null return. No assertion was removed or relaxed.
5. **Docs.** `data/manual/index.html` states the two behaviours separately (a live refusal preserves; opening a saved
   project uses the placeholder and keeps its XML), and `docs/050-features/Q050-359.md` drops the Known-limits sentence
   that declared the loss out of scope.

## Self-check

**Each new test FAILS with the product change reverted, then PASSES restored.** Product hunks (`git diff -- include src`,
280 lines, `logs/red-product.diff`) reversed with `git apply -R`: rebuild exit **0** (`logs/build-red-7.log`),
`ctest -R '^CarlaRackNoRuntimeTest$'` exit **8**, 4 of 5 cases fail (`logs/test-red-8.log`). Restored with
`git apply logs/red-product.diff`: rebuild exit **0** (`logs/build-green-9.log`), same ctest exit **0**, 5 passed /
0 failed / 0 skipped (`logs/test-green-10.log`). The test binary was rebuilt between the runs because the seam lives in
`zene`'s library, not only in a module.

**Every acceptance criterion is proven by a test I ran.** Criterion 1: `CarlaRackNoRuntimeTest` (5 cases) - red exit 8,
green exit 0, plus the bounded not-reproduced half in the table above. Criterion 2: the same four data-loss cases, red
exit 8 / green exit 0, and the live socket probe `logs/gui-refusal-1.log` (exit 0) which shows the refusal typed, the
track still `TripleOscillator`, and the moved parameter unchanged at `0`. Neighbours: 15 of 15, exit 0
(`logs/test-green-4-neighbours.log`).

**One undo step per edit, honest classes.** No command was added or changed. `plugin.load` keeps its committed class and
its `snapshot` transaction; a refused load journals nothing at all, which every inverted case asserts by comparing
`control.transactions` / `journalledSteps()` before and after. `ReversibilityContractTest` passes (exit 0,
`logs/test-green-13-metadata.log`), so the A16 histogram line needed no change, and `ControlCommandsSnapshot` passes
(exit 0, same log) - no metadata was regenerated because none changed. `snapshot_live.py` is not in this checkout
(`research/` is absent), so `ctest -R '^ControlCommandsSnapshot$'` is the live-instance check.

**Nothing added runs on the audio thread.** The new code paths are: `InstrumentTrack::loadInstrument` (called only from an
explicit load - GUI action, drop, project file, or a `plugin.load` command; it takes the track lock exactly as before),
`InstrumentLoaderThread::run` (a worker thread created per drop), `TrackContainerView::dropEvent`,
`FileBrowser::handleFile`, `InstrumentTrackWindow::dropEvent`, `PresetPreviewPlayHandle`'s constructor (a preview note setup)
and `arrangerdrop::reportRefusedInstrument`. None of them is on the render path; `InstrumentTrack::play`,
`playImpl`, `processOutEvent` and `CarlaInstrument::playImpl` are untouched by the diff, and no allocation, lock, wait,
I/O or signal was added to them. Local TSan does not run on this box, so this is a design argument, not a measured one.

**Old projects load and render as before; new data round-trips.** A project that names Carla Rack opens with the
placeholder and its own `<instrument name="carlarack">` element, which saves back canon-identical before the open, after
open + save, and after a refused `plugin.load` (`aSavedCarlaRackProjectKeepsItsPlaceholder`, green). Old-project paths were
re-run: `LegacyVst2ProjectTest`, `PartialLoadTest`, `ProjectStateTest`, `MissingDevicesPreserveTest`,
`MissingDevicesRound2Test` - all pass in the 15/15 run. No format changed.

**Completeness: the real user path and the agent path.** Agent path: `plugin.load` of `carlarack`/`carlapatchbay` on a
track that holds a configured instrument - refused, typed, reason quoted (`logs/gui-refusal-1.log`). GUI path: the device
card's Replace Instrument... opens the real picker, picks the real `carlarack` row, and the same registered command refuses
(`theDeviceCardRefusalKeepsTheInstrument`). Edge cases tried: a **track with no instrument yet** (placeholder installed,
`plugin.load` refused, `InstrumentTrackRenderPathTest`'s five slots green), a **saved project naming Carla Rack** (XML
round-trip, and a refused load onto that very track), the **drop path** (the loader thread reports instead of dropping the
track silently), a **file dropped on a track whose instrument declined** (browser and instrument window report instead of
dereferencing), and the **journal** (no step for a refusal).

**The GUI test drives the visible control through the same registered command.** `theDeviceCardRefusalKeepsTheInstrument`
finds the instrument's device card, triggers its Replace Instrument... action, drives the real picker (asserts the row reads
`Carla Rack`, selects that row, accepts), and then checks the model, the journal count and the saved XML - the picker calls
`plugin.load`, so the same `ControlRegistry` command runs that the agent calls; the test does not invoke the handler itself.
Screenshot evidence for the `src/gui` change: `logs/gui-refusal.png`, 1600x1000, sha256
`80b1decf5c1cbaa9144994f51d7ba2ab54e70d9edd054758798c82e9b214568b`, taken with `window.screenshot` after the refused load
(`logs/gui-refusal-1.log`).

**Qt 5.15 / MSVC / macOS < 10.15 portability.** The diff adds plain `nullptr` comparisons, `QStringLiteral`, one signal and
one `connect` with a lambda (C++11, no Qt-6-only overload), and `QString::fromUtf8`; no `std::filesystem`, no POSIX-only
call, no identifier from the reserved list (`small`, `near`, `far`, `interface`, `min`, `max`). The test drives the widget
tree through actions and the picker, not through Qt-6-only APIs.

**Gates.** `feature_check.py` exits 1 in this lane, and only there: its six SIMPLE gates run and pass, then
`base_listings()` raises
`bunny-guard: 'git -C .../gate-baselines/tree checkout -q --detach 9913d36d5397025d1b7092097f50f6d50c652dc4' is not allowed
in a Bunny lane (AGENTS.md rules 1, 2, 11, 12)` before it can print a table (`CalledProcessError: ... exit status 97`),
because the cached baseline for that merge-base is absent and creating it needs the denied checkout. So the two ratchets
get no comparison (round 1's reviewer saw the same). `complexity-gate.sh --check` cannot run here either
(`complexity-gate: lizard is not installed`, exit 2); `file-length-gate.sh --check` runs and reports no regression in my
files (exit 0). Every gate that can run passes:
fork-sources 0, all-sources-reproduce 0 (REPRODUCES), no-upstream-regression 0, check-namespace 0, no-tautology 0,
doc-figures 0, file-length 0 with no regression in my files.

**Commits.** `2e56348fb6` (product), `1bf95db103` (tests + ledger), the report commit (manual, feature record, this file).
