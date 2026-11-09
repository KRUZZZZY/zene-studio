FALSE-POSITIVE

# Cluster gui-1030-menu-navigate-tool-menu-at-pointer - "Tool Menu popup closes with the Navigate menu"

## Verdict

Not a product defect. Luna's stated cause (a popup opened synchronously inside the Navigate action's trigger is closed with the owning menu) is not what happens, and the symptom has a different, intended cause.

- **Normal Zene on a real display (what a user runs):** clicking Navigate > Tool Menu at Pointer opens the popup at the pointer, listing the Arranger tools and the Note editor tools. Reproduced with the real binary and a real X11 click (below).
- **The sweep's Zene:** the sweep starts Zene with `--control-socket` (`tests/control_socket_harness.py:258`, the harness the sweep's `gui.py` uses). `src/core/main.cpp:1248` marks that process an agent instance, which makes the registry headless (`src/core/ControlRegistry.cpp:158`, `src/core/UnattendedRun.cpp:84-87`). In a headless/agent instance `window.tool_menu` deliberately builds no popup and answers `shown:false` with the entries (`src/gui/ControlCommandsArrangerView.cpp:335-338, 359`). The contract is asserted in `tests/control-customisation.py:415-418` (`menu.get("shown") is False`, 14 entries). So "no popup visible" is the specified behaviour of an agent-driven instance, not a menu-dismissal race.

No product code is changed; the commit is this report. The contract test already pins the agent-instance behaviour, so no new test is added. A RED run is not possible, because there is no defective behaviour to fail on.

Base: `2a7fd9f9c7` (`050/m0-train-24`). Branch: `bugs/qwen-gui-1030-menu-navigate-tool-menu-at-pointer`. The branch had no commits of its own before this one.

## Evidence (file:line)

| Claim | Where |
| --- | --- |
| The menu item runs `window.tool_menu` through the generic command action | `src/gui/MenuBarExtras.cpp:146`; `src/gui/CommandRun.cpp:122-131` (`runCommand` -> `ControlRegistry::invoke`) |
| The handler pops a menu only when not headless | `src/gui/ControlCommandsArrangerView.cpp:335` (`shown = !isHeadless()`), `:338` (`if (shown)`), `:357` (`popup`), `:359` (`shown` in the reply) |
| `--control-socket` marks an agent instance | `src/core/main.cpp:1241-1248` (`setAgentInstance(true)`) |
| An agent instance is headless | `src/core/UnattendedRun.cpp:84-87` (`isUnattendedRun() = s_agentInstance \|\| platformHasNoDisplay()`); `src/core/ControlRegistry.cpp:158` |
| The sweep's instance is started with `--control-socket` | `tests/control_socket_harness.py:258` (the sweep's `gui.py` start uses that harness, `terra-pilot/gui.py` docstring) |
| The contract is pinned | `tests/control-customisation.py:415-418` |

## Reproduction (real binary, `build/zene` at the base, Xvnc + Qt xcb, real X11 clicks)

All screenshots are in the log folder `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1030-menu-navigate-tool-menu-at-pointer.r1/logs/`.

| Run | How Zene was started | Action | Result |
| --- | --- | --- | --- |
| A (the sweep's setup) | the sweep's `gui.py start build/zene` (control socket) | click Navigate (`s3-nav.png`), click Tool Menu at Pointer | no popup (`s4-after.png`): matches Luna's finding |
| A2 | same instance | no Navigate menu at all: send `window.tool_menu` on the socket | no popup (`s5-socket.png`); reply ends `"shown":false` |
| B (a user's setup) | plain `build/zene`, no `--control-socket`, throwaway HOME, `QT_QPA_PLATFORM=xcb` on Xvnc `1600x1000` | click Navigate (`p5-nav.png`), click Tool Menu at Pointer | popup at the pointer with the arranger tools (draw, knife, select, range, audition, zoom, smart) and the note editor tools (draw, select, slice, erase, audition, paint, mute) (`p6-after.png`) |

Run A2 is the decisive one against the stated cause: with the Navigate menu not involved at all, the popup is equally absent, and the reply says why (`shown:false`). Run B shows the same click on the same binary works once the instance is not agent-driven.

A bare-Qt check supports the same conclusion. A small scratch program, run on real xcb with real XTest clicks, opened a `QMenu` with `popup(QCursor::pos())` synchronously inside a Navigate-style action's `triggered`, and it stayed visible (`alive=1 visible=1`). Opening it with `QTimer::singleShot(0, ...)` instead behaved the same. The scratch program and its display were deleted afterwards.

### Note

- In run B the section headings ("Arranger", "Note editor") render very faintly, so the two tool groups are separated only by a rule. That is a styling matter outside this finding, and I did not change it.
- If the owner wants Navigate > Tool Menu at Pointer to also pop up in an agent-driven instance on a real display, that is a change to the intended `headless` contract (and to `tests/control-customisation.py:415`). I did not make it. The sweep's own harness is the place to account for it: a click on this item in an agent instance cannot show a popup by design.

## Commands and results

| Step | Command | EXIT |
| --- | --- | --- |
| Sweep-setup repro | `venv/bin/python terra-pilot/gui.py start build/zene --size 1600x1000`, then `gui.py click 196 15`, `gui.py click 240 943`, `gui.py shot` | 0 |
| Socket repro | `python3 -I call.py <zene.sock> window.tool_menu` (reply `"shown":false`) | 0 |
| Plain-instance repro | `HOME=<scratch> QT_QPA_PLATFORM=xcb DISPLAY=:97 build/zene` (Xvnc `:97`, `1600x1000`), then XTest clicks on Navigate and on Tool Menu at Pointer | 0 |
| Contract test | `cd tests && QT_QPA_PLATFORM=offscreen python3 control-customisation.py ../build/zene > .../logs/contract-1.log 2>&1` (line 49: `set_tool and the tool menu ok`) | 0 |

The first `gui.py start` attempts failed with `ModuleNotFoundError: vncdotool` because they used the system Python; the sweep's `venv/bin/python` works (`gui-start-1.log`, `gui-start-2.log`, `gui-start-3.log`).

| Gates | `python3 .../qwen/feature_check.py <worktree>` as shipped | 1 - `PermissionError`: it creates `<worktree>-feature-check/` beside the worktree, outside this lane's write area (`gates-1.log`); no gate ran |
| Gates | the same script, copied to the log folder with only two path lines changed (the log directory, and `ROOT` pointing at the real `qwen/` folder); gate commands and baselines unchanged | 0 (`gates-2.log`): fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS; `RESULT: PASS` |

Not run: `ctest`, because no C++ changed.

## Self-check

- **Regression test fails with the change reverted and passes with it:** not applicable; there is no product change and no defect to turn RED. The behaviour the verdict relies on (`shown:false` for an agent instance, 14 entries) is pinned by `tests/control-customisation.py:415-418`, which passes (EXIT=0, above).
- **Deterministic test:** no new test; the pinned contract is a socket round trip with no timing.
- **Fix at the cause, siblings:** no fix. Siblings checked: `rg -n "new QMenu|->popup\(|->exec\(QCursor" src/gui/Control*.cpp` finds only `ControlCommandsArrangerView.cpp:340` and `:357`, so `toolMenu` is the only control-command handler that opens a popup menu and there is no sibling to keep consistent.
- **Collateral damage:** none; no product file changed.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** the table above, citing `ControlCommandsArrangerView.cpp:335-338`, `main.cpp:1248`, `UnattendedRun.cpp:84-87`, `ControlRegistry.cpp:158`, `control-customisation.py:415-418`, and the three reproduction runs.
