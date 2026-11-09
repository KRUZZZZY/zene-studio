FIXED

# cluster gui-1105-menu-navigate-next-marker — Navigate refusal notification is placed off-screen

## Verdict: REAL, fixed

- `src/gui/MenuBarExtras.cpp:95` adds "Next Marker" through `addCommandAction`. Its trigger calls
  `runCommand(hostOf(menu), …)` (`src/gui/CommandRun.cpp:128`), and `hostOf` climbs past the menus to the
  **main window** (`src/gui/CommandRun.cpp:49-53`). A refusal goes through `announce` →
  `TextFloat::displayMessage(…, parent)` (`src/gui/CommandRun.cpp:95-96`).
- Before the fix, `TextFloat::displayMessage` placed every float with a parent at `parent->width() + 2, 0`, which is
  just past the parent's right edge (old `src/gui/widgets/TextFloat.cpp:113`). That placement suits a knob or a
  button. For a whole window it means outside the window, so the float is off-screen when the window is maximized.
  The RED run measured it: `float at 802,0 158x76 is outside the main window 800x600`.
- Other callers also pass whole windows: `LayoutMenu.cpp:100,120` (`QApplication::activeWindow()`),
  `CommandListDialog.cpp:359` (`this`, a dialog), `ControlCommandsArrangerView.cpp:354` (`controlMainWindow()`) and
  `CommandPalette.cpp:257`. They all have the same defect, so the fix goes into `displayMessage` (the cause) and not
  into the Navigate menu.

## Fix

`src/gui/widgets/TextFloat.cpp:111-120`: a parent that `isWindow()` is now placed like the no-parent case. The message
goes in the lower left area inside that window (`32, height - float height - 8`). A child-widget parent (knobs,
clips, the file browser and so on) is placed exactly as before. A null parent still uses the main window as before.
The change touches no audio path, no undo step and no control command. It only moves a GUI widget.
Registered in `tests/upstream-modifications.txt` (an inherited LMMS file).

## Regression

`tests/src/gui/GrooveMenuTest.cpp` `aRefusalFromTheMainWindowIsShownInsideIt`:
1. Sets the main window to 800x600.
2. Installs the real Navigate menu (`installNavigateMenu`) and triggers "Next Marker" in a project with no markers.
3. Requires a TextFloat to be shown, and requires every shown float to lie inside the main window's global rectangle.

The test is deterministic: no timers and no sleeps.

## Evidence (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-next-marker.r1/logs`)

All builds ran with `CCACHE_DISABLE=1`: ccache's temp dir under `zene-bunny-fix/dsh/home` refused writes (log 01, EXIT=2, `Permission denied`).

| Step | Command | EXIT |
| --- | --- | --- |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target GrooveMenuTest -j4` (02) | 0 |
| RED test | `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure` (03): `FAIL! aRefusalFromTheMainWindowIsShownInsideIt … float at 802,0 158x76 is outside the main window 800x600` | 8 |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4` (04) | 0 |
| GREEN family | `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure` (05): 100% tests passed out of 2 | 0 |
| 4b reverse | `git diff -- src/gui/widgets/TextFloat.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b build + test | build (06) EXIT=0; `ctest -R '^GrooveMenuTest$'` (07): same FAIL at 802,0 | 8 |
| 4b re-apply | `git apply logs/fix.patch` | 0 |
| 4b build + test | build (08) EXIT=0; `ctest -R '^GrooveMenuTest$'` (09): 100% tests passed out of 1 | 0 |
| Gates | `feature_check.py` (11): 8/8 PASS, RESULT: PASS | 0 |

The gate run used a copy of `feature_check.py`, not the original. Two lines differ, and only in file paths: `ROOT`
is pinned to `~/.local/share/zene-bunny-fix/qwen`, and the log folder moves into the LOG FOLDER. The original writes
`<worktree>-feature-check` next to the worktree, which is outside this lane's write-jail (first attempt, log 10:
`PermissionError`). The gates themselves, the baselines and the comparison logic are unchanged.

Files committed: `src/gui/widgets/TextFloat.cpp`, `tests/src/gui/GrooveMenuTest.cpp`,
`tests/upstream-modifications.txt`, this report.

## Integration onto 050/m0-train-26 (round 2, 2026-11-09)

Source `bugs/qwen-gui-1105-menu-navigate-next-marker` @ `42c566a498`, target `050/m0-train-26` @ `af010bc3cc`.
The reported conflict in `src/gui/widgets/TextFloat.cpp` did not need a hand resolution: the driver's three-way
merge fell back to direct application, and the patch applied cleanly to all files (`integration-apply.log`).
The file has no commit on the target since the accepted patch's base (`git log af010bc3cc -- src/gui/widgets/TextFloat.cpp`
tops out at `b9a463ad42`, the same parent the accepted fix sits on). The integrated `TextFloat.cpp` and
`GrooveMenuTest.cpp` are byte-identical to the accepted commit (`git diff --quiet 42c566a498 -- <file>`: both
identical). The design is the same, with no semantic difference. The target has 31 `TextFloat::displayMessage(` calls in
`src`/`plugins`, and the caller analysis below still holds. The change is a pure Qt widget move. It has no
platform-specific code (`isWindow()`/`moveGlobal` are portable Qt), no audio path, no undo step and no command.

Logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-next-marker.r2/logs` (all builds `CCACHE_DISABLE=1`, -j4):

| Step | Command | EXIT |
| --- | --- | --- |
| Build | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4` (01) | 0 |
| GREEN family | `cd build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure` (02): 100% passed out of 2 | 0 |
| 4b reverse | `git diff --cached -- src/gui/widgets/TextFloat.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b RED | build (03) EXIT=0; `ctest -R '^GrooveMenuTest$'` (04): `FAIL! aRefusalFromTheMainWindowIsShownInsideIt … float at 802,0 158x76 is outside the main window 800x600` | 8 |
| 4b re-apply | `git apply logs/fix.patch` | 0 |
| 4b GREEN | build (05) EXIT=0; `ctest -R '^GrooveMenuTest$'` (06): 100% passed out of 1 | 0 |
| Gates (original) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (07): `PermissionError` creating `<worktree>-feature-check` (write-jail) | 1 |
| Gates (path-only copy) | `python3 logs/feature_check_copy.py <worktree>` (08): 8/8 PASS, RESULT: PASS | 0 |

The copy differs from the original only in lines 19 (`ROOT` pinned to the original's directory, so the same
`FIX-BASE` and `gate-baselines` are used) and 70 (log folder moved into the LOG FOLDER). `diff` of the two is in the
session log.

Files committed: `src/gui/widgets/TextFloat.cpp`, `tests/src/gui/GrooveMenuTest.cpp`,
`tests/upstream-modifications.txt`, this report.

## Self-check

- **Fails without the fix, passes with it:** step 4b above. With `logs/fix.patch` reversed, `ctest -R '^GrooveMenuTest$'`
  gave EXIT=8 (`float at 802,0 158x76 is outside the main window 800x600`). With it re-applied, EXIT=0 (1/1 passed).
- **Deterministic:** no timers, sleeps or waits. The test sets a fixed 800x600 geometry on the main window and triggers
  the real "Next Marker" action synchronously (`QAction::trigger`; the command and `displayMessage` both run inline).
  It then reads the float's geometry, which `displayMessage` sets with `move()` before it returns.
- **At the cause and complete:** the defect is in the placement rule in `TextFloat::displayMessage`
  (`src/gui/widgets/TextFloat.cpp:111-120`), the only function that places a float relative to its parent. I checked
  every `displayMessage` caller (`rg -n displayMessage src`):
  - These pass a whole window, and now land inside it: CommandRun's `announce` (via `runCommand`/`addCommandAction`
    with `hostOf`, `LayoutMenu` with `activeWindow()`, `CommandListDialog` with `this`, `CommandPalette`) and
    `ControlCommandsArrangerView.cpp:354` with `controlMainWindow()`.
  - These pass a child widget, or nothing, and behave unchanged: TimeLineWidget, ClipView, FileBrowser,
    `ControllerTemplateMenu`, MainWindow, PianoRoll, ClipLockFeedback and the rest.
  - The sibling `SimpleTextFloat`/`FloatModelEditorBase` (`FloatModelEditorBase.cpp:137`) places beside a knob
    widget, never beside a window, so it does not have this defect and is untouched.
- **No collateral damage:** the change moves a GUI widget only. No project data, saved files, commands, undo or audio
  paths are involved. It adds no limit or check that another caller could hit. Child-widget parents take the
  identical old branch. The PluginShortcutsTest and GrooveMenuTest family passed (05: 2/2).
- **ALREADY-FIXED / FALSE-POSITIVE:** not applicable. The verdict is FIXED, and the RED run reproduced the bug on the
  unmodified code.
- **submit_check "gates" line:** `submit_check.py` reports `FAIL gates (feature_check.py)` with no failing gate named.
  It runs the original `feature_check.py`, which cannot create `<worktree>-feature-check` from inside this lane's
  write-jail (log 12: `PermissionError`, EXIT=1). The path-only copy, run on the committed tree, passes all 8 gates
  (log 13: RESULT: PASS, EXIT=0). This is an environment limit, not a gate result. A run from outside the jail should
  confirm it.
