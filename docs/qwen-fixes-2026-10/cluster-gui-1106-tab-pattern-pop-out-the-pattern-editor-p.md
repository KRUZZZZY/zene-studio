Verdict: FALSE-POSITIVE

gui-1106-tab-pattern-pop-out-the-pattern-editor-p: FALSE-POSITIVE (a sweep-harness artifact: the click never reached the button)

## Evidence

**1. The sweep clicked outside its own screen.** The sweep runs Zene on a virtual screen of `SIZE = "1600x1000"`
(`~/.local/share/zene-bunny-fix/terra-pilot/buttons/sweep.py:59`). In `runs/m0-train-26/results.jsonl` the target
`tab-pattern | button | Pop out the Pattern Editor panel` has click centre **x=1657, y=712**. x=1657 is beyond the 1600-pixel
screen, so the click landed on nothing. The `before` and `after` screenshots (`shots/0159-*.png`) are 1600x1000 and
the pop-out button is not in either of them. The main window is wider than the screen: the same run lists 19 controls
with centres at x>=1600 or y>=1000, including the footer's `Browser` toggle at (1778, 985). The same coordinates
(1657, 712) appear for the Devices and Automation pop-outs (and 1717, 712 for Mixer), so those verdicts share this cause.
The harness has since been fixed: `click_problem()` in the current `sweep.py:230-244` refuses a control whose centre is
`outside the 1600x1000 screen`. The run in the finding predates that check.

**2. The in-screen pop-outs in the same run did float.** The Arranger pop-out was clicked at (1257, 99), inside the
screen. Judge verdict `FAIL` said "remains docked", yet the `after` half of `shots/0039-pair.png` shows the Arranger
button relabelled **Dock** (`Shell::syncFloatButton`, `ShellFloating.cpp:437-442` sets "Dock" only when `isDetached(id)`).
So a real click through the same `dispatchFloat` path does float the panel. The floating window is a separate top-level
window that the screenshot does not capture. That is the Arranger's separate cluster, and I did not touch it.

**3. The command behind the button works for `pattern`.** The button calls `Shell::dispatchFloat`
(`src/gui/shell/ShellFloating.cpp:220-231`), which calls `window.detach {panel: id}` through `ControlRegistry::invoke`.
The handler `movePanel` (`src/gui/ControlCommandsLayouts.cpp:228-244`) accepts any id in `ShellLayout::panels()`, and
`pattern` is one (`src/gui/shell/ShellLayout.cpp:82`). `Shell::detachPanel` (`ShellFloating.cpp:237-265`) needs only a
mounted frame, which exists for every spec (`Shell.cpp:217-222`). The result of `invoke` is discarded at
`ShellFloating.cpp:229`, as Luna says, but I traced a real instance and no rejection occurs:

Commands run against the real `build/zene` (headless, `QT_QPA_PLATFORM=offscreen`) through `tests/control_socket_harness.py`,
script `logs/probe.py`, output `logs/probe-2.log`:

    pattern detach -> {'changed': True, 'floating': True, 'panel': 'pattern'}
    pattern state  -> floating: True  (window.get_state)
    pattern attach -> {'changed': True, 'floating': False, 'panel': 'pattern'}

`mixer`, `devices`, `automation` and `browser` return the same shape.

**4. Nothing in the click path eats the click.** `ShellTearOff` installs its event filter only on the panel header
(`ShellTearOff.cpp:169-170`), not on the header's child buttons, so a press on the pop-out `QToolButton` reaches the
button and its `clicked` signal is connected to `dispatchFloat` (`ShellFloating.cpp:424`).

## Commands and exit codes

| Evidence | Command | Result |
| --- | --- | --- |
| Probe a real instance (build/zene was already built from this tree; no source changed) | `QT_QPA_PLATFORM=offscreen timeout 200 python3 -I $LOGS/probe.py build/zene > $LOGS/probe-2.log 2>&1` (run from the worktree) | EXIT=0; detach/attach of `pattern` both succeed |
| Gates | `python3 $LOGS/feature_check_copy.py <worktree> > $LOGS/feature-check-2.log 2>&1` | EXIT=0; all eight rows PASS |
| Completion check | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> gui-1106-tab-pattern-pop-out-the-pattern-editor-p > $LOGS/submit-check-final.log 2>&1` | run after the commit; its output and exit code are in `logs/submit-check-final.log` |

`$LOGS` is `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-tab-pattern-pop-out-the-pattern-editor-p.r1/logs`.

**Gate-script note.** The stock `feature_check.py` creates its log folder next to the worktree
(`<worktree>-feature-check`), which the write-jail forbids (`PermissionError`, EXIT=1, `logs/feature-check-1.log`). I ran a
copy (`logs/feature_check_copy.py`) that differs from the original in exactly three lines: `ROOT` is pinned to the qwen
directory and the log folder is `logs/feature-check-logs`. The gates themselves and their commands are the original's.

## Not fixed on purpose, and what I did not verify

- No product change and no new test. The step-2 not-real branch applies, and there is no behaviour to pin: the code path
  already works. A test written against it would pass without any change, which is exactly what the procedure rejects.
- I did not click the real button under a display. I have no X11/Xvnc driver here (no `xdotool`). The click path is
  covered by reading (items 2 and 4) plus the live command run (item 3), not by an executed click.
- A different, real observation that this run exposed: `MainWindow` is created at `resize(1600, 1000)`
  (`src/gui/MainWindow.cpp:190`), but its content is wider and taller than that on the sweep's 1600x1000 screen (controls
  at x up to 1778, y up to 1153). Whether the shell's minimum size is too large for a 1600x1000 display is a separate
  question and is not part of this finding. I left it alone and did not measure the minimum size.

## Self-check

- **Regression test fails without the change and passes with it:** not applicable; the verdict is FALSE-POSITIVE, there is
  no product change and no test. Evidence that nothing needs fixing is items 1-4 above.
- **Deterministic test:** no test added; the probe (`logs/probe.py`) is a one-off diagnostic and is not committed.
- **Fix at the cause, callers and siblings checked:** no fix. I checked every caller of `dispatchFloat`
  (`ShellTearOff.cpp:235,340,342,395`, `ShellFloating.cpp:184,215,424`) and every sibling pop-out (`mixer`, `devices`,
  `automation`, `browser` detach fine on the live instance; Arranger visibly floats in `shots/0039-pair.png`).
- **No collateral damage:** nothing changed in `src/`, `include/` or `tests/`; saved data and old projects are untouched.
- **ALREADY-FIXED / FALSE-POSITIVE contract and file:line:** `sweep.py:59` (1600x1000 screen), `results.jsonl` (click at
  x=1657), `sweep.py:230-244` (current guard), `ShellFloating.cpp:220-265`, `ControlCommandsLayouts.cpp:228-244`,
  `ShellLayout.cpp:82`, `ShellTearOff.cpp:169-170`.

Files committed for this verdict: `docs/qwen-fixes-2026-10/cluster-gui-1106-tab-pattern-pop-out-the-pattern-editor-p.md`;
no production file changed.
