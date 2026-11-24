IMPLEMENTED

# Q050-367 - VST2 editor teardown stability (member `R-effects-5`, area L10)

Removing a hosted device while its own editor is open no longer destroys the device with the editor still attached to
it: every removal path ends in `EffectChain::removeEffect()`, which now closes and detaches the device's editor while
the device is still on its chain, and `EffectChain::clear()` does the same before it deletes the devices. The legacy
VST2 host's dialog hides the plug-in's editor before it detaches the embedded widget.

**The crash itself was not reproduced, and this report does not claim it was.** No VST2 host exists in this tree or on
this box: the Carla runtime the product hosts VST2 effects in is absent (`build/carla-runtime/lib` does not exist),
and the legacy VeSTige host's `RemoteVstPlugin64`/`NativeLinuxRemoteVstPlugin64` external executables are not built
and would need a display for their X11 embedding. The VST2 fixture (`tests/data/vst2-test-effect`, built into
`build/tests/data/vst2-test-effect/native/ZeneVst2TestEffect.so`) and its hosting test are present, and every hosted
case of that test - including `theEffectsOwnEditorOpensThroughCarla`, the case that opens the editor under the
fixture - reports **Skipped** with that reason. Acceptance criterion 1 explicitly allows this bounded result; it is
recorded with the commands, the log and the exit codes below.

What *was* reproduced is the teardown-order defect the finding named: a device destroyed with its own editor still
open. It is reproducible here without any plug-in host, through the editor contract every format implements
(`Plugin::openNativeEditor` / `closeNativeEditor` / `nativeEditorOpen` - VST2 through Carla or the legacy in-dialog
host, VST3, CLAP, an AU), because the plug-in behind an editor is destroyed with the effect. The fix closes it at the
one choke point, and the regression test drives the real removal paths.

## What changed

- `src/core/EffectChain.cpp` - `removeEffect()`: `_effect->closeNativeEditor()` once the effect is found, **before**
  `requestChangeInModel()` and before the erase, so it runs while the effect is still alive and still on its chain,
  and outside the engine's change guard (a plug-in's UI call never holds off the render thread). `clear()`: the same
  call for every device before any of them is deleted (a track or channel going, a preset replacing what the chain
  carries, the project closing).
- `plugins/VstEffect/VstEffectControlDialog.cpp` - destructor: `m_plugin->hideUI()` (guarded by `hasEditor()`) before
  `removeWidget`/`setParent(nullptr)`, because `VstPlugin::~VstPlugin()` deletes the widget and the plug-in behind it.
  This is the legacy VST2 host, which embeds the plug-in's window in this dialog.
- `tests/src/gui/EffectEditorTeardownTest.cpp` (new, registered in `tests/CMakeLists.txt`, `tests/fork-sources.txt`,
  `tests/all-sources.txt`, `tests/upstream-modifications.txt`): the regression.

No command, no saved format and no undo step changed (see Self-check).

## Acceptance

| Acceptance criterion | Test / command | What it proves | Exit |
| --- | --- | --- | --- |
| 1. Reproduce removal with an open editor under the available VST2 fixture, **or** record a bounded not-reproduced result. | `cd build/tests && ctest -R '^CarlaVst2EffectHostingTest$' --output-on-failure` (`test-03-vst2-fixture.log`), and the same binary run directly (`test-04-vst2-skips.log`) | The fixture test builds and runs, but no VST2 host is installed in this tree: 2 passed, 0 failed, **12 skipped**, every hosted case skipping with "no Carla runtime at `<worktree>/build/carla-runtime/lib`: build one with tools/build-carla-runtime.sh". `theEffectsOwnEditorOpensThroughCarla` is among the skipped. So the crash cannot be driven with the fixture here: fixture present, host absent - the bounded not-reproduced result criterion 1 allows. The ordering defect behind it is reproduced separately (row 2, red runs). | ctest 0 (12 skipped); binary 0 |
| 2. If reproduced, close and detach the editor before destroying the effect, and add a regression test. | `cd build/tests && ctest -R '^EffectEditorTeardownTest$' --output-on-failure` (`test-02-green.log`, `test-06-green-restored.log`) | `theRacksRemoveEntryClosesTheEditorBeforeTheDeviceGoes`: a `QContextMenuEvent` on the device's view opens the rack's real context menu, its `Remove this plugin` `QAction` is triggered, and the record is `open,close,destroyed-closed` with the editor closed **while the device was still on its chain** (`g_chainAtClose == chain`). `theAgentsUnloadClosesTheEditorBeforeTheDeviceGoes`: the same through `plugin.unload` on the registry. `aClearedChainClosesTheEditorBeforeTheDeviceGoes`: `EffectChain::clear()`. `repeatedRemovalWithAnOpenEditorStaysStable`: three removals in a row, events run between them. `aDeviceWithoutAnOpenEditorIsRemovedAsBefore`: a device whose editor was never opened is told nothing and is removed as before. | 0 (1/1; binary run: 7 passed, 0 failed) |

Negative control (criterion 2's "fails without the change"): the two product hunks reversed with
`git diff HEAD^ HEAD -- src/core/EffectChain.cpp plugins/VstEffect/VstEffectControlDialog.cpp | git apply -R`, the test
rebuilt (`build-10-revert.log`, BUILD_EXIT=0) and run (`test-05-red-reverted.log`): **exit 8**, 3 passed / 4 failed,
each with "the device was destroyed with its editor still open: open,destroyed-open" - `theRacksRemoveEntry...`,
`theAgentsUnload...`, `aClearedChain...`, `repeatedRemovalWithAnOpenEditor...`. Restored with
`git checkout HEAD -- <files>` and rebuilt (`build-11-restore.log`, BUILD_EXIT=0), the same ctest exits **0** (`test-06-green-restored.log`), and the binary run directly reports
`Totals: 7 passed, 0 failed, 0 skipped`, exit 0 (`test-09-final-direct.log`). The first red run, before any product code existed at all, is `test-01-red.log`:
exit 8, the same four failures.

Neighbours on the fixed tree, one command (`test-07-neighbours.log`):
`ctest -R '^(EffectEditorTeardownTest|RackDeferredLifetimeTest|EffectRackAddTest|VstEffectControlsTest|PluginNativeEditorTest|DeviceMovesUndoTest|ReversibilityContractTest|MissingDevicesPreserveTest|EffectDeviceMenuTest)$'`
- 8 of 9 pass; `DeviceMovesUndoTest` failed only because the `bassbooster` module was not built in this tree. Built it
(`build-12-bassbooster.log`, EXIT=0) and re-ran that one test (`test-08-devicemoves.log`): **exit 0**, all cases pass.

Gates: `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`gate-01-feature-check.log`):
fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity
and file-length all **PASS**, exit 0. `submit_check.py <worktree> Q050-367` before this report: the only FAILs were
this file and its Self-check section. With the report committed, the same check prints `RESULT: PASS - submit`
(`gate-03-submit-check-final.log`, exit 0): every mechanical item PASS, the one WARN being `plugins/LadspaEffect/LadspaEffect.cpp`
lines from another lane that the merge-base diff pulls in (see the realtime answer below), and every ASK answered here.

Logs are in `/home/kruzzzzy/.config/kads/zene/state/runs/Q050-367.r1/logs`.

## Self-check

**? Each new test FAILS with your product change reverted (exit code?), then PASSES restored.**
Reversed (both product files, `git apply -R`), rebuilt, run: **exit 8** - 4 of 6 cases fail on "the device was
destroyed with its editor still open: open,destroyed-open" (`test-05-red-reverted.log`; the earlier pre-fix run is
`test-01-red.log`, also exit 8). Restored (`git checkout HEAD -- <files>`), rebuilt, run: **exit 0** (`test-06-green-restored.log`); the binary run
directly prints `Totals: 7 passed, 0 failed, 0 skipped` and exits 0 (`test-09-final-direct.log`). The reversed run
printed `Totals: 3 passed, 4 failed`. Builds: `build-10-revert.log` and `build-11-restore.log`, both BUILD_EXIT=0.

**? Every acceptance criterion is proven by a test you ran (name the test and its exit code per criterion).**
Criterion 1: `CarlaVst2EffectHostingTest`, exit 0 with 12 skipped (the bounded not-reproduced result; `test-03`,
`test-04`). Criterion 2: `EffectEditorTeardownTest`, exit 0 (`test-02`, `test-06`; `Totals: 7 passed, 0 failed, 0 skipped` in
`test-09-final-direct.log`). Both are named per row in the
table above.

**? Each edit is ONE undo step, and every new or changed command has an honest reversibility class.**
No command was added or changed, so no reversibility row moved: `plugin.unload` keeps `true_inverse`
(`src/core/ControlReversibilityTableStructure.cpp:138`) and `plugin.load` keeps `snapshot`; `ReversibilityContractTest`
passes (in `test-07-neighbours.log`). Closing an editor is not an edit: `closeNativeEditor()` writes no model, pushes
no journal checkpoint and changes nothing a save writes, so a removal is still exactly the one undo step it was, and
`control.undo` of that step still rebuilds the device from its settings document (whose rebuilt instance starts with
its editor closed, as it always did).

**? Nothing you added runs on the audio thread and allocates, frees, locks, waits, does I/O or emits a signal
(FLAGGED lines: plugins/LadspaEffect/LadspaEffect.cpp ...).**
The flagged lines are **not mine**: `plugins/LadspaEffect/LadspaEffect.cpp` is untouched by this branch's two commits
(`git diff --name-only f2ec5dde2f^..HEAD` lists only my five files plus docs) - they come into the check because it
diffs against the merge-base with the train, which carries other lanes' commits. My own additions: two calls to
`_effect->closeNativeEditor()` in `EffectChain::removeEffect()`/`clear()`, placed **before** `requestChangeInModel()`
so they run outside the engine's change guard, and one `m_plugin->hideUI()` in a widget destructor. Neither
`processImpl()`, `processAudioBuffer()`, the render path nor any `RealtimeScope` was touched; `removeEffect()` and
`clear()` are GUI-thread operations that already take the model-change guard (which is why they can never be called
from a render period). `closeNativeEditor()` is a virtual that is a no-op for most formats; for the VST2 hosts it is
the same call `plugin.editor_close` and the effect's own destructor already make from the GUI thread. Local TSan does
not run on this box, so this is reasoned from the call sites, not measured.

**? Old projects still load and render exactly as before; new data round-trips through save and load.**
Nothing in loading, saving, rendering or the routing graph changed: the erased list, the graph rebuild, the enabled
flag, the latency refresh and the undo journal are byte-for-byte the code paths they were. An editor was never saved
and is not saved now, so a project round-trips unchanged - and a project that was saved with an editor open reopens
with it closed, exactly as before. `MissingDevicesPreserveTest`, `EffectDeviceMenuTest`, `EffectRackAddTest`,
`RackDeferredLifetimeTest` and `PluginNativeEditorTest` pass on the fixed tree (`test-07-neighbours.log`); the VST2
save/load/export cases of `CarlaVst2EffectHostingTest` and `CarlaVst2EffectExportTest` are untouched but skip here for
want of a runtime, which is why this claim rests on the code paths being unchanged rather than on a VST2 run.

**? Completeness: walk each criterion through the REAL user path and the agent path and say which edge case you
tried.**
User path: right-click a device's view in a track's or channel's rack -> `Remove this plugin` (driven in
`theRacksRemoveEntryClosesTheEditorBeforeTheDeviceGoes` and in the repeated-removal case, through a real
`QContextMenuEvent` and the popup's `QAction`), with the editor open. Agent path: `plugin.unload` on the registry with
`target` = `ch-<n>`, `plugin` = `fx-<n>` (`theAgentsUnloadClosesTheEditorBeforeTheDeviceGoes`). Edge cases tried: an
editor never opened (no close invented); a chain cleared under its devices (a track or channel going); three removals
in a row with the event loop run between them; the removal that happens in a *later* event-loop turn than the click
(the rack's queued removal, so the close is proven to happen in the same turn as the detach, not on the click). Not
tried, and named as a limit: a real VST2 plug-in (no host here), and a rejected/refused removal of a device that
cannot be addressed (unchanged, still refused before anything is touched).

**? The GUI test drives the visible control and checks the SAME registered command ran (undo depth +1).**
Half of this holds and half is honestly not applicable. The rack's own `Remove this plugin` entry does **not** go
through `ControlRegistry` and never has: `EffectRackView::deletePlugin()` calls `fxChain()->removeEffect(e)` and
`e->deleteLater()` directly (upstream behaviour, `src/gui/EffectRackView.cpp:147-155`), so there is no registry call
to check and no undo step to count for it - routing it through `plugin.unload` would change the rack's undo semantics
(a new undo step per rack removal), which is a separate change this fix does not make. What the two paths **do** share
is the code the fix lives in: both end in `EffectChain::removeEffect()`, which is why one hunk covers both, and the
agent half is driven through the registered command (`plugin.unload`) in the same test binary. The GUI half does drive
the visible control - a context-menu event on the widget and the popup's `QAction::trigger()`, not a handler call or
a direct registry call.

**? Qt 5.15 / MSVC / macOS < 10.15 portability: no Qt-6-only API without a guard, no windows.h macro names, no
std::filesystem outside _WIN32.**
No Qt-6-only API: `QContextMenuEvent`, `QAction`, `QMenu`, `QTimer::singleShot`, `QCoreApplication::sendPostedEvents`
and `QApplication::activePopupWidget` all exist in Qt 5.15; no `small`/`near`/`far`/`interface`/`min`/`max`
identifiers; no `std::filesystem`; no POSIX-only call. The product hunks are plain member calls on classes compiled in
every configuration. This box builds Qt 6 only, so a Qt 5.15 / MSVC compile is reasoned, not run.

## Known limits

- The crash is not reproduced (no VST2 host in this tree); the claim is bounded to the ordering defect the finding
  named, reproduced and fixed, and the VST2 fixture test that would show the crash cannot run here.
- The legacy-host half (`VstEffectControlDialog`'s destructor) has no test here for that same reason: it needs a
  loaded legacy VST2 plug-in. It is guarded by `hasEditor()` and only calls the `hideUI()` the dialog's own button
  calls. It compiles as part of `VstEffectControlsTest`, which passes.
- Moving a device between chains closes its editor, because a move is a remove plus an append. That is the honest
  consequence of one choke point; the editor button reopens it.
- No screenshot: this fix has no visible interface state of its own (the editor simply closes), and the legacy VST2
  dialog cannot be populated in this environment for want of a host.
