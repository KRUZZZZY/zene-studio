FALSE-POSITIVE

# cluster gui-1106-main-browser — "Browser footer switch does not close the Browser"

## Verdict

FALSE-POSITIVE. The footer switch and `Shell::togglePanel` work. The sweep's click landed
**outside the screen**: the Browser footer switch was at x=1778 on a 1600x1000 screen, because the
main window had grown wider than the screen by that point in the sweep run. No product line in the
finding's path is wrong, so there is no fix and no regression test (a claim-only closure).

## Evidence

Sweep run `~/.local/share/zene-bunny-fix/terra-pilot/buttons/runs/m0-train-26`:

1. **The screen is 1600x1000.** `sweep.py:59` sets `SIZE = "1600x1000"`; `shots/0085-before.png` is
   1600x1000 (read with PIL: `(1600, 1000)`).
2. **The click was at x=1778.** `results.jsonl`, id `main|check box|Browser|...Footer`: `"x": 1778,
   "y": 985, "outcome": "clicked"`. That is 178 px right of the screen's edge. In the inventory taken
   earlier on the fresh window (`controls.jsonl:86`) the same switch was at x=1580, on screen.
3. **The pair screenshot shows it.** `shots/0085-pair.png`: the Browser panel is cut off at the right
   edge of the screen and the footer ends at "TASKS". The Arrange/Mix/Edit/Browser switches are not
   drawn on screen at all.
4. **Same pattern for every control beyond x=1600.** Click x and judge verdict for the footer
   (`results.jsonl` / `verdicts.jsonl`): Inspector 48 PASS, Mixer 137 PASS, Devices 306 PASS,
   Automation 394 PASS, Pattern Editor 497 PASS, Clip Launcher 619 PASS, Tasks 1573 PASS; then
   Arrange 1635 UNSURE, Mix 1689 FAIL, Edit 1729 FAIL (the sibling finding "Edit footer switch does
   not activate the Edit view", `triage-9.md` line 1), Browser 1778 FAIL. (Note Editor 219 also
   FAILed; that is a different control and not part of this finding.) Every switch that was on
   screen passed; every switch off screen failed. Luna's own root cause says source inspection does
   not explain the no-op ("more runtime context is needed").
5. **The code path is correct.**
   - Footer action: `Shell.cpp:274-277` → `dispatchWindowToggle(id)` then `syncActions()`.
   - Handler: `ControlCommandsWindow.cpp:132-144` (`window.toggle`) → `MainWindow::togglePanel`
     (`MainWindow.cpp:1278`) → `Shell::togglePanel` (`Shell.cpp:397-409`).
   - `Shell::togglePanel`: Browser is not detached (`Shell.cpp:404`), is offered in every view
     (`ShellLayout.cpp:147-153`), so `m_layout.toggle` runs (`Shell.cpp:406`).
   - `ShellLayout::toggle` (`ShellLayout.cpp:281-292`) → `close` (`:256-279`) removes the Right region's
     open panel and its pin.
   - `apply()` (`Shell.cpp:319-346`) parks the frame and hides the empty host; `syncActions()`
     (`Shell.cpp:370-381`) unchecks the switch.
6. **Run-time confirmation.** A throwaway probe slot in `ShellTest` (not committed; removed with
   `git checkout -- tests/src/gui/ShellTest.cpp`) built a `Shell` at 1600x1000, checked the Browser
   action is checked and its content visible, called `togglePanel("browser")`, and verified it returns
   false, the action and its `ShellButton` footer switch are unchecked and the content is hidden.
   It passed, so the toggle works when the switch is reachable.

## What is real but outside this finding (unverified, not fixed here)

Why the window was wider than the screen is not proven by me. The "before" shot has the Clip Launcher
(360 px host, `Shell.cpp:204-205` region minimums) and the Pattern Editor open when the window had
grown, which points to the content's minimum widths exceeding 1600 px once the launcher is open
beside the arranger. If the owner wants a main window that always fits a 1600 px screen, that is a
separate window-sizing change with a design choice (which minimum to give up); it is not a
`togglePanel` bug, so I did not touch it. The sweep itself could also skip controls whose
centre is outside the screen, or re-run those after resetting the layout.

## Commands run (all in this worktree; logs in the run's `logs/` folder)

| step | command | EXIT |
| --- | --- | --- |
| probe build (first try) | `cmake --build build --target ShellTest -j4 > logs/build-1.log 2>&1` | 2 (ccache could not write its temp dir `~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`, "Permission denied", outside my write area; not a code error) |
| probe build | `CCACHE_DISABLE=1 cmake --build build --target ShellTest -j4 > logs/build-2.log 2>&1` | 0 |
| probe run, family | `cd build/tests && ctest -R '^ShellTest$' --output-on-failure > logs/test-2.log 2>&1` | 0 (1 test run, passed) |
| probe slot only | `QT_QPA_PLATFORM=offscreen ./ShellTest zzProbeBrowserSwitchCloses > logs/probe-1.log 2>&1` | 0 (`PASS : ShellTest::zzProbeBrowserSwitchCloses()`, 3 passed 0 failed) |
| revert probe | `git checkout -- tests/src/gui/ShellTest.cpp` | 0 (`git status` clean) |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gates-1.log 2>&1` | 1: `PermissionError` creating `<worktree>-feature-check` (a sibling directory outside my write area); the gates did not run. This commit changes only this report. |

There was no product change, so there is no RED/GREEN pair (step 4b) and no `fix.patch`.

## Self-check

- *Regression test fails reverted / passes with it:* not applicable, there is no product change and
  no new test (FALSE-POSITIVE). The throwaway probe passed on the unmodified code, which is the
  evidence that the behaviour already holds; it was not committed.
- *Deterministic test:* nothing committed. The probe used no timers, sleeps or injected clocks.
- *Fix at the cause, callers/siblings:* nothing changed. I read every caller of the same path:
  `dispatchWindowToggle` is also used by `ShellRegionTabs.cpp:202`, `ShellFloating.cpp:181`,
  `MainWindow.cpp:155,1375`, all of which end in `window.toggle` → `Shell::togglePanel`.
- *Collateral damage:* none, the diff is this report only.
- *ALREADY-FIXED / FALSE-POSITIVE code or contract:* points 1-6 above; the key facts are
  `results.jsonl` click x=1778 versus the 1600 px screen (`sweep.py:59`, `0085-pair.png`) and the
  correct path at `Shell.cpp:274-277,397-409`, `ShellLayout.cpp:256-292`.
