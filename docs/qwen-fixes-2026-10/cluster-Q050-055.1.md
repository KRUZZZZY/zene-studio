IMPLEMENTED

# Q050-055.1 - Context-aware shortcuts and plug-in actions, stage 1/2: Panel-scoped shortcuts

Unit Q050-055.1 (completion, L, P1, area L13). Stage 1 only: the arranger, the note editor, the
automation editor and Perform each get a shortcut context of their own, with scoped conflict
resolution and restart persistence. Stage 2 (keys bound to plug-in actions) is NOT started and
nothing here has to be undone for it.

Worktree `zene-feat-q050-055.1`, branch `feat/050-Q050-055.1-oc`, cut from `050/m0-train-12`
(base `07d6d02bd`).

## Integration onto `050/m0-train-17` (round 3, 2026-10-27)

The accepted patch (`integration/Q050-055.1-cf3618e61a` @ `1ded8aa57`) was applied by the driver's
three-way merge onto `050/m0-train-17` @ `639e1d23e` on branch `integration/Q050-055.1-639e1d23e4`.
Nine files conflicted. One was product code; the rest were ledgers and measured figures. The
accepted design and its regression tests are kept unchanged.

| file | conflict | resolution |
| --- | --- | --- |
| `src/gui/ControlCommandsShortcuts.cpp` | train 17 added Q050-202's `shortcuts.preset_list/preset_apply` (backup file, `storeOverrides`, a menu-only export document) next to the place where the patch rewrote import (`Imported`, `importMenuMap`/`importPanelMap`) and replaced `storeOverrides` with `storeShortcuts(menus, overrides, panels, ...)` | kept both. The preset code now calls the patch's helpers. `shortcutSetJson` (the export AND backup document) uses `allBindings` and carries `panels`, and `shortcuts.export` writes it through `writeShortcutSet`, so export output is the same as the accepted patch. `preset_apply` writes the preset's menu map with the **current panel overrides kept** (a preset maps menu actions only), and its inverse is the whole-map `shortcuts.import` with both `overrides` and `panels` |
| same file, after the merge | 561 counted lines, so the `file-length` gate failed ("new file over 500 lines") | the `shortcuts.*` registration table (the `add()` helper and `registerShortcutCommands`, moved verbatim) is now `src/gui/ControlCommandsShortcutsRegister.cpp`. Seven one-line forwarders in `lmms::shortcutcommands` (`src/gui/ControlCommandsShortcutsShared.h`, the `*Shared.h` pattern of `src/core/ControlCommandsSafeStartShared.h`) give it the handlers. No schema or description text changed: `ControlCommandsSnapshot` passes against the live-regenerated snapshot |
| `src/gui/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/fork-sources.txt`, `tests/upstream-modifications.txt` | both sides added entries at the same place | kept every entry from both sides; nothing dropped. The two new files are registered in `src/gui/CMakeLists.txt` and `tests/fork-sources.txt`, and `tests/all-sources.txt` is re-derived with its recipe (+2 lines) |
| `data/manual/index.html` | the patch's "One key, four contexts" tip sat next to the train's nav line, which gained "Quick start" | kept the tip and the train's nav |
| `tools/mcp-zene-control/zene_control/commands_snapshot.json`, `README.md` | both regenerated | regenerated live (`snapshot_live.py`): **864 ids / 76 groups**, which is the train's 863 plus `automation.editor_mode`. Canary set to 864 |
| `docs/RELEASE-NOTES-v0.3.0-alpha.md` | A16 histogram | re-taken from `ReversibilityContractTest`: the build measured 864 = 863 + 1 `not_mutating`, so the published reference figure is `rows=873 true_inverse=430 snapshot=42 irreversible=25 not_mutating=376` (train 17's 872/375 + 1) |

**Semantic difference from the accepted patch, and its proof.** On train 16 the patch had no
presets, so this interaction is new: Q050-202 code running alongside the panel map. Without the
resolution above, a preset would have wiped every panel binding, or its "safety copy" would not
have restored them. New case `ShortcutsCommandsTest::aPresetKeepsPanelBindingsAndItsBackupCarriesThem`:
it binds an arranger action, applies `ableton-live`, checks that the binding is still there, undoes
and checks again, resets everything, then imports the backup and checks that the binding is back.
Red/green:

| variant | command | result | exit |
| --- | --- | --- | --- |
| merged tree | `ctest -R '^ShortcutsCommandsTest$'` (`test-3.log`) | PASS | 0 |
| red a: `preset_apply` writes `QJsonObject()` as the panel map | same (`test-red-a.log`) | FAIL "the preset left the arranger binding on ''" | 8 |
| red b: the backup/export document leaves out `panels` | same (`test-red-b.log`) | FAIL "the preset's safety copy did not restore the panel binding" | 8 |
| restored (`cmp` with the saved copy: identical), rebuilt | same (`test-4.log`, `test-6.log`) | PASS | 0 |

**Affected failure paths and supported-platform hazards (together).** Failure paths: (1) a preset
erasing panel bindings: covered by red a. (2) A backup that cannot restore panel bindings: covered
by red b. (3) Undo of a preset: the inverse restores the whole map, and the test undoes it. (4) An
export written by train-17 code then read back: `shortcutSetJson` is the patch's export document
(the same `format`/`version`), and the two-process transcript and `ShortcutsPageTest` pass.
(5) A forwarder that registers the wrong handler: `ControlCommandsSnapshot` and every
`shortcuts.*` test pass. Platform hazards: the new code is plain C++17/Qt-5.15-compatible: function
pointers, `QJsonObject`, a nested `namespace lmms::shortcutcommands` (C++17, as used elsewhere in
the tree). It adds no Qt-6-only API, no `std::filesystem`, no POSIX call, and no
`min`/`max`/`small`/`near`/`far`/`interface` identifier, and `check-namespace` passes. The handlers
have external linkage and no `LMMS_EXPORT`; they are only used inside the `zene` target (both `.cpp`
files are in `LMMS_SRCS`), so MSVC DLL export does not come into it. Nothing touches the audio thread.

Evidence on the merged tree (logs in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-055.1.r3/logs/`):

| step | command | exit |
| --- | --- | --- |
| build | `cmake --build build --target zene PanelShortcutsTest ShortcutsCommandsTest ShortcutsPageTest ReversibilityContractTest tripleoscillator -- -j4` (`build-6.log`, after the split) | 0 |
| focused tests | `ctest -R '^(PanelShortcutsTest\|ShortcutsCommandsTest\|ShortcutsPageTest\|ReversibilityContractTest\|ControlCommandsSnapshot)$' --output-on-failure` (`test-6.log`) | 0 (5/5) |
| two-process transcript | `python3 tests/control-panel-shortcuts.py build/zene` (`transcript-2.log`) | 0 |
| acceptance red: `panelshortcuts::install()` commented out in `Shell::showEvent`, rebuilt | `ctest -R '^PanelShortcutsTest$'` (`test-red-c.log`): both criteria FAIL | 8 |
| acceptance green: restored (`cmp` identical), rebuilt | same (`test-green-c.log`) | 0 |
| first run before the histogram was re-taken | focused tests (`test-1.log`): `ReversibilityContractTest` asked for the re-take | 8 |
| snapshot | `snapshot_live.py $PWD` (`snapshot-1.log`): 864 ids / 76 groups | 0 |
| doc figures | `python3 tests/doc-figures-gate.py` (`docfig-1.log`) | 0 |
| gates | `feature_check.py` (log dir redirected to `/tmp`, see the self-check) (`feature-check-3.log`): all 8 PASS | 0 |

`build-1.log` (exit 2) was the first build: make did not yet know the `PanelShortcutsTest` target
in the same invocation that re-ran CMake. The same command run again succeeded (`build-2.log`, exit 0).

## Integration onto `050/m0-train-16` (round 2, 2026-10-26)

The accepted patch (`feat/050-Q050-055.1-oc` @ `6b1e76688`) was applied by three-way merge onto
`050/m0-train-16` @ `cf3618e61` on branch `integration/Q050-055.1-cf3618e61a`. No product code
conflicted and no design changed. Five files conflicted, all ledgers or measured figures:

| file | conflict | resolution |
| --- | --- | --- |
| `tests/fork-sources.txt`, `tests/upstream-modifications.txt` | both sides appended entries at the end | union (train entries, then this unit's), no line dropped; `fork-sources` gate exit 0 |
| `tools/mcp-zene-control/zene_control/commands_snapshot.json` | both sides regenerated it | regenerated live from this tree's `zene` (`snapshot_live.py`): **850 ids / 76 groups** = the train's 849 + `automation.editor_mode` |
| `README.md` | canary (train 849, patch 846) | `<!-- canary -->850 ids / 76 groups<!-- /canary -->`; `doc-figures-gate.py` exit 0 |
| `docs/RELEASE-NOTES-v0.3.0-alpha.md` | A16 histogram (train 858 rows, patch 855) | re-taken from `ReversibilityContractTest` on the merged tree: `rows=859 true_inverse=425 snapshot=42 irreversible=24 not_mutating=368` (the train's figure plus the one `not_mutating` row); the test exit 0 |

The files both the train and the patch changed without a textual conflict (`src/gui/CommandLists.cpp`,
`tests/CMakeLists.txt`, the manual, `KNOWN-LIMITATIONS.md`) touch different functions/entries: the
train changed `pluginManager()`/`pluginModules()`, the patch only `shortcuts()`. `tests/all-sources.txt`
reproduces (exit 0). Supported-platform hazards: the merge adds no code beyond the accepted patch, so
the accepted patch's portability review (below, item 6) stands; no new realtime path.

Evidence on the merged tree (logs in `~/.local/share/zene-bunny-fix/qwen/runs/Q050-055.1.r2/logs/`):

| step | command | exit |
| --- | --- | --- |
| build | `cmake --build build --target zene PanelShortcutsTest ShortcutsCommandsTest ShortcutsPageTest ReversibilityContractTest tripleoscillator -- -j4` (`build-2.log`) | 0 |
| focused tests | `ctest -R '^(PanelShortcutsTest\|ShortcutsCommandsTest\|ShortcutsPageTest\|ReversibilityContractTest\|ControlCommandsSnapshot)$'` (`test-2.log`) | 0 (5/5) |
| two-process transcript | `python3 tests/control-panel-shortcuts.py build/zene` (`transcript-1.log`) | 0 |
| red: `panelshortcuts::install()` in `Shell::showEvent` commented out, rebuilt | `ctest -R '^PanelShortcutsTest$'` (`test-red-1.log`) - both criteria FAIL | 8 |
| green: restored (`git diff` of Shell.cpp empty), rebuilt | same (`test-green-1.log`) | 0 |
| gates | `feature_check.py` (log dir redirected to `/tmp`, see the self-check note) (`feature-check-2.log`) | 0, RESULT: PASS |

Environment note: the build needed `CCACHE_TEMPDIR=/tmp/ccache-tmp-q055` because ccache's configured
temporary directory is not writable in this session (`build-1.log`, exit 2, "Permission denied");
nothing in the tree changed for it.

## Acceptance criteria

| # | criterion | test run (command) | result | exit |
| --- | --- | --- | --- | --- |
| 1 | Assign the same key to different actions in all four contexts and verify only the active panel's action fires. | `ctest -R '^PanelShortcutsTest$' --output-on-failure` (from `build/tests`) - `theSameKeyMeansOneThingPerPanel` | PASS | 0 |
| 1 | Same, end to end across a real process and the socket. | `python3 tests/control-panel-shortcuts.py build/zene` | PASS (22 checks) | 0 |
| 2 | Create a same-context shortcut conflict, resolve it in the shortcuts UI, and verify the selected bindings survive restart without changing other contexts. | `ctest -R '^PanelShortcutsTest$'` - `aScopedClashSurvivesARestart` | PASS | 0 |
| 2 | Same, across TWO real processes (the second starts from the config the first saved and quit with). | `python3 tests/control-panel-shortcuts.py build/zene` | PASS | 0 |

How criterion 1 is proven: through the shortcuts page's own widgets (`CommandListDialog` over
`CommandLists::shortcuts()`; the row is selected and its **Set Keys...** button clicked, the opened
`CommandForm`'s `keys` field filled and **Run** pressed), `Ctrl+Alt+J` is bound to the arranger's
Knife tool, the note editor's Erase tool, the automation editor's Erase mode and Perform's Stop All.
The list reports `conflict_count` 0. Then `QTest::keyClick` is delivered to each panel with the
keyboard focus in it: the arranger's mode becomes Knife, `window.get_state.note_editor.tool` becomes
`erase`, the automation editor's mode becomes Erase, and a playing Perform scene stops - each key
press running only its own panel's command, the other three unchanged. A press with the menu bar
focused runs none of them. Criterion 2: two note-editor actions claim `Ctrl+Alt+K`; the second set
is refused typed (`refused`); the same form's `force` resolves it (winner holds the key, loser
unbound in that context only, arranger's own `Ctrl+Alt+K` untouched); `ConfigManager::loadConfigFile`
then re-reads the saved file and the store, `actionOf()` and the live router all report the same
resolution, and the built-in panel keys still work. The transcript repeats this in two processes.

## Self-check

**Round 3 (integration onto train 17), measured on the merged tree.** `submit_check.py`: see the
final paragraph of this section. `feature_check.py` cannot create its sibling log directory
`<worktree>-feature-check`, because that is outside the write-jail (`feature-check-1.log`:
PermissionError, exit 1). The copy `/tmp/q055-fc/feature_check.py` differs from it in two lines
only: the log directory and `ROOT`. It ran every gate: fork-sources, all-sources-reproduce,
no-upstream-regression, check-namespace, no-tautology and doc-figures 0; complexity and file-length
"no new item in your files"; RESULT: PASS, exit 0 (`feature-check-3.log`). The "?" items:
1. Red/green: the acceptance test with the router install removed exits 8 and passes (exit 0) when
   restored. The new merge case `aPresetKeepsPanelBindingsAndItsBackupCarriesThem` exits 8 under
   two separate reverts and 0 when restored (table above).
2. Criteria 1 and 2: `PanelShortcutsTest` exit 0, `control-panel-shortcuts.py` exit 0.
3. Undo and classes: no class changed. `preset_apply` is still one `uistore::write` (one undo
   step) whose inverse restores both maps, and the new test undoes it. `ReversibilityContractTest`
   exit 0 with the re-taken histogram.
4. Realtime: the merge adds GUI-thread command code only (config JSON, a file write that already
   existed for the backup). No audio path. No TSan on this box; this is an argument, not a measurement.
5. Projects: no project format or serialisation change. `ui/shortcuts` keeps its `overrides`, plus
   the optional `panels` object.
6. Portability: see "Affected failure paths and supported-platform hazards" above.


**Round 2 (integration onto train 16), re-measured on the merged tree.** `submit_check.py` (exit 1,
`submit-check-1.log`): every item PASS except "gates (feature_check.py)". That fail is the tool's
environment: `feature_check.py` makes its log directory `<worktree>-feature-check` beside the worktree,
outside this session's write-jail, and dies with `PermissionError` before running a gate
(`feature-check-1.log`, exit 1). A copy at `/tmp/q055-fc/feature_check.py` that differs only in that
log path and in `ROOT` pointing back at the real `qwen/` directory (two lines; reproduce with `diff qwen/feature_check.py /tmp/q055-fc/feature_check.py`)
runs every gate on this tree: RESULT: PASS, exit 0. The six "?" items, for the merged tree:
1. red/green: `panelshortcuts::install()` removed -> `PanelShortcutsTest` exit 8 (both criteria
   FAIL); restored -> exit 0. 2. criterion 1 and 2: `PanelShortcutsTest` exit 0 and
   `control-panel-shortcuts.py` exit 0. 3. undo/classes: unchanged from the accepted patch;
   `ReversibilityContractTest` exit 0 with the re-taken histogram, `ShortcutsCommandsTest` exit 0.
4. realtime: the merge added no code beyond the accepted patch (argument below stands; no TSan
   here). 5. projects: no format or serialisation change (below). 6. portability: no new code (below).
The round-1 record follows.

**1. Each new test FAILS with the product change reverted (exit code), then PASSES restored.**
Measured, three separate reverts:

| revert | command | result | exit |
| --- | --- | --- | --- |
| the router install line (`panelshortcuts::install()` in `Shell::showEvent`) removed | `ctest -R '^PanelShortcutsTest$'` | FAIL both criteria: `arranger->editMode()` 0, expected Knife; `noteTool()` "draw", expected "audition" | 8 |
| same, restored | same | PASS | 0 |
| the scope check in `clashOn` widened to the whole map (a same key in ANY scope is a clash) | same | FAIL both criteria: "Ctrl+Alt+J is already Arranger Knife Tool's key in note_editor" - the second binding is refused | 8 |
| same, restored | same | PASS | 0 |
| the panel store flattened under a control character again (the pre-fix layout) | `python3 tests/control-panel-shortcuts.py build/zene` | the config is not XML-parsable, the next start loses every binding | 0 (transcript) / see note |
| restored | same | PASS (22 checks) | 0 |

Note on the last leg: with the flat store the write itself succeeds and the transcript's write-side
checks pass; the read-back in the second process fails. That is recorded here rather than claimed as
a clean red because the transcript's own exit code was 0 in that configuration - I found the defect
this way (a `0x1f` separator inside an XML attribute makes the whole config unreadable at the next
start), fixed it by nesting the panel map by scope, and the transcript now passes in full. Logs:
`build/q050-055.1-logs/{build-red-1,build-red-2,test-red-1,test-red-2,transcript-red-2,test-green-2}.log`.

**2. Every acceptance criterion is proven by a test I ran myself (name + exit code per criterion).**
See the table above: criterion 1 by `PanelShortcutsTest::theSameKeyMeansOneThingPerPanel` (exit 0)
and `control-panel-shortcuts.py` (exit 0); criterion 2 by
`PanelShortcutsTest::aScopedClashSurvivesARestart` (exit 0) and the same transcript (exit 0). Logs:
`build/q050-055.1-logs/{test-17,transcript-green-3}.log`.

**3. Each edit is ONE undo step, and every new or changed command has an honest reversibility class.**
- A scoped binding write goes through `uistore::write`, the same "action checkpoint" a menu rebind
  uses: one undo step that puts the previous whole `ui/shortcuts` object back. Measured inside
  `theSameKeyMeansOneThingPerPanel`: one `control.undo` after four scoped sets removes exactly the
  last one (Perform's), the three earlier ones stay, and one `control.redo` puts it back - exit 0.
  A forced move (which clears another action of the same scope) records its inverse as a whole-map
  `shortcuts.import`, so it is still one step; `ShortcutsCommandsTest`'s existing "an undo of a set
  must not clear bindings it never touched" still passes (exit 0).
- `automation.editor_mode` is `not_mutating` with a reason and a mechanism in
  `ControlReversibilityTableAutomationModes.cpp`, and the A16 histogram in
  `docs/RELEASE-NOTES-v0.3.0-alpha.md` is re-taken for it (rows 854 -> 855, `not_mutating`
  365 -> 366). `ReversibilityContractTest` reads that block and passes (exit 0).
- The one edit that touches no undo step at all is the router's dispatch (a key press) - it runs a
  command that has its own class, never a write of its own.

**4. Nothing added runs on the audio thread.** The diff touches no audio-path code: `PanelShortcuts.cpp`
is an application event filter and runs on the GUI thread only (Qt delivers key events there); its
config read is `ConfigManager`'s in-memory settings, so no I/O and no lock; the command it invokes
goes through `ControlRegistry::runOnUiThread`. `ControlCommandsAutomationMode.cpp` only triggers a
`QAction`. `AutomationEditor::setEditMode` calls `update()` (a repaint request) - it is GUI code
reached from the GUI thread. No allocation, free, lock, wait, I/O or signal is added anywhere the
audio thread can run. Local TSan does not run on this box; the argument above is the evidence.

**5. Old projects still load and render exactly as before; new data round-trips.** No project format
changed: `ui/shortcuts` is a config value, not project state, and an existing config (only
`overrides`) loads unchanged (`ShortcutsCommandsTest` exit 0). No serialisation code was touched, so
a project's save/load path is byte-identical. The new data - the `panels` object and
`automation.editor_mode` - round-trips: the panel map is written, read back by the same process, and
read back by a SECOND process from the same config text (`control-panel-shortcuts.py`, exit 0).

**6. Qt 5.15 / MSVC / macOS < 10.15 portability.** No Qt-6-only API: `QKeyCombination` is used only
under `#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)` (with the Qt 5 int form in the `#else`); no
`std::filesystem`; no POSIX-only call; no `windows.h` macro name (`small`, `near`, `far`,
`interface`, `min`, `max`) used as an identifier. `check-namespace` passes (exit 0).

**7. Gates.** `feature_check.py` on the committed tree: PASS, exit 0 - `fork-sources` 0,
`all-sources-reproduce` 0, `no-upstream-regression` 0, `check-namespace` 0, `no-tautology` 0,
`doc-figures` 0, and `complexity` / `file-length` "no new item in your files". Both ratchets exit 1
on the train by themselves (6 and 19 pre-existing regressions, none in a file this branch changed;
verified both by grepping their output for each of the branch's files and by replaying
`feature_check`'s own base-vs-branch set difference against the cached base listing).

`submit_check.py` reports one FAIL, "gates (feature_check.py)", and it is the tool's own
environment rather than the work: `feature_check.py` writes its logs to the SIBLING directory
`<worktree>-feature-check`, which is not creatable here (`PermissionError: ... Permission denied`),
so submit_check's subprocess call exits non-zero before running any gate. Run with only that log
path redirected, the same script prints `RESULT: PASS` for this tree (exit 0, paste above). I did
not create or chmod the sibling path - that is outside the worktree and outside what this lane may
touch.

Note on the log directory: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-055.1.r1/logs` is not
writable in this session (`touch` there is permission-denied even as the owning user), so numbered
logs were kept in the ignored `build/q050-055.1-logs/` instead.

## What changed

| area | file |
| --- | --- |
| the four scopes, the action table, the store access, the key router | `include/PanelShortcuts.h`, `src/gui/PanelShortcuts.cpp` (new) |
| `Binding::scope`; `conflicts` is scope-aware | `include/ShortcutMap.h`, `src/gui/ShortcutMap.cpp` |
| `shortcuts.list/set/reset/export/import` learn `scope` and `panels` | `src/gui/ControlCommandsShortcuts.cpp` |
| `automation.editor_mode` (new command) | `src/gui/ControlCommandsAutomationMode.cpp` (new), `AutomationEditorWindow::editorMode/setEditorMode` |
| the shortcuts page shows and passes the context | `src/gui/CommandLists.cpp` |
| the router is installed | `src/gui/shell/Shell.cpp` |
| A16 row, histogram, snapshot, canary, docs, ledger, manifests | `src/core/ControlReversibilityTableAutomationModes.cpp`, `docs/RELEASE-NOTES-v0.3.0-alpha.md`, `tools/mcp-zene-control/zene_control/commands_snapshot.json`, `README.md`, `data/manual/index.html`, `docs/KNOWN-LIMITATIONS.md`, `docs/050-features/Q050-055.1.md`, `include/PerformView.h`, `tests/upstream-modifications.txt`, `tests/fork-sources.txt`, `tests/all-sources.txt` |

Store layout: `ui/shortcuts` = `{"overrides": {menuId: keys}, "panels": {scope: {actionId: keys}}}`.
Only a string is an override; `""` is a deliberate unbind; a panel action with no override has no
key (so every existing built-in panel key keeps working until the user binds over it).

`tests/all-sources.txt` was re-derived with its own header recipe, which also removed the 1148
vendored `plugins/SfzPlayer/sfizz/...` entries the base commit `07d6d02bd` had re-added against
that recipe (its header documents the same regression having been fixed once before). No entry for
first-party code left the list; `all-sources-reproduce` now prints REPRODUCES (exit 0).

## Tests

- `tests/src/gui/PanelShortcutsTest.cpp` (new, registered in `tests/CMakeLists.txt`) - both criteria,
  driven through the GUI controls and real key delivery.
- `tests/control-panel-shortcuts.py` (new) - the same criteria across two real processes, the second
  started from the first one's saved config.
