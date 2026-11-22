FALSE-POSITIVE

# Cluster gui-1106-tab-automation-pop-out-the-automation-pa: "Automation panel pop-out silently fails"

Verdict: **FALSE-POSITIVE.** The pop-out button and `window.detach` both float the Automation panel. In the GUI sweep
the click never reached the button: the sweep clicked a point outside the virtual screen and hit the panel header.
No product code was changed. A regression test that pins the button path was added.

## Evidence

### 1. The product path works, both ways into it

- Button: `Shell::addFloatButton` connects the click to `dispatchFloat(id, !isDetached(id))`
  (`src/gui/shell/ShellFloating.cpp:424`). With a main window, `dispatchFloat` calls `window.detach`
  (`ShellFloating.cpp:229-230`). That handler is `movePanel` → `detachTo` → `Shell::detachPanel`
  (`src/gui/ControlCommandsLayouts.cpp:220-244`). With no main window it calls `detachPanel` directly
  (`ShellFloating.cpp:223-227`).
- `window.detach` is a GUI command with `requiresEngine = false` and no `requiresDecl`
  (`src/gui/ControlGuiSupport.cpp:34-43`). So `ControlRegistry::invoke` has no Busy/Requires refusal for it on an
  interactive run (`src/core/ControlRegistry.cpp:275-295`). `automation` is a registered panel
  (`src/gui/shell/ShellLayout.cpp:81`), so `isPanel` accepts it (`ControlCommandsLayouts.cpp:234`).
- `detachPanel` titles the window `"%1 - Zene Studio"` (`ShellFloating.cpp:251`), which gives "Automation - Zene Studio".
- **Live run (real `build/zene`, headless, control socket):** `window.detach {"panel":"automation"}` answered
  `{"changed": true, "floating": true, "panel": "automation"}`. `window.get_state` then listed
  `('automation', True, ...)`. Mixer, devices, pattern, detail and arranger gave the same result. Script:
  `logs/probe.py`, output `logs/probe-1.log`.
- **Button path, in-process:** the new `ShellTest::theAutomationPopOutButtonFloatsItAndDocksItBack`
  (`tests/src/gui/ShellTest.cpp:308`) does three things:
  - finds the button by its accessible name "Pop out the Automation panel" and clicks it once;
  - asserts the panel is detached, its window is visible and titled "Automation - Zene Studio";
  - clicks again ("Dock") and asserts it is docked. It passes.

### 2. Why the sweep saw "remains docked": the click landed off the button

Sweep run: `~/.local/share/zene-bunny-fix/terra-pilot/buttons/runs/m0-train-26`.
- The screenshots are 1600x1000 (`shots/0136-before.png`/`-after.png`; `sweep.py:59` `SIZE = "1600x1000"`). The click
  row in `results.jsonl` is at **x=1657, y=712**, which is outside the screen.
- In `shots/0136-after.png` the pointer sits at the right screen edge on the Automation header, showing the
  tear-off grip's hand cursor. There is no float button in that strip (crop: `logs/crop-after.png`). One click on the
  header does nothing, by design: a double-click or a drag floats (`ShellFloating.cpp:443-445`). So the panel stayed
  docked.
- This failure is systematic, not specific to Automation. **Every** panel pop-out in that run has a FAIL or UNSURE
  verdict: Note Editor, Arranger, Mixer, Devices, Automation and Pattern Editor are FAIL; Browser and Inspector are
  UNSURE. Several of their clicks were at x > 1600, and the inventories place the same buttons at different x
  (Browser at 1539 or 1939; Automation at 1257 or 1657).
- That run used an older sweep. Its rows have no `target_before`, and the current `sweep.py` records one and refuses
  a target whose centre is off screen: `click_problem`, `sweep.py:230-242`, "control centre (...) is outside the
  1600x1000 screen".

The "root cause (Luna)" note says a rejected detach is silent. The detach is not rejected here: it succeeds live and
in the test, and its result is only discarded on a path that does not fail for a valid panel id.

### Separate lead (not this cluster, not changed)
The AT-SPI inventories place the right-hand panel buttons past x=1600 in some launches. That suggests the main window
sometimes opens wider than a 1600-px screen. If real, it is a separate window-geometry issue. It is unverified here
and I did not chase it.

## Commands run (exact, with exit codes; LOGS = `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-tab-automation-pop-out-the-automation-pa.r1/logs`)

| Step | Command | EXIT |
|---|---|---|
| Live probe | `cd tests && QT_QPA_PLATFORM=offscreen timeout 120 python3 LOGS/probe.py ../build/zene . > LOGS/probe-1.log 2>&1` | 0 (automation detached: `changed:true, floating:true`) |
| Build (1st try) | `cmake --build build --target ShellTest -j4 > LOGS/build-1.log 2>&1` | 2: ccache temp dir outside the write jail (`Permission denied`), not a code error |
| Build | `CCACHE_DISABLE=1 cmake --build build --target ShellTest ShellLayoutTest ShellHomesTest -j4 > LOGS/build-2.log 2>&1` | 0 |
| Family | `cd build/tests && ctest -R '^Shell' --output-on-failure > LOGS/test-2.log 2>&1` | 0: 3/3 tests (ShellLayoutTest, ShellTest, ShellHomesTest) |
| New test alone | `cd build/tests && QT_QPA_PLATFORM=offscreen ./ShellTest theAutomationPopOutButtonFloatsItAndDocksItBack > LOGS/test-3.log 2>&1` | 0 (PASS) |
| Gates (as given) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1: `PermissionError` creating `<worktree>-feature-check` (outside the write jail), no gate ran |
| Gates (logs redirected) | `python3 LOGS/feature_check_local.py <worktree> > LOGS/gates-2.log 2>&1` (the same script, with only the log dir moved to `LOGS/feature-check` and `ROOT` pinned to the original; diff in this session) | 0: all 8 rows PASS |

Files committed: `tests/src/gui/ShellTest.cpp`,
`docs/qwen-fixes-2026-10/cluster-gui-1106-tab-automation-pop-out-the-automation-pa.md`.

## Self-check

- **Verdict with file:line or contract:** FALSE-POSITIVE. See section 1: `ShellFloating.cpp:220-231,237-265,424`,
  `ControlCommandsLayouts.cpp:220-244`, `ControlGuiSupport.cpp:34-43`, `ControlRegistry.cpp:275-295`. Section 2
  covers the sweep's off-screen click: `results.jsonl` x=1657 on a 1600 screen, and `sweep.py:230-242`.
- **Regression test fails without the fix / passes with it:** there is no product fix, so there is nothing to revert.
  The added test is a guard: it passes on the unchanged product (`test-3.log`, EXIT 0). That is the evidence the
  behaviour already works. It is not a red/green proof of a fix.
- **Deterministic:** yes. It makes a direct `QToolButton::click()` on an in-process `Shell` (offscreen QPA), with no
  timers, sleeps or waits on the window manager.
- **Fix at cause / callers and siblings:** no product change. The sibling entry points into the same function were
  checked: the button (`ShellFloating.cpp:424`), the panel menu action (`ShellTearOff.cpp:394-395`), the shortcut
  (`ShellFloating.cpp:210-216`) and the command (`window.detach`, live probe). All reach `detachPanel`.
- **Collateral damage / saved data:** none. Only a test was added.
- **Realtime / undo / commands:** nothing touched. `window.detach` is non-mutating (`ControlCommandsLayouts.cpp:351`).
  No command was added, so the snapshot canary is unchanged.
- **Registration:** no new file. `ShellTest.cpp` is already registered (`tests/CMakeLists.txt:1232`), and the gates
  pass (fork-sources, all-sources-reproduce).
- **GUI path tested:** yes, `tests/src/gui/ShellTest.cpp` drives the actual pop-out button.
- **Gates row in `submit_check.py`:** it reports FAIL because it calls the original `feature_check.py`. That script
  `mkdir`s `<worktree>-feature-check`, a sibling of the worktree outside this session's write jail, and dies with
  `PermissionError` before running any gate (`LOGS/gates-4.log`, EXIT 1). The same script with only its log dir
  moved into LOGS passes all 8 rows on the committed tree (`LOGS/gates-3.log`, EXIT 0, `RESULT: PASS`). This is an
  environment limit, not a gate failure. Re-run the original once the directory can be created.
- **Not checked:** the GUI sweep was not re-run with the current `sweep.py`. The full ctest suite was not run, only
  the `^Shell` family.
