FIXED

# gui-1106-menu-navigate-next-marker - Navigate refusal notices are placed outside the main window

## Verdict: REAL

Navigate > Next Marker with no marker refuses (`marker.jump`), the refusal is announced, but the floating notice is
positioned past the main window's right edge, so it is never seen.

## Evidence (file:line, current tree)

- `src/gui/MenuBarExtras.cpp:95` - "Next Marker" is `marker.jump` through `addCommandAction`.
- `src/gui/CommandRun.cpp:49-53,128` - `hostOf(menu)` climbs out of the menu to its parent, which for the Navigate menu is the
  main window (`MenuBarExtras.cpp:67`, `new QMenu(..., bar->window())`); `runCommand(host, ...)` -> `announce(parent, ...)`.
- `src/gui/CommandRun.cpp:95` - `announce` passes that `parent` to `TextFloat::displayMessage`.
- `src/gui/widgets/TextFloat.cpp:113` (before the fix) - a non-null parent is a widget to float *beside*:
  `moveGlobal(parent, QPoint(parent->width() + 2, 0))`. With the main window as parent that is x = window width + 2.
  Measured: notice at `1602,0 158x76` in a main window `0,0 1600x1000`.
- Every menu command (`runCommand(hostOf(menu)...)`, `menu->parentWidget()`, `window()` callers) reaches the same line, so the
  fix is at `displayMessage`, not in the Navigate menu.

## Fix

`src/gui/widgets/TextFloat.cpp` `displayMessage`: a parent that is the main window takes the same placement as "no parent"
(lower-left inside the main window). Any other parent is still placed beside it, unchanged. No new command, no audio path.
`tests/upstream-modifications.txt` gets a line for the change.

## Integration into 050/m0-train-26 (round 2)

- Source `bugs/qwen-gui-1106-menu-navigate-next-marker` @ `23ff7a64a7173fab76ee342419409a70a681c99e`; target
  `050/m0-train-26` @ `af010bc3cc055d477045a54e2d866f59a95017d7`; source base = target base = `af010bc3cc`
  (`integration-evidence.json`).
- The driver's three-way merge fell back to direct application and all three files applied cleanly
  (`integration-apply.log`): `src/gui/widgets/TextFloat.cpp`, `tests/src/gui/GrooveMenuTest.cpp`,
  `tests/upstream-modifications.txt`.
- **Semantic identity with the accepted patch:** `git diff 23ff7a64a7 --stat` (working tree vs the accepted branch) is empty,
  so the integrated tree is byte-identical to the accepted branch for every tracked file. No design or test change was made.
- **The claimed conflict ("a newer line in `src/gui/widgets/TextFloat.cpp`"):** the base `af010bc3cc` already contains the
  theme-17 a11y block at `TextFloat.cpp:124-128`, which the accepted patch does not touch; the patch hunk's context
  (`if(parent != nullptr)` ... `QWidget * mw = getGUI()->mainWindow();`) matches the base exactly, so the direct application
  is clean. The patch only moves the `mw` declaration out of the `else` and adds `&& parent != mw`. No conflict resolution
  beyond the accepted design was needed.

## Affected failure paths and supported-platform hazards

- `displayMessage(..., parent == main window)`: every menu-command refusal (`hostOf(menu)` -> main window) now lands inside.
  This is the fixed path.
- `displayMessage(..., parent == nullptr)`: unchanged, lower-left inside the main window.
- `displayMessage(..., parent == any other widget)`: unchanged, beside that widget's right edge.
- Sibling `moveGlobal(this, width() + 2, ...)` uses (Fader, ClipView, SongEditor, ...): untouched; they float beside their own
  widget, never a main-window parent.
- Platform hazards: none new. The change is a pointer comparison plus a declaration move; no new include, no Qt 5/Qt 6 or
  MSVC/macOS-specific API, no realtime/audio-thread call, no saved-data format change. The regression uses the offscreen
  platform's fixed geometry; the assertion is a pure rectangle containment.

## Regression test

`tests/src/gui/GrooveMenuTest.cpp` `aRefusalOverTheMainWindowLandsInsideIt`: runs the real `runCommand(mainWindow,
marker.jump{direction:next})` in a fresh project (refuses), takes the `TextFloat` shown over the main window and requires its
geometry to lie inside the main window's global rect. No timing or sleeps.

## Evidence table (round-2 logs in this run's `logs/` folder)

| Step | Command | EXIT | Result |
|---|---|---|---|
| Build with fix | `cmake --build build --target zene GrooveMenuTest -j4` (`build-1.log`) | 0 | built |
| GREEN (fix) | `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure` (`test-green-1.log`) | 0 | 1/1 passed |
| 4b extract patch | `git diff HEAD -- src/gui/widgets/TextFloat.cpp > logs/fix.patch` | 0 | 23-line product patch |
| 4b revert fix | `git apply -R logs/fix.patch` | 0 | product change removed from the working tree |
| 4b rebuild reverted | `cmake --build build --target zene GrooveMenuTest -j4` (`build-2-reverted.log`) | 0 | built |
| 4b RED | same ctest (`test-red-1.log`) | 8 | FAIL `aRefusalOverTheMainWindowLandsInsideIt` at `GrooveMenuTest.cpp:188`: "notice at 1602,0 158x76 is outside the main window 0,0 1600x1000" |
| 4b re-apply | `git apply logs/fix.patch` | 0 | fix restored; `git diff` clean |
| 4b rebuild | `cmake --build build --target zene GrooveMenuTest -j4` (`build-3-reapplied.log`) | 0 | built |
| 4b GREEN | same ctest (`test-green-2.log`) | 0 | 1/1 passed |
| Family build | `cmake --build build --target AccessibilityHelperTest CommandFormTest PluginShortcutsTest -j4` (`build-4-family.log`) | 0 | built |
| Affected family | `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest\|CommandFormTest\|AccessibilityHelperTest)$'` (`test-family-1.log`) | 0 | 4 tests run, 4 passed |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`gates-1.log`) | 0 | 8/8 PASS, RESULT: PASS |

Round-1 note: the earlier lane ran with `CCACHE_DISABLE=1` and a redirected copy of `feature_check.py` because that sandbox
forbade the gate's sibling log directory. In this round the stock gates ran unmodified and passed, so no workaround is needed.

Files committed: `src/gui/widgets/TextFloat.cpp`, `tests/src/gui/GrooveMenuTest.cpp`, `tests/upstream-modifications.txt`,
`docs/qwen-fixes-2026-10/cluster-gui-1106-menu-navigate-next-marker.md`.

Not covered: other callers that pass a *child* widget near the right edge of the screen are placed beside that widget as
before; that is a different behaviour from this finding and was left unchanged. Not run in a real display session: the test
uses the offscreen platform geometry.

## Self-check

- **Test fails with the fix reverted, passes with it:** yes. Round 2: reverted `ctest -R '^GrooveMenuTest$'` EXIT=8
  (`test-red-1.log`, "notice at 1602,0 158x76 is outside the main window 0,0 1600x1000"); fix re-applied EXIT=0
  (`test-green-2.log`); with the fix from the start EXIT=0 (`test-green-1.log`).
- **Deterministic:** yes. It calls `runCommand` synchronously and reads the `TextFloat` it created, then compares its
  `geometry()` with the main window's global rect. No timers, sleeps or waits (the float's close timer is never needed), and
  the window geometry comes from the offscreen platform's fixed size.
- **Fix at the cause, callers and siblings:** the cause is `TextFloat::displayMessage` treating every non-null parent as
  "float beside it". All `displayMessage` callers were listed (`grep -rn 'displayMessage(' src/ include/`); only
  `CommandRun.cpp:95` (`announce`) passes a caller-supplied widget. Its callers pass the main window via `hostOf(menu)`,
  `menu->parentWidget()` or `window()` (`CommandRun.cpp:128,150`, `src/gui/shell/ShellMenuEntries.cpp:123`,
  `LibraryCommandMenus.cpp:140` ...), so one fix in `displayMessage` covers all of them, none had to be edited. `TimeLineWidget`, `ClipView`, `PianoRoll`
  etc. pass no parent or their own widget and are unchanged. The sibling `moveGlobal(this, width()+2 ...)` uses (Fader,
  ClipView, SongEditor...) position their own floats beside their own widgets, not a main-window parent, and are
  intentionally untouched.
- **No collateral damage:** nothing is stored or loaded; only the screen position of a transient notice changes, and only when
  the parent is the main window. No limit or check was added. Notices with any other parent, and with no parent, are placed
  exactly as before. Nothing on the audio thread.
- **Verdict is FIXED**, so the ALREADY-FIXED/FALSE-POSITIVE item does not apply.
- **Gates:** the stock `feature_check.py` ran unmodified and reports 8/8 PASS, RESULT: PASS (`gates-1.log`, EXIT 0). The
  round-1 sandbox limitation (the gate's sibling log directory) did not recur.
