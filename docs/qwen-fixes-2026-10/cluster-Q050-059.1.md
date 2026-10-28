IMPLEMENTED

# Q050-059.1 - Modular instrument graph and starter modules, stage 1/3: playable modular instrument core

Branch `feat/050-Q050-059.1-oc`, cut from `050/m0-train-21` (`37eaf0755`). Feature record (modules, format, real-time
design, known limits): `docs/050-features/Q050-059.1.md`. Logs: round 1 in
`~/.local/share/zene-bunny-fix/qwen/runs/Q050-059.1.r1/logs/`, round 2 in `.../Q050-059.1.r2/logs/` (file names in brackets,
`r2:` for round 2).

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
| agent | plugin.list / plugin.load / plugin.param_get / param_set / undo | `ModularSynthTest::theInstrumentIsReachableThroughThePluginCommands` | 8 [r2:test-4] | 0 [r2:test-6] |
| RT | Render path allocation-free | `ModularSynthTest::theRenderPathAllocatesNothing` (4 notes, whole life, 0 allocations / 0 frees) | 8 [r2:test-4] | 0 [r2:test-6]; sensitivity: an allocation planted in `playNoteImpl` fails it with 164 [test-3-mutation] |

Case counts (round 2, current tests): reverted `ModularSynthTest` 2 passed / 16 failed, `ModularSynthViewTest` 2 passed /
1 failed (the passes are initTestCase/cleanupTestCase); with the change 18/18 and 3/3. Round 1's run gave the same counts
on the round-1 tests [test-17..20].

## Self-check

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
