IMPLEMENTED

# Q050-055.2 - Context-aware shortcuts and plug-in actions, stage 2/2: Keyboard plug-in actions

Worktree `zene-feat-q050-055.2`, branch `feat/050-Q050-055.2-oc`, cut from `050/m0-train-24` (`2a7fd9f9c7`). Design, commands
and known limits: `docs/050-features/Q050-055.2.md`.

## Acceptance

| # | criterion | command (from `build/tests`) | exit |
| --- | --- | --- | --- |
| 1 | Selected-track editor key acts on the selected device, no other track | `ctest -R '^PluginShortcutsTest$' --output-on-failure` (`theEditorKeyOpensTheSelectedTracksDevice`) | 0 |
| 2 | Bound plug-in key loads onto the selected compatible target, with `plugin.load` result and undo | same test (`aBoundEffectLoadsOntoTheSelectionWithOneUndoStep`, `aBoundInstrumentReplacesTheSelectedTracksAndIsReportedIrreversible`) | 0 |
| 2 | Same over the socket, across a real restart | `ctest -R '^ControlPluginShortcuts$' --output-on-failure` | 0 |

Neighbours on the final tree, each exit 0: `PanelShortcutsTest`, `ShortcutsCommandsTest`, `ShortcutsPageTest`, `CommandFormTest`,
`RackMenuTest`, `DevicePanelTest`, `LibraryMenusTest`, `ControlDevices`, `PluginNativeEditorTest`, `ControlCommandsSnapshot`,
`ReversibilityContractTest`. The whole suite was not run (the brief says targeted only).

## Not proven / limits

- A hosted plug-in's native window through these keys is NOT exercised: the offscreen test platform cannot embed one. The key
  runs the unchanged `plugin.editor_open` first; what was proven is the fallback to the host's view.
- The transcript's instance is headless, so there `plugin.selected_editor_open` is proven only to refuse `requires`; the opening is
  proven by the GUI test.
- TSan does not run on this box. Reasoning: all three commands and the router run on the GUI thread through `ControlRegistry`;
  no new shared state, nothing on an audio path.

## Environment notes (write-jail)

- `ccache.conf` sets `temporary_dir` outside the jail, so every compile failed. Builds here set `CCACHE_TEMPDIR=/tmp/q0552-ccache-tmp`;
  the shared config was not edited.
- `feature_check.py` writes logs to a sibling of the worktree, which the jail refuses (`PermissionError`). I ran its own gate
  definitions and cached base listing from a small driver with logs in the run folder; the tool itself did not run to completion
  here, and the check the loop runs after this session has not been seen.
- `submit_check.py` here ends `RESULT: FAIL - 1 item`: its one FAIL is the `gates (feature_check.py)` row, because that tool cannot create
  its log folder from inside the jail. Every other row passes. Expect that row to need re-reading if the loop runs it under the same jail.
- One early `git add -A` was refused by the guard, so commit `ff80488771` holds only the new files (not yet wired into CMake, so it builds).

## Self-check

- **Each new test FAILS with the product change reverted, then PASSES restored.** `git diff 2a7fd9f9c7 HEAD -- include src`, reversed
  with `git apply -R`, rebuilt (build exit 0): `ctest -R '^(PluginShortcutsTest|ControlPluginShortcuts)$'` exit 8, both failed
  (logs `build-21-red.log`, `test-19-red.log`). Restored and rebuilt: exit 0 for both (`test-20-green.log`). Mutant aiming at the song's
  first track instead of the selection: `PluginShortcutsTest` exit 8 (`test-16-mut.log`).
- **Every acceptance criterion is proven by a test I ran.** See the table: `PluginShortcutsTest` exit 0, `ControlPluginShortcuts` exit 0.
- **One undo step per edit; honest classes.** Effect load: one `control.undo` restores the chain, one `control.redo` restores it
  again (test). Instrument replacement: recorded `reversible:false`, undo depth unchanged (test), as `plugin.load` already was. Bind:
  one undo removes the row, redo returns it (test). `plugin.selected_editor_open` is `not_mutating`; `plugin.selected_load` and
  `shortcuts.bind_plugin` are `not_mutating` wrappers whose nested command records itself (the `arrangement.nudge` pattern; the brief
  suggested `snapshot`, and the nested `plugin.load` keeps that class). `ReversibilityContractTest` exit 0 with the A16 block at
  `rows=890 ... not_mutating=389`.
- **Audio thread.** Nothing added runs there: key routing, config writes, selection reads, plug-in loading and editor windows are all
  GUI-thread (`ControlRegistry::invoke` dispatches to the UI thread). `plugin.load`'s own engine handling is unchanged.
- **Old projects.** No project format change and no project code touched; the binding is in `ui/shortcuts` config, whose existing
  form loads unchanged. Round trip of the new data: config re-read (GUI test), real restart (transcript, exit 0), `shortcuts.import`
  of the map reports nothing unknown (GUI test). No render comparison was run, since no audio path changed.
- **Portability.** No Qt-6-only API added: `std::optional` (C++17), `QKeySequence` handling unchanged, `Action{...}` spelled out so no
  `QList::append` overload ambiguity under Qt 5, no `std::filesystem`, no windows.h macro names as identifiers. Only built here on Qt 6.12;
  the Qt 5.15, MSVC and macOS builds were not run.
- **Gates** (driver above, `gates-3.log`): fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology,
  doc-figures exit 0; complexity and file-length report no new item in my files. Snapshot regenerated live: 881 ids / 77 groups.
