FALSE-POSITIVE

# cluster gui-1105-main-pop-out-the-inspector-panel - "Inspector pop-out opens over and obscures the workspace"

Finding: in the GUI sweep of 050/m0-train-25, clicking "Pop out the Inspector panel" in a fresh
project left the Inspector "across the left side ... obscuring much of the workspace without a
clearly separate window". Luna's root cause was that the window opens at the Inspector's docked
screen position and is at least 480 x 320 (`src/gui/shell/ShellFloating.cpp:260`).

## Verdict: false positive. The code does what its contract says; the sweep environment made it look wrong

1. **The pop-out is its own top-level window.** `ShellFloating.cpp:249` creates
   `new QWidget(this, Qt::Window)` and `:251` titles it "Inspector - Zene Studio". The panel is
   reparented into that window (`:255-257`), and the window is shown (`:261`). The existing
   `ShellTest::aFloatingPanelDocksBackIntoItsZone` (`tests/src/gui/ShellTest.cpp:264-277`) checks
   that the panel shows in `floatingWindow(...)` and is visible there. It passes (see the table below).
2. **Its place and size are the documented contract, not a slip.**
   - Place: `ShellFloating.cpp:246` says "It opens where it was (or where a drag dropped it): a
     pop-out never sends the panel far from the eye." Line `:248` does exactly that.
   - Size: `include/Shell.h:158-159` says "A floating window's size: the panel's, at least
     480 x 320 and at most 1200 x 800". `ShellFloating.cpp:193-196` implements it, and the
     tear-off drag preview uses the same size (`src/gui/shell/ShellTearOff.cpp:275`), so the
     preview and the window match.
   - `docs/UI-SHELL.md:150-182` (the tear-off section) describes floating windows that the user
     moves, docks back, or sends to another monitor. Nothing in it says a pop-out must avoid
     the main window.
3. **The sweep had no window manager, and the main window filled the screen.**
   - `~/.local/share/zene-bunny-fix/terra-pilot/gui.py:84-89` starts Zene on Xvnc with
     `QT_QPA_XCB_GEOMETRY=1600x1000+0+0` and the comment "no window manager: Zene's window is
     placed to fill the screen". The screenshots are 1600 x 1000
     (`runs/m0-train-25/shots/0012-before.png` / `-after.png`).
   - Without a window manager, no window gets a frame or a title bar. So the floating Inspector
     (480 x 800 at the panel's old top-left) shows no border that marks it as separate. That is
     the "without a clearly separate window" in the finding.
   - Because the main window fills the whole and only screen, *every* spot on that screen
     overlaps the main window. "Obscures the workspace" is unavoidable there, whatever placement
     rule is used. Changing the placement could not change the sweep's after-shot. It would only
     break the stated "near the eye" contract for real desktops.
   - The after-shot shows the parts of the change that *are* specified: the Inspector's dock slot
     closed, the arranger moved left, and the header button now reads **Dock**
     (`docs/UI-SHELL.md:168`).

No product code changed, so there is no regression test. A test would have to pin a behaviour
change that the contract above does not ask for. Whether a pop-out should prefer a second screen,
or a free spot beside the main window, is a design choice for the owner. It is not a defect.

## Commands run (exact, with exit codes)

| Step | Command | EXIT |
| --- | --- | --- |
| build (ccache temp dir not writable in this jail) | `cmake --build build --target ShellTest -j4 > logs/build-1.log 2>&1` | 2 (ccache: Permission denied, not a compile error) |
| build | `CCACHE_DISABLE=1 cmake --build build --target ShellTest -j4 > logs/build-2.log 2>&1` | 0 |
| Shell family | `ctest -R '^Shell' --output-on-failure > logs/test-2.log 2>&1` (from build/tests) | 8: ShellTest Passed; ShellLayoutTest and ShellHomesTest "Not Run" (not built in this lane, unrelated to the finding) |
| existing float test | `ctest -R '^ShellTest$' -V > logs/test-3.log 2>&1` (from build/tests) | 0 (1 test, Totals: 24 passed, 0 failed, including `aFloatingPanelDocksBackIntoItsZone`) |
| gates (stock script) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1` | 1: PermissionError creating `<worktree>-feature-check` (a sibling of the worktree, outside this lane's write-jail); no gate ran |
| gates (same script, only `ROOT` pinned to `~/.local/share/zene-bunny-fix/qwen` and `logs` moved to the run's logs folder; diff in Self-check) | `python3 -I logs/feature_check_logs.py <worktree> > logs/gates-2.log 2>&1` | 0: RESULT PASS, 8/8 (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

The logs are in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-main-pop-out-the-inspector-panel.r1/logs/`.

Files committed: this report only.

## Self-check

- **Regression test fails without the fix and passes with it:** not applicable. The verdict is
  FALSE-POSITIVE, no product file changed, so there is no fix to revert (step 4b has nothing to
  apply). The behaviour the finding questions is already covered by
  `ShellTest::aFloatingPanelDocksBackIntoItsZone` (`tests/src/gui/ShellTest.cpp:264-277`).
  It passes: `logs/test-3.log`, EXIT=0, Totals: 24 passed, 0 failed.
- **Deterministic:** no new test. The existing test cited calls `Shell::detachPanel` directly
  under `offscreen`, with no timers or sleeps.
- **Fix at the cause, callers and siblings checked:** no change. I read every path into
  `detachPanel`: the header button and Ctrl+Shift+F through `dispatchFloat`
  (`ShellFloating.cpp:219-231`), `window.detach` (`src/gui/ControlCommandsLayouts.cpp:222`), a
  saved layout's `floatOne` (`ShellFloating.cpp:399-415`, which passes its saved geometry), and the
  tear-off drop through `setNextFloatGeometry`. All of them share the documented placement and
  `floatSize` contract, so leaving it unchanged keeps them consistent.
- **No collateral damage:** nothing changed in product code, saved layouts or project data.
- **FALSE-POSITIVE evidence:**
  - Top-level window: `src/gui/shell/ShellFloating.cpp:249-251`.
  - Placement contract: `ShellFloating.cpp:246-248`.
  - Size contract: `include/Shell.h:158-159` and `ShellFloating.cpp:193-196`, the same size as
    the drag preview (`ShellTearOff.cpp:275`).
  - Sweep environment: no window manager, and the main window fills the only 1600x1000 screen
    (`~/.local/share/zene-bunny-fix/terra-pilot/gui.py:84-89`). So the window has no frame, and
    any placement on that screen overlaps the main window.
- **Gates:** the stock `feature_check.py` cannot create its log folder from inside this lane's
  jail (`logs/gates-1.log`, EXIT=1). The copy I ran differs only in two lines:
  `ROOT = Path("/home/kruzzzzy/.local/share/zene-bunny-fix/qwen")` (so `FIX-BASE` and
  `gate-baselines` resolve as the original does) and the `logs` path. Result: EXIT=0, RESULT
  PASS (`logs/gates-2.log`). No gate, limit or baseline was changed.
