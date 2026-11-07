FIXED

# cluster gui-1106-menu-edit-collaborate - Edit > Collaborate... silently refused in agent / offscreen GUI sessions

## Verdict: REAL

Evidence (base `af010bc3c`):

- `src/core/ControlCommandsCollab.cpp:200` registered `collab.show_panel` with `requiresDecl = {"display"}`.
- `src/core/ControlRegistry.cpp:233-235` refuses any `display` command while `m_headless` is set, and
  `m_headless = isUnattendedRun()` (`ControlRegistry.cpp:159`), which is true for `--control-socket`
  (`src/core/main.cpp:1264`, `setAgentInstance(true)`) and for `QT_QPA_PLATFORM=offscreen|minimal|vnc`
  (`src/core/UnattendedRun.cpp:47-59,76-79`). Such sessions still have the main window and its menus.
- `src/gui/MainWindow.cpp:417-418`: Edit > Collaborate... calls `dispatchShellCommand("collab.show_panel")`, and
  `dispatchShellCommand` (`src/gui/ControlCommandsShell.cpp:504-507`) discards the `ControlResult`. So the
  refusal never shows up anywhere: the menu closes and no panel appears, which is exactly the finding.
- The panel is not a reason to refuse. `showCollabPanel` (`src/gui/CollabPanel.cpp:79-89`) shows a non-modal
  `Qt::Tool` widget and refuses with `requires` itself when there is no main window (line 83). Its only modal
  question, `collabConsentPrompt` (`CollabPanel.cpp:73`), is reached only from `collabHost`, and only when
  `personPresent()` is true (`ControlCommandsCollab.cpp:73-76,92`), i.e. never in an unattended run.
- The sibling panels in the same Edit menu (`window.tempo_map`, `window.progression`, `window.livecode`,
  `window.undo_history`, `MainWindow.cpp:405-416`) and in the footer Tools list (`src/gui/shell/ShellToolsMenu.cpp:71-83`)
  declare no `display` requirement. They open in these sessions; Collaborate was the only one that did not.

## Fix

`src/core/ControlCommandsCollab.cpp`: drop `requiresDecl = {"display"}` from `collab.show_panel`, with a comment
giving the reason. This brings the command in line with the other non-modal panels. Both entry points
(Edit > Collaborate... and the footer Tools > Collaboration item, which goes through `runCommand` ->
`ControlRegistry::invoke`) now open the panel. Nothing else changes:
- The command set is the same, and the id count and ControlCommandsSnapshot canary don't move. Only
  `commands_list`'s `requires` array for this one command changes, from `["display"]` to `[]`.
- No project or save data is touched. The command stays `not_mutating`
  (`ControlReversibilityTableCollab.cpp:65`).
- No audio-thread code is touched.
- A GUI-less run still gets a typed `requires` refusal from the handler (`CollabPanel.cpp:83`).

Contract change, stated openly: the existing `CollabPanelTest::theCommandShowsThePanel` asserted the old
headless refusal. It now asserts the opposite: under `setHeadless(true)` the command succeeds with `shown: true`.
Its other assertions are unchanged: single instance, visible, accessible name, Host enabled, Stop disabled.
A new slot, `theEditMenuOpensThePanelInAHeadlessSession`, triggers the real Edit > Collaborate... `QAction`
(found by its `controlCommand` property and label) with the registry headless. It checks that one visible
"Collaborate" panel appears with enabled Host and Join controls. No timing is involved.

Siblings checked and left alone (different clusters, not part of this finding): `app.help` / `app.tour`
(`src/gui/ControlCommandsHelp.cpp:142,160,165`) and the plugin-editor commands
(`src/core/ControlCommandsPluginEditor.cpp:115`, `src/gui/ControlCommandsPluginActions.cpp:178`) also declare
`display`. Plugin editors genuinely need a native window. Whether the help menus show the same silent refusal is
UNVERIFIED here.

## Commands and results

LOG FOLDER = `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-edit-collaborate.r1/logs`. All builds ran
with `CCACHE_DISABLE=1`, because ccache's temp dir (`~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`)
cannot be written from this lane (`01-build-red.log`: "Permission denied", EXIT=2).

| Step | Command | EXIT |
| --- | --- | --- |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target CollabPanelTest -j4 > LOGS/02-build-red.log` | 0 |
| RED test | `cd build/tests && ctest -R '^CollabPanelTest$' --output-on-failure > LOGS/03-test-red.log` | 8 (2 failed: `'headlessShown.ok' returned FALSE. (command 'collab.show_panel' requires a display (headless instance))`; `'panel->isVisible()' returned FALSE. (the menu closed but no Collaborate window appeared)`) |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene CollabPanelTest -j4 > LOGS/04-build-green.log` | 0 |
| GREEN test | `ctest -R '^CollabPanelTest$' --output-on-failure > LOGS/05-test-green.log` | 0 (1/1 test, 5 slots pass) |
| family build | `CCACHE_DISABLE=1 cmake --build build --target CollabPolicyTest CollabTransportTest ReversibilityContractTest -j4` | 0 |
| family test | `ctest -R "Collab\|ReversibilityContractTest\|ControlReversibilityTranscript" --output-on-failure > LOGS/07-test-family.log` | 8: 6 tests run, 4 pass (CollabPolicyTest, CollabPanelTest, ReversibilityContractTest, ControlCollab); 2 fail on the base too, see below |
| 4b revert | `git diff -- src/core/ControlCommandsCollab.cpp > LOGS/fix.patch && git apply -R LOGS/fix.patch` | 0 |
| 4b build | `CCACHE_DISABLE=1 cmake --build build --target zene CollabPanelTest CollabTransportTest -j4 > LOGS/08-build-unfixed.log` | 0 |
| 4b test (no fix) | `ctest -R '^CollabPanelTest$' --output-on-failure > LOGS/09-test-unfixed.log` | 8 (same 2 failures as RED) |
| base check | `ctest -R '^(CollabTransportTest\|ControlReversibilityTranscript)$' --output-on-failure > LOGS/10-test-unfixed-unrelated.log` (fix reverted) | 8: the same two failures without the fix |
| 4b re-apply | `git apply LOGS/fix.patch` | 0 |
| 4b build | `CCACHE_DISABLE=1 cmake --build build --target zene CollabPanelTest -j4 > LOGS/11-build-refixed.log` | 0 |
| 4b test (fix) | `ctest -R '^CollabPanelTest$' --output-on-failure > LOGS/12-test-refixed.log` | 0 |
| gates | `python3 LOGS/feature_check_logdir.py .` (see note) `> LOGS/14-feature-check.log` | 0: all 8 rows PASS |

Failures that are not caused by this fix: both reproduce identically with the fix reverted (`10-test-unfixed-unrelated.log`):
- `CollabTransportTest::throttledUntrackedEditIsRetried`: "Compared values are not the same". It does not touch the panel or the registry's requirements.
- `ControlReversibilityTranscript`: "the LADSPA host plugin 'ladspaeffect' is not in this build".

Gate note: `feature_check.py` as shipped fails with `PermissionError` (`13-feature-check.log`, EXIT=1). It runs
`mkdir <worktree>/../<name>-feature-check`, and that path is outside this lane's write-jail. I ran a byte-identical
copy with two lines changed (diff recorded in the session): `ROOT` points at the original
`~/.local/share/zene-bunny-fix/qwen` (same FIX-BASE and baseline cache), and `logs` points at `LOGS/feature-check`.
The gates and their inputs are unchanged.

Files committed: `src/core/ControlCommandsCollab.cpp`, `tests/src/gui/CollabPanelTest.cpp`, `tests/CMakeLists.txt`
(comment only), `tests/upstream-modifications.txt` (one line for `tests/CMakeLists.txt`), this report.
