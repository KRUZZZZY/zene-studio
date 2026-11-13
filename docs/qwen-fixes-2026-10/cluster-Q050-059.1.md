IMPLEMENTED

# Q050-059.1 - Modular instrument graph and starter modules, stage 1/3: playable modular instrument core

Branch `feat/050-Q050-059.1-oc`, cut from `050/m0-train-21` (`37eaf0755`); train 26 not merged (round 3, item 1). Feature record (modules, format, real-time
design, known limits): `docs/050-features/Q050-059.1.md`. Logs: round 1 in
`~/.local/share/zene-bunny-fix/qwen/runs/Q050-059.1.r1/logs/`, round 2 in `.../Q050-059.1.r2/logs/` (file names in brackets,
`r2:` for round 2).

## Round 3 (review round 2: knob edits bypassed plugin.param_set without an agent socket)

Logs for this round: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-059.1.r3/logs/` (`r3:NN-*`).

1. **Merge of `050/m0-train-26`: not done, because the lane's guard refuses it.** `git merge 050/m0-train-26` returned
   `bunny-guard: 'git merge 050/m0-train-26' is not allowed in a Bunny lane (AGENTS.md rules 1, 2, 11, 12)`
   (`~/.local/share/zene-bunny-fix/bunny/shims/git`). I did not get around the guard; the Q050-062.3 lane handled the same
   refusal this way. Instead I removed the conflicts I could. Commit `fb1c433f8` moves this unit's `tests/fork-sources.txt` block
   and its `tests/upstream-modifications.txt` rows (the `PluginList.cmake` reason is now its own row, as the train's
   Q050-491 row is) away from the places where train 26 inserts its own lines. Afterwards
   `git merge-tree --write-tree --name-only HEAD 050/m0-train-26` lists only **`tests/all-sources.txt`** as conflicting. That
   file is derived and sorted: this branch adds `tests/src/gui/ModularSynthViewTest.cpp` right after
   `MixerWindowsTest.cpp`, and the train adds `MixerZonesTest.cpp` at the same place. Re-deriving it with the recipe in its
   header after the merge resolves it, and the merge train re-derives it anyway. The registration files merge cleanly.
2. **Root cause, and the fix.** Every engine command run through `ControlRegistry::invoke` is gated on `s_ready`. That flag
   is set only when the program starts with `--control-socket` (`src/core/main.cpp`, the `if( controlServer )` block after
   startup). Round 2's knob used `invoke` and, on any failure, fell back to a direct journalled write. So without an agent
   socket, every knob turn skipped the command. The fix (`30ab6dead`, plus the length fix below):
   - `ControlRegistry::invokeInProcess(id, args)` (`include/ControlRegistry.h`) is `invoke` with one difference: an
     engine command needs only the engine to exist (`Engine::getSong()`/`mixer()`), not `s_ready`. Both functions run the
     same private `dispatch(id, args, inProcess)`, which is the old `invoke` body: lookup, `requires`, schema validation,
     the engine gate, UI-thread hop, collaboration tap, edit locks, journal bracket, coalescing and transaction record.
     `invoke`'s behaviour is unchanged (`ControlRegistryTest`, which covers the `busy`-before-ready refusal, and
     `ReversibilityContractTest` pass).
   - `ModularSynthView::dispatch` calls `invokeInProcess("plugin.param_set", ...)`. **The fallback is removed.** A refusal is
     announced over the window (`announce`, the shared GUI refusal display) and the knob snaps back to the parameter's
     value. The model is not touched.
   - No control command was added or changed. `invokeInProcess` is a C++ entry point and not a registered id, so there is
     no snapshot or reversibility change.
3. **New regressions** in `ModularSynthViewTest`. Both drive the knob with real `QWheelEvent`s:
   - `aKnobTurnWithNoAgentSocketIsStillOneUndoablePluginParamSet`: `setReady(false)` (asserting `!isEngineReady()`), then 5
     wheel steps on `osc1.coarse`. The parameter rises, `control.transactions` count +1, one `control.undo` reports
     `undone_command: "plugin.param_set"`, and the parameter and knob return to 0.
   - `aRefusedKnobTurnChangesNothing`: a collaboration interceptor (`collab::CommandTap::install`, the hook a joined peer
     uses) refuses `plugin.param_set`, then 3 wheel steps. The interceptor saw the command, the parameter and knob stay 0,
     and the transaction count and `control.undo_depth` are unchanged.
   - **Revert proof.** I put the round-2 `ModularSynthView.cpp` (invoke + fallback) back from `HEAD` and rebuilt
     `modularsynth ModularSynthViewTest` (BUILD=0, `r3:05`). `ctest -R '^ModularSynthViewTest$' -V` then gave **EXIT=8**, 3
     passed and 2 failed (`r3:06`). The no-socket case failed at `transactionCount()` 2 vs expected 3. The refusal case
     failed because the parameter was 3, not 0, so the fallback wrote through a refusal. With the fix restored and rebuilt
     (BUILD=0, `r3:07`), both tests gave **EXIT=0**: ModularSynthTest 18/18 and ModularSynthViewTest 5/5 (`r3:08`).
4. **File length.** The first gate run flagged `src/core/ControlRegistry.cpp` at 509 code lines (limit 500,
   `r3:13`). The helper became a file-static one-liner, `invoke` became one line and `invokeInProcess` moved inline into
   the header. After that the file is no longer listed (`r3:14`). Final build and tests: see the self-check.
5. **Docs.** The manual's Modular Synth paragraph now says a knob is the same command with or without an agent socket and
   that a refused edit leaves the knob where it was. The feature record's design note and known limits drop the
   fallback. They add one limit: the rest of the interface still dispatches through `invoke`, so it answers `busy`
   without an agent socket. That wider behaviour is outside this unit.

**Limits (checkpoint's exhaustion row).** `ModularSynthTest` now has 2 more `aDamagedGraphFallsBackToTheStarterGraph` rows,
"17 modules" and "65 wires": they fall back to the starter graph, report "17 modules, more than 16" or "65 wires, more than
64", and play. A new `aGraphAtTheSizeLimitsLoads` covers "16 modules" and "16 modules, 64 wires": each loads as written, with
no problem reported, its wire count kept, and a finite, sounding note (`7cfac5e27`). These cases pin existing graph
validation and do not test changed behaviour, so their negative control is the whole-module revert from round 2. Final run:
build EXIT=0 (`r3:17`), `ctest -R '^ModularSynth(View)?Test$' -V` EXIT=0, ModularSynthTest 22/22 and
ModularSynthViewTest 5/5 (`r3:18`). Neighbours after the core change: ControlRegistryTest 28/28 and
ReversibilityContractTest 9/9, EXIT=0 (`r3:15`, `r3:16`). Not re-run separately, but passing inside the 22/22: the eight
original damaged-graph rows, the allocation test and the graph-replaced-under-a-note test. The round-2 allocation mutation
evidence still stands, because the render path did not change. Portability beyond Linux/GCC/Qt 6 was not built;
`invokeInProcess` uses no new API. TSan does not run on this box. The new path runs
on the GUI thread only (`dispatch` still hops to the UI thread via `runOnUiThread`), so it adds no new cross-thread access.

## Round 2 (review round 1: REWORK, "GUI parameter edits bypass plugin.param_set")

Fixed exactly that. The window's knobs no longer bind the parameter models. Each knob shows a stand-in `FloatModel` owned by
the window (not a child of the instrument, not journalled); what the knob does to it is dispatched as `plugin.param_set`
`{target, plugin: "inst", name, value}` through `ControlRegistry::invoke` (`ModularSynthView::dispatch`), the handler an agent
reaches - same range check, journal checkpoint and transaction. The command's coalescing key (`target,plugin,name,index`)
makes a drag or a run of wheel steps on one parameter one undo step. The parameter's own changes (agent, automation, undo,
load) are copied back into the stand-in (`follow`). The knobs' automation context menu and Ctrl-drag are switched off
(`CommandKnob`), since they would act on the stand-in; automation goes through the automation editor and commands
(documented in the manual and known limits). If the surface cannot address the instrument (rack layer, surface not up),
the knob falls back to a plain journalled edit.

`ModularSynthViewTest` now: 12 wheel steps on `osc1.coarse` -> `plugin.param_get` reads the new value and the pitch is heard
-> ONE `control.undo` returns `undone_command: "plugin.param_set"` and puts the value and the knob back to 0 -> an agent's
`plugin.param_set` -7 moves the knob -> the instrument still has 16 parameters (the stand-ins are not parameters).
Revert proof for this change: the committed direct-binding `ModularSynthView.{h,cpp}` put back with `git show HEAD:...`,
module and test rebuilt (EXIT=0) [r2:build-2-revert]: `ctest -R '^ModularSynthViewTest$' -V` EXIT=8, the undo returned
`undone_command` "" instead of `plugin.param_set` [r2:test-2-revert-view]; restored and rebuilt [r2:build-3-restore]: both
tests EXIT=0, 18/18 and 3/3 [r2:test-3-restored]. Commit `89ac7eff6`.

**Change.** A new instrument module, `modularsynth` ("Modular Synth", `plugins/ModularSynth/`): a graph of oscillator,
envelope, filter, LFO (modulation) and output modules, compiled on the control thread into fixed arrays and rendered per
note from the `NotePlayHandle`'s inline storage. A new instrument holds the starter graph (osc -> filter -> output, envelope
on the level, LFO on the cutoff). Its state saves `<graph>` with modules, wires and each module's parameters; a state
without `<graph>` loads the starter graph, a damaged one falls back to it and is reported. Parameters are named models
(agents: `plugin.param_get`/`param_set`; automatable); the window has a knob for each and lists the wiring. No other
product code changed except the one `PluginList.cmake` entry. Graph editing is stages 2 (commands) and 3 (GUI editor).

## Acceptance

Environment: the brief's, plus `CCACHE_TEMPDIR=/tmp/ccache-tmp-q0591` (the ccache config's temp dir is outside this
session's writable area). Tests run from `build/tests` with `ctest -R '^<name>$' -V`.

| # | Criterion | Test | Reverted (exit) | With the change (exit) |
| --- | --- | --- | --- | --- |
| 1 | A note through an oscillator-to-filter-to-output graph renders non-silent audio | `ModularSynthTest::aNoteThroughTheStarterGraphSounds` (starter wiring, peak > 0.05, release to silence); `::theFilterAndTheWiringAreInTheNotesPath` (cutoff/mode change the spectrum; filter not wired to the output = exactly 0; wired = sounds) | 8 [r2:test-4-revert-all-plugin] | 0 [r2:test-6-restored-plugin] |
| 2 | Instrument state saves and restores safely | `ModularSynthTest::theStateRoundTripsModulesWiringAndParameters` (6-module graph + edited values -> state -> fresh instrument: same wiring, values, sample-identical audio); `::projectSaveAndOpenKeepTheGraph` (`project.save` / clear / `project.open`: same wiring, values, identical audio); `::aDamagedGraphFallsBackToTheStarterGraph` (8 damaged graphs -> starter graph, reason kept, plays); `::aGraphReplacedUnderASoundingNoteKeepsPlaying` | 8 [r2:test-4] | 0 [r2:test-6] |
| 2 | ... including when new graph state is absent in older projects | `ModularSynthTest::aStateWithoutAGraphLoadsTheStarterGraph` (`<modularsynth/>`); `::projectSaveAndOpenKeepTheGraph` second half (the saved `.mmp` with `<graph>` removed opens with the starter graph, no error, and plays) | 8 [r2:test-4] | 0 [r2:test-6] |
| GUI | The window edits through `plugin.param_set`, one undo step | `ModularSynthViewTest::theWindowHasAKnobForEveryParameterAndTurningOneIsHeard` (knob per parameter showing its value; wheel on `osc1.coarse` -> `plugin.param_get` reads it -> pitch rises 2^(st/12) -> one `control.undo` reports `plugin.param_set` and restores 0, knob follows; agent write moves the knob; a replaced graph rebuilds the knobs) | 8 [r2:test-5-revert-all-view]; 8 with the direct-binding view [r2:test-2-revert-view] | 0 [r2:test-7-restored-view] |
| GUI, no agent socket (r3) | A knob turn is `plugin.param_set` (one undo step) when the registry was never made ready | `ModularSynthViewTest::aKnobTurnWithNoAgentSocketIsStillOneUndoablePluginParamSet` | 8, round-2 view [r3:06] | 0 [r3:08, r3:18] |
| GUI refusal (r3) | A refused `plugin.param_set` changes nothing: no value, no undo step, no transaction | `ModularSynthViewTest::aRefusedKnobTurnChangesNothing` | 8, round-2 view [r3:06] | 0 [r3:08, r3:18] |
| limits (r3) | 16 modules / 64 wires load; 17 / 65 fall back with a reason | `ModularSynthTest::aGraphAtTheSizeLimitsLoads`, `::aDamagedGraphFallsBackToTheStarterGraph(17 modules / 65 wires)` | pins existing validation (whole-module revert, round 2) | 0 [r3:18] |
| agent | plugin.list / plugin.load / plugin.param_get / param_set / undo | `ModularSynthTest::theInstrumentIsReachableThroughThePluginCommands` | 8 [r2:test-4] | 0 [r2:test-6] |
| RT | Render path allocation-free | `ModularSynthTest::theRenderPathAllocatesNothing` (4 notes, whole life, 0 allocations / 0 frees) | 8 [r2:test-4] | 0 [r2:test-6]; sensitivity: an allocation planted in `playNoteImpl` fails it with 164 [test-3-mutation] |

Case counts (round 2, current tests): reverted `ModularSynthTest` 2 passed / 16 failed, `ModularSynthViewTest` 2 passed /
1 failed (the passes are initTestCase/cleanupTestCase); with the change 18/18 and 3/3. Round 1's run gave the same counts
on the round-1 tests [test-17..20].

## Self-check

**Round 3** (`r3:` logs in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-059.1.r3/logs/`):

- `submit_check.py ... Q050-059.1` [r3:20]: the first run had 2 FAILs. The first was "commands snapshot regenerated live".
  I fixed it by running `snapshot_live.py $PWD`, EXIT=0, which reported 871 ids / 76 groups. The diff touches only
  `captured_at`, lane, socket and version, so no command changed. It is committed as `a619c8e4e`, and
  `tests/doc-figures-gate.py` gave EXIT=0 [r3:21, r3:22]. The second was `gates (feature_check.py)`. That is the same
  sandbox `PermissionError` as round 2: the script writes `../zene-feat-q050-059.1-feature-check/`, outside the writable
  area [r3:11]. I ran a copy that differs only in its log directory (`/tmp/q0591fc`) and its `ROOT` path (the shown
  `diff`). It gave **all 8 rows PASS, EXIT=0** [r3:19], including file-length after the `ControlRegistry.cpp` trim (it had
  failed at 509 lines [r3:13]). The WARN "canary figures" lists strings from test code (`control.undo`, `modular.mmp`), not
  new command ids, so no README figure changed.
- **Each new test fails with the product change reverted.** I reverted the product hunk the round-3 tests target, the
  view's dispatch, to the round-2 `invoke`+fallback version, and rebuilt `modularsynth ModularSynthViewTest` (BUILD=0
  [r3:05]). `ctest -R '^ModularSynthViewTest$' -V` then gave **EXIT=8**: the no-socket case failed (transactions 2, not
  3) and the refusal case failed (parameter 3, not 0) [r3:06]. With the fix restored and rebuilt (BUILD=0 [r3:07]), the run
  gave **EXIT=0**, 5/5 and 18/18 [r3:08]. With the final tree: build EXIT=0 [r3:17], and both tests EXIT=0, 22/22 and 5/5
  [r3:18]. The new limit rows pin the existing graph validation and are not changed behaviour. They fail only with the
  whole module reverted (round 2).
- **Acceptance.** Criteria 1 and 2 are covered by `ModularSynthTest`, EXIT=0 with 22/22 [r3:18]; the table above names
  the cases.
- **One undo step and reversibility.** No command was added or changed; the live snapshot gives the same 871 ids.
  `invokeInProcess` runs the registered `plugin.param_set` handler (`true_inverse`) through the same `runHandler`
  bookkeeping. The no-socket test shows one `control.undo` reporting `plugin.param_set`, and the refusal test shows no
  undo step. `ControlRegistryTest` (28/28) and `ReversibilityContractTest` (9/9) give EXIT=0 [r3:16].
- **Audio thread.** Round 3 changed only GUI-thread and control code: `ModularSynthView::dispatch`,
  `ControlRegistry::invoke`/`invokeInProcess`/`dispatch`, and the file-static `engineAddressable`. The handler still runs
  through `runOnUiThread`. No render-path code changed, so the round-2 audio analysis below stands.
- **Old projects.** No loader or render change this round. `invoke` keeps its behaviour for every other caller, and
  `ControlRegistryTest` still passes, including its busy-before-ready refusal.
- **Portability.** The new code uses no Qt-6-only API, no `std::filesystem`, no POSIX call and no reserved identifier
  (`inProcess`, `engineAddressable`). `invokeInProcess` is inline in an `LMMS_EXPORT` class and calls the exported member
  `dispatch`. I built only Linux/GCC/Qt 6.
- **Train merge.** It was not performed because the guard refuses `git merge` (round 3, item 1). Only the derived
  `tests/all-sources.txt` still conflicts.

**Round 2 (kept for the record):**

`submit_check.py` (round 2, after the report commit): every item PASS except `gates (feature_check.py)`, which fails in this
session only because the script creates its log directory beside the worktree (`PermissionError: ...
zene-feat-q050-059.1-feature-check`, outside the writable area). The same script with only its log directory moved to
`/tmp` (`diff` against the original: those two lines) reports **all 8 rows PASS** on the round-2 tree [r2:gates-1.log, and
again after the docs commit, r2:gates-2.log]: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
no-tautology, doc-figures exit 0; complexity and file-length "no new item in your files" (round 1 split render,
`Graph::isValid` and `processingOrder` to CCN <= 10 after its first run flagged them [gates-2.log]).

- **Each new test fails with the product change reverted, then passes restored.** Whole change (round 2, current tests):
  `plugins/ModularSynth/` and the `ModularSynth` line of `cmake/modules/PluginList.cmake` moved out, the built
  `build/plugins/libmodularsynth.so` moved away (stale-artifact trap), `cmake --build build --target rebuild_cache` EXIT=0,
  `cmake --build build --target ModularSynthTest ModularSynthViewTest -- -j4` EXIT=0 [r2:build-4/5-revert-all];
  `ctest -R '^ModularSynthTest$' -V` EXIT=8 (16 failed), `ctest -R '^ModularSynthViewTest$' -V` EXIT=8 (1 failed)
  [r2:test-4/5-revert-all-*]. Restored, re-run, `cmake --build build --target zene modularsynth ModularSynthTest
  ModularSynthViewTest -- -j4` EXIT=0 with a freshly linked module [r2:build-6/7-restore-all]; both EXIT=0 (18/18, 3/3)
  [r2:test-6/7-restored-*]. Round-2 change alone (knobs through the command): direct-binding view put back -> GUI test
  EXIT=8 (`undone_command` "" not `plugin.param_set`) [r2:test-2-revert-view]; restored -> EXIT=0 [r2:test-3-restored].
  The tree was clean afterwards.
- **Every acceptance criterion is proven by a test I ran.** Criterion 1: `ModularSynthTest` (cases above) EXIT=0.
  Criterion 2: `ModularSynthTest` (state, project, graph-less state, damaged graphs, replacement under a note) EXIT=0.
  Table above.
- **One undo step per edit; honest reversibility classes.** No command was added or changed, so no reversibility row
  and no snapshot regeneration (the registry still has the 871 ids of the base). The agent edit path is the existing
  `plugin.param_set` (`true_inverse`): `theInstrumentIsReachableThroughThePluginCommands` sets "Output Gain" 0.2 and one
  `control.undo` restores 0.8 (EXIT=0). The GUI edit path is the same command: a knob turn dispatches `plugin.param_set`,
  and `ModularSynthViewTest` shows 12 wheel steps taken back by ONE `control.undo` that reports `plugin.param_set`
  (EXIT=0). The knob's stand-in model is not journalled, so the GUI adds no step of its own. Loading the instrument is the
  existing `plugin.load` (`snapshot`) or the instrument list.
- **Audio thread.** Functions that run there: `ModularSynth::playNoteImpl` (worker/render threads) - `std::construct_at` of
  a trivially-copyable `modular::Voice` in `NotePlayHandle::pluginDataStorage()` (no heap; `static_assert` on size and
  alignment), one atomic acquire load of the program pointer, then `modular::render` and the upstream
  `applyFadeIn`/`applyRelease`; `modular::render` and its helpers (`readParameters`, `input`, `stepOscillator`,
  `stepEnvelope`/`ramp`, `stepFilter`, `stepLfo`, `step`) - fixed `std::array`s on the stack, `<cmath>` only, parameter
  reads through `FloatModel::value()`; `ModularSynth::deleteNotePluginData` - `std::destroy_at` of the trivial voice;
  `ModularSynth::desiredReleaseTimeMs` / `modular::longestReleaseMs` (called from `NotePlayHandle::noteOff`) - an atomic load
  and model reads. None allocates, frees, locks, waits, does I/O or emits. The graph build, validation, XML, model creation
  and program compile run on the control thread (`loadSettings`, constructor); the program swap happens under
  `AudioEngine::requestChangesGuard()` (a lock the *control* thread takes; the render side takes none) and the old program
  and models are freed after it on the control thread. The window's knob dispatch (`ModularSynthView::dispatch`/`follow`)
  runs on the GUI thread only. Automation of a parameter on the render thread holds its change
  signal back (`setDeferChangeSignalOnRenderThread`) and a 100 ms timer on the instrument's thread emits it. Measured:
  `theRenderPathAllocatesNothing` 0 allocations / 0 frees over four full notes, and the planted-allocation mutation fails it
  (164) [test-3-mutation]. Races are argued, not measured: ThreadSanitizer does not run on this machine.
- **Old projects load and render as before; new data round-trips.** No loader, engine, track or other plug-in source was
  changed (`git diff 37eaf0755..HEAD --stat`: only `plugins/ModularSynth/*`, the PluginList entry, tests, registration
  manifests, manual and docs), so a project without this instrument takes exactly the code path it took before. The new
  data round-trips through state and through `project.save`/`project.open` with identical audio, and a graph-less state
  loads as the starter graph (tests above). Neighbour tests that enumerate or load instruments pass on this build:
  ControlDeviceVendorTest, PluginScanCacheTest, InstrumentLayersCommandsTest, SfzPlayerTest, InstrumentSetupUiTest
  [test-14-neighbours], ControlDeviceCatalogueTest EXIT=0 once the `patman` module it loads was built in this targeted tree
  [build-25-patman, test-15-catalogue] (its first failure was "Patman plugin is unavailable", a module never built here).
- **Portability (Qt 5.15 / MSVC / macOS < 10.15).** Only Linux/GCC/Qt 6 was built and run. By construction: no Qt-6-only
  API (QDomDocument, QString::arg, QTimer, QScrollArea/QGridLayout/QLabel, QWheelEvent's 8-argument constructor which
  existing tests use on Qt 5); no `std::filesystem`; no POSIX call; identifiers checked - the parameter-range fields are
  `lowest`/`highest`, not `min`/`max`, and no `small`/`near`/`far`/`interface`; `QString::arg` gets `std::size_t` /
  `std::ptrdiff_t`, which map to `ulong`/`qulonglong` and `long`/`qlonglong` overloads on every platform; C++20 features used
  (`std::span`, `std::construct_at`, `std::numbers`, defaulted `operator==`) are already used across the tree.

## Not done here (later stages)

Agent commands to create/inspect/wire modules and the preset (`.xpf`) round-trip are stage 2; the GUI graph editor is
stage 3. Nothing in this stage would have to be undone for them: the graph model (`modular::Graph`, string module ids,
named ports, validation) and the program publication are the pieces they would call.
