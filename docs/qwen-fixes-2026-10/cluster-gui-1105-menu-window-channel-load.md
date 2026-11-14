FIXED

# gui-1105-menu-window-channel-load - Channel Load results are positioned off-screen

## Verdict: REAL, fixed at the cause

Window > Channel Load is `run(menu, "Channel Load", "mixer.cpu_load", {}, false, true)` (`src/gui/WorkspaceMenus.cpp:135`;
the last `true` is `showResult`). The action runs `runCommand(hostOf(menu), ...)` (`src/gui/CommandRun.cpp:127-129`) - `hostOf`
walks past every QMenu, so the parent is the **main window**. `announce()` then calls
`TextFloat::displayMessage(title, text, QPixmap(), 8000, parent)` (`CommandRun.cpp:95`), and with a parent that function
placed the float at `parent->width() + 2, 0` (`src/gui/widgets/TextFloat.cpp`, old line 113) - the window's right edge plus
2 px. For the main window (or any widget flush with the window's right edge) that is outside the app. The readout is created
and shown, then sits where nobody sees it. Measured on the unfixed code: float frame at `902,0 124x80` in a window `0,0 900x600`.

## Fix

`TextFloat::displayMessage` (the one place the position is decided) now clamps the beside-the-parent position into the
top-level window of the parent (`insideWindowOf`, `src/gui/widgets/TextFloat.cpp`), using the float's `frameSize()` (`move()`
places the frame, which is 2 px larger than the client area on every side - found when the first clamp using `size()` still
left the float 4 px over the edge). A position already inside the window is returned unchanged, so every caller whose
float already lay beside a widget inside the window (ClipView hints, TimeLine hints, PianoRoll, ...) behaves as before.

Siblings checked: all 30-odd `displayMessage` callers go through this one function; those with no parent use the other branch
(lower left of the main window, already inside); `SimpleTextFloat` is a different widget (knob value readout) and not in the
finding's path. No header change, no new command, nothing on the audio thread, no saved data touched.

## Tests (`tests/src/gui/GrooveMenuTest.cpp`, real GUI app, offscreen; no timing - only geometry)

- `aResultFloatedOverTheMainWindowLiesOverIt` - displayMessage over the main window lies inside it.
- `aMessageBesideAWidgetStaysOverTheWindow` - a widget flush with the right edge: inside; a widget well inside: the old
  beside-the-widget position is kept exactly (`QCOMPARE(pos, mapToGlobal(width+2, 0))`).
- `channelLoadFromTheWindowMenuShowsInsideTheApp` - the product's Window menu (built by `installWorkspaceMenus`, the installer the
  shell runs on first show) is triggered through its real `Channel Load` `QAction` (`mixer.cpu_load`, host = main window), and the
  readout it floats lies inside the app. Round 3: this slot used to call `runCommand` directly (Luna's round-2 note); it now goes
  through the menu action.

## Evidence

All logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-window-channel-load.r1/logs/`.

| Step | Command | Exit | Result |
| --- | --- | --- | --- |
| RED (fix reverted via `git apply -R fix.patch`, rebuilt) | `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure` (`test-6-RED-final.log`) | 8 | 3 failed, 4 passed; `float 902,0 124x80 is not inside the window 0,0 900x600` |
| GREEN (fix re-applied, rebuilt) | `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure` (`test-7-GREEN-final.log`) | 0 | 2 of 2 ctest tests passed (GrooveMenuTest: all 7 slots) |
| builds | `cmake --build build --target GrooveMenuTest PluginShortcutsTest -j4` (`build-7`, `build-8`) | 0 | |
| gates | feature_check (copy, see below) (`gates-2.log`) | 0 | 8/8 PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

Files committed: `src/gui/widgets/TextFloat.cpp`, `tests/src/gui/GrooveMenuTest.cpp`, `tests/upstream-modifications.txt`
(new line for TextFloat.cpp), this report.

## Integration onto 050/m0-train-26 (round 2, 2026-11-09)

Job: integrate accepted `bugs/qwen-gui-1105-menu-window-channel-load` @ `c606e68b11` onto `050/m0-train-26` @ `af010bc3cc`
(reported conflict in `src/gui/widgets/TextFloat.cpp`).

- The source branch is based directly on the target (`git merge-base --is-ancestor af010bc3cc c606e68b11` true), and the
  driver's fallback applied every file cleanly (`integration-apply.log`: "Applied patch to 'src/gui/widgets/TextFloat.cpp'
  cleanly", likewise GrooveMenuTest.cpp and upstream-modifications.txt). No conflict markers remain.
- Semantic difference from the accepted patch: **none**. The +/- lines of the staged diff are byte-identical to
  `accepted.patch` (`diff` of both line sets printed `SAME`; staged diff saved as `r2/logs/01-staged.diff`). No new design.
- Affected failure paths (unchanged from round 1): `displayMessage` with a parent (main window via `CommandRun.cpp`
  `announce()`; a widget flush with the window's right edge; a widget inside the window - position kept); the no-parent
  branch is untouched. Platform hazards: only `std::min/max`, `QWidget::window()/frameSize()/mapToGlobal` (Qt 5/6, all
  platforms); on Wayland global `move()` is advisory for top-level floats, as it already was for the old placement.

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-window-channel-load.r2/logs/`.

| Step | Command | Exit | Result |
| --- | --- | --- | --- |
| build | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4` (`02-build.log`) | 0 | |
| GREEN family | `cd build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure` (`03-GREEN.log`) | 0 | 100% tests passed out of 2 |
| RED | `git diff --cached -- src/gui/widgets/TextFloat.cpp > logs/fix.patch && git apply -R logs/fix.patch` (EXIT 0), rebuild GrooveMenuTest (`04-build-RED.log`, EXIT 0), `ctest -R '^GrooveMenuTest$' --output-on-failure` (`05-RED.log`) | 8 | `Totals: 4 passed, 3 failed`; `float 902,0 124x80 is not inside the window 0,0 900x600` |
| GREEN | `git apply logs/fix.patch` (EXIT 0), rebuild (`06-build-GREEN.log`, EXIT 0), `ctest -R '^GrooveMenuTest$' --output-on-failure` (`07-GREEN.log`) | 0 | 100% tests passed out of 1 |
| gates (original) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`08-gates.log`) | 1 | PermissionError creating `<worktree>-feature-check` (outside the write-jail) |
| gates (copy) | `python3 logs/feature_check_copy.py <worktree>` (`09-gates.log`; copy differs only in `ROOT` and log dir, diff shown in the run) | 0 | 8/8 PASS, RESULT: PASS |

Files committed: `src/gui/widgets/TextFloat.cpp`, `tests/src/gui/GrooveMenuTest.cpp`, `tests/upstream-modifications.txt`, this report.

## Round 3 (2026-11-09): the regression triggers the real menu action

Luna's round-2 review was ACCEPT-NOTE with one note: `channelLoadFromTheWindowMenuShowsInsideTheApp` called
`runCommand(window, mixer.cpu_load, showResult)` directly instead of triggering the menu action. Addressed. The slot now
builds the product's Window menu with the product's own installer (`installWorkspaceMenus`, `include/WorkspaceMenus.h` - the
function the shell runs on first show), finds the `Channel Load` entry by its text and its `controlCommand`
(`mixer.cpu_load`), and triggers that `QAction`. The entry's host is the main window (`hostOf` walks past the QMenu to
`bar->window()`), so the float is placed by the same path a person's click takes. No product change: the accepted fix is
untouched, and the product diff is still byte-identical to `accepted.patch`.

The only change is the test: the direct `runCommand` call is replaced by `windowMenu()` + `action->trigger()`, with four
extra assertions (menu installed, entry found, entry enabled, command id `mixer.cpu_load`). The accepted design and the other
regression slots are retained. Affected failure paths and platform hazards are unchanged from round 2 (below).

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-window-channel-load.r3/logs/`.

| Step | Command | Exit | Result |
| --- | --- | --- | --- |
| build | `CCACHE_TEMPDIR=<logs>/ccache-tmp cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4` (`build-1.log`, `build-7-GREEN.log`) | 0 | |
| GREEN family | `cd build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure` (`test-7-GREEN.log`) | 0 | 100% tests passed out of 2 (GrooveMenuTest: all 7 slots) |
| RED | `git apply -R logs/fix.patch` (EXIT 0), rebuild zene + GrooveMenuTest (`build-6-RED.log`, EXIT 0), `ctest -R '^GrooveMenuTest$' --output-on-failure` (`test-6-RED.log`) | 8 | `Totals: 4 passed, 3 failed`; `the Channel Load readout lies outside the app` |
| GREEN | `git apply logs/fix.patch` (EXIT 0), rebuild (`build-7-GREEN.log`, EXIT 0), family ctest (`test-7-GREEN.log`) | 0 | 100% tests passed out of 2 |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`gates-2.log`) | 0 | 8/8 PASS, RESULT: PASS |

Files committed this round: `tests/src/gui/GrooveMenuTest.cpp` (strengthened regression), this report. `src/gui/widgets/TextFloat.cpp`
is unchanged from the accepted patch.

## Environment notes (honest)

- Round 2: ccache could not create its temp dir (`/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`,
  outside the write-jail; first build `build-1.log` EXIT=2). Every build in rounds 2 and 3 ran with
  `CCACHE_TEMPDIR=<log folder>/ccache-tmp`.
- Round 2: `feature_check.py` wrote its logs to `<worktree>/../<name>-feature-check`, which the jail forbade
  (`gates-1.log`: PermissionError, EXIT=1), so a copy with a different log dir was used. Round 3: the **original**
  `feature_check.py` ran unchanged and passed (`gates-2.log`, EXIT=0); the log dir was allowed this time. No copy, no
  re-anchoring.

## Self-check

- Regression fails reverted / passes fixed: RED exit 8 (3 failures, message above), GREEN exit 0 - table above.
- Deterministic: no timers, sleeps, races or wall-clock waits in the test. The Window menu is installed synchronously by the
  product's own `installWorkspaceMenus`, the `Channel Load` action is triggered synchronously, and the assertions read widget
  geometry (window resized to 900x600 explicitly; floats deleted at the end of each slot). `installWorkspaceMenus` does arm the
  product's own start-up timers (a 0 ms accessibility fill and a 4 s update check), but neither fires during the test: the slot
  runs synchronously and the process leaves `QTest::qExec` without running the event loop, so they cannot affect an assertion.
- Cause and completeness: the single placement point in `displayMessage` was changed; all callers share it; in-window positions
  are unchanged (QCOMPARE slot). The Channel Load path is exercised through the real menu action (round 3).
- Collateral: no saved data, no limits added; messages that already lay inside the window do not move.
- Audio thread: GUI-only code; nothing realtime touched. Undo/commands: none added or changed.
- Portability: only `std::min/std::max`, `QRect`, `QWidget::frameSize()/window()` (Qt 5 and 6).
