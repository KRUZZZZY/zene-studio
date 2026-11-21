FALSE-POSITIVE

# cluster gui-1106-tab-devices-pop-out-the-devices-panel - "Devices pop-out failure is silently discarded"

Branch `bugs/qwen-gui-1106-tab-devices-pop-out-the-devices-panel`, base `af010bc3c` (050/m0-train-26).

## Verdict: FALSE-POSITIVE

The button works. The sweep's click missed it: the click landed off-screen. The "refusal is discarded" path that
Luna named as the root cause cannot be reached from this button. Luna's own triage says "the reported no-op has no
valid docked-state path in the source".

### 1. The sweep never clicked the button (sweep evidence, m0-train-26)

- `inv-tab-devices.jsonl:86`: the button "Pop out the Devices panel" is inventoried at x=1257, y=712, 31x28.
- `results.jsonl:130`: the sweep clicked it at **x=1657**, y=712. That is 400 px right of the inventoried button. The
  "Pin" control in the same header also shifted +400. The tab-bar controls before them were clicked within 2 px of their
  inventory positions, apart from one at -38.
- `shots/0129-before.png` and `shots/0129-after.png` are 1600x1000, so the screen is 1600 px wide and x=1657 is off-screen.
  In the after-shot the pointer sits at the right edge, about x=1597, so it was clamped to the screen edge. The main window
  runs past the right edge of the screen: the arranger toolbar is cut off, and the Devices header's Pin and pop-out
  buttons are not visible. So no click reached the button, and Devices stayed docked as it should.

### 2. The source: every failure the discarded result could carry is unreachable from this button

The click goes through `Shell::addFloatButton` → `dispatchFloat(id, !isDetached(id))` at
`src/gui/shell/ShellFloating.cpp:424`. Then `dispatchFloat` (`ShellFloating.cpp:220-231`) either calls `detachPanel`
directly (no MainWindow) or calls `ControlRegistry::invoke("window.detach", {panel: id})`. Each way `invoke` can fail:

| Failure in `ControlRegistry::invoke` / handler | Why it cannot happen for this click |
| --- | --- |
| unknown command (`src/core/ControlRegistry.cpp:268-273`) | `window.detach` is always registered (`src/gui/ControlCommandsLayouts.cpp:346-352`) |
| `requires` unmet / engine not ready (`ControlRegistry.cpp:275-295`) | `guiCommand` sets `requiresEngine = false` and no requires (`src/gui/ControlGuiSupport.cpp:34-43`) |
| schema invalid (`ControlRegistry.cpp:281-285`) | `panel` is a string and present; `screen` is not passed |
| collaboration refusal (`ControlRegistry.cpp:312`) | `window.detach` is registered non-mutating (`ControlCommandsLayouts.cpp:351`, `false`); a non-mutating command routes `Local` for every role (`src/core/CollabPolicy.cpp:204` and `:219`) |
| `shell() == nullptr` → `noInterface` (`ControlCommandsLayouts.cpp:231`) | `dispatchFloat` only calls `invoke` when `main->shell() == this` (`ShellFloating.cpp:222-228`) |
| `!ShellLayout::isPanel(panel)` → `unknownPanel` (`ControlCommandsLayouts.cpp:233`) | the id is the frame's own `frame->id()` (`ShellFloating.cpp:424`) |
| `detachPanel` false → `Refused` (`ControlCommandsLayouts.cpp:220-224, 240`) | `detachPanel` returns false only when `m_panels` has no frame for the id (`ShellFloating.cpp:239-244`); the button is a child of that frame |

The only remaining way out is an exception, and `invoke` turns that into a typed error.

So nothing here needs fixing. Adding a GUI surface for a refusal that cannot happen would change behaviour outside the
bug. The off-screen window seen in the screenshots (the main window wider than the 1600 px screen) is a separate issue
in the sweep's environment or in window sizing. I did not investigate it, so it is unverified and not part of this finding.

### 3. Contract test (added; passes on the unchanged product code)

`tests/src/gui/ShellTest.cpp:402` `theDevicesPopOutButtonFloatsAndDocksDevices` clicks the Devices panel's `panelFloat`
button, whose accessible name is "Pop out the Devices panel". It asserts that Devices floats in a window titled exactly
"Devices - Zene Studio" and that the button then reads "Dock the Devices panel". A second click docks it again. It is
deterministic: plain `QToolButton::click()` with no timers or sleeps. The test's `World` has no MainWindow, so it goes
through `dispatchFloat`'s direct path (`ShellFloating.cpp:223-227`). The registry path ends in the same `detachPanel`
(`ControlCommandsLayouts.cpp:222`).

No product code changed, so there is no RED/GREEN pair and no `fix.patch`. This test pins the contract the finding
expected.

## Commands and exit codes

| Step | Command | EXIT |
| --- | --- | --- |
| build (1st try) | `cmake --build build --target zene ShellTest -j4 > LOGS/01-build.log 2>&1` | 2: ccache could not create its temp file in `~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6` (sandbox permission), not a compile error |
| build | `CCACHE_TEMPDIR=<run>/ccache-tmp cmake --build build --target zene ShellTest -j4 > LOGS/02-build.log 2>&1` | 0 |
| test (from build/tests) | `ctest -R '^ShellTest$' --output-on-failure > LOGS/03-test.log 2>&1` | 0 (1/1 ctest) |
| test verbose | `ctest -R '^ShellTest$' -V > LOGS/04-test-verbose.log 2>&1` | 0: `Totals: 25 passed, 0 failed`; `PASS : ShellTest::theDevicesPopOutButtonFloatsAndDocksDevices()` |
| gates (direct) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/05-gates.log 2>&1` | 1: `PermissionError` creating `<worktree>-feature-check` (the sandbox cannot write next to the worktree); no gate ran |
| gates (log dir redirected) | `python3 <run>/fc_wrapper.py <worktree> <run>/feature-check > LOGS/06-gates.log 2>&1` | 0: all 8 rows PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

`LOGS` = `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-tab-devices-pop-out-the-devices-panel.r1/logs`.
`fc_wrapper.py` runs `feature_check.py` exactly as written, except that `logs = wt.parent / ...` is replaced with a
log directory given as an argument.

Files committed: `tests/src/gui/ShellTest.cpp`,
`docs/qwen-fixes-2026-10/cluster-gui-1106-tab-devices-pop-out-the-devices-panel.md`.

## Self-check

- **Regression test FAILS with the product change reverted and PASSES with it:** there is no product change (verdict
  FALSE-POSITIVE), so there is nothing to revert. The added contract test passes on the unchanged code (EXIT 0, 25/25).
  It is a guard against future breakage, not a regression proof.
- **Deterministic:** yes. It uses synchronous `QToolButton::click()` and direct state queries (`isDetached`,
  `floatingWindow`), with no wall-clock timing, sleeps or event-loop waits.
- **Fix at the cause and complete; callers and siblings checked:** no fix. I checked every `dispatchFloat` caller:
  `ShellFloating.cpp:184, 215, 424` and `ShellTearOff.cpp:235, 340, 342, 395`. All of them pass an existing panel's id,
  so the same table in section 2 applies to each. The sibling `window.attach` (`attachPanel`) returns false only when
  the panel is not floating, and callers only ask to dock a floating panel.
- **No collateral damage:** only a test was added; no product code, saved data or project loading changed.
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** section 1 (sweep coordinates `results.jsonl:130` vs
  `inv-tab-devices.jsonl:86`; 1600 px screenshots) and section 2 (file:line table showing that no failure path is
  reachable).
- **Sibling commands:** none changed.
