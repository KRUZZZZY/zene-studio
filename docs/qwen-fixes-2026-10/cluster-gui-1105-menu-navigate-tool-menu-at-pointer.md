FALSE-POSITIVE

gui-1105-menu-navigate-tool-menu-at-pointer: FALSE-POSITIVE (no product change; a guard test added)

## Finding

Luna's GUI sweep (050/m0-train-25): Main menu > Navigate > Tool Menu at Pointer closes the Navigate menu, but no tool
menu appears. Luna's suggested cause: the popup is opened synchronously while the Navigate menu's action is still being
activated, and closing the Navigate menu dismisses it. Luna's suggested fix: open the popup later.

## Verdict and evidence

The behaviour the sweep saw is the intended agent-instance behaviour. The suggested cause is wrong, and the suggested
fix was tried and does not change what the sweep sees.

- `window.tool_menu` shows a popup only when the registry is not headless:
  `src/gui/ControlCommandsArrangerView.cpp:335-336` (`const bool shown = !ControlRegistry::instance()->isHeadless();`,
  "Only a menu that is shown needs the window: headless, the entries are the answer").
- The registry is headless whenever `isUnattendedRun()` is true (`src/core/ControlRegistry.cpp:159`). That is true for
  every instance started with `--control-socket` (`src/core/UnattendedRun.cpp:76-79`; `src/core/main.cpp:1264` calls
  `setAgentInstance(true)`). The contract is in `include/UnattendedRun.h`: an agent-driven instance is unattended.
  Other GUI paths follow the same rule, for example the result notice in `src/gui/CommandRun.cpp:90`.
- The sweep always starts Zene as an agent instance: `terra-pilot/gui.py start` calls the harness's `start_instance`,
  which launches `zene --config ... --control-socket ...` (`tests/control_socket_harness.py:257-259`).

Measured live with this worktree's `zene`, on xcb inside a private Xvnc (the sweep's own platform, 1600x1000):

| Run | Launch | Code | Result | Evidence (LOG FOLDER) |
| --- | --- | --- | --- | --- |
| 1 | sweep driver (`--control-socket`) | unmodified | Navigate closes, no tool menu: the sweep's symptom reproduced | `13-navigate-open.png`, `14-after-click.png` |
| 2 | sweep driver (`--control-socket`) | Luna's fix (popup deferred with `QTimer::singleShot(0, ...)`) | still no tool menu | `19-after-click-fixed.png` |
| 3 | same instance, `window.tool_menu` over the socket with no menu open | Luna's fix | reply `"shown":false` and no popup, so the headless rule decides, not the Navigate menu | `22-socket-toolmenu.log`, `23-socket-toolmenu.png` |
| 4 | a person's launch (`zene --config`, xcb, **no** `--control-socket`) | unmodified | the tool menu opens at the pointer with the arranger tools (draw … smart) and the note editor tools (draw … mute) | `27-normal-orig-navigate.png`, `27-normal-orig-after.png` |

I reverted the experimental deferral (`git checkout -- src/gui/ControlCommandsArrangerView.cpp`), rebuilt `zene`
(`25-build-orig.log`, exit 0), and ran run 4 on that unmodified build. Driver:
`runs/…r1/scripts/normal_launch.py`; exit 0, `zene alive: True` (`27-normal-orig.log`).

Side observation, out of scope: in run 4, the menu's `addSection` titles ("Arranger" / "Note editor") are drawn as
plain separators by the current style. The choices themselves are present.

## Test

`tests/src/gui/ToolMenuAtPointerTest.cpp` is new and registered in `tests/CMakeLists.txt` (with the offscreen
environment), `tests/fork-sources.txt` and `tests/all-sources.txt`. It builds the real application and checks:

- `clickLeavesToolMenuOpen`: opens Navigate by clicking its title in the menu bar, then clicks "Tool Menu at Pointer".
  Both clicks go through the `QWindow`, so `QGuiApplication`'s popup handling sees them. The Navigate menu closes and
  the tool menu is the active popup, with a `draw` entry.
- `keyboardLeavesToolMenuOpen`: the same item chosen with Return.
- `commandAnswersWithEntries`: from the socket, at a display, the reply is `shown:true` with the entries, and the menu
  shows.
- `agentInstanceAnswersWithoutMenu`: when headless (an agent instance), the reply is `shown:false` with the entries,
  and no popup shows. This is the by-design case the sweep reported.

This is a guard, not a RED/GREEN regression: there is no product change to revert. My first run, before the test set
`setHeadless(false)`, failed the three display cases (`04-test-red.log`, exit 8). The reason was that the test process
is itself unattended (offscreen), not the reported bug, and that run is what pointed to the headless rule.

## Commands and exit codes (logs in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-tool-menu-at-pointer.r1/logs)

| Step | Command | Exit | Log |
| --- | --- | --- | --- |
| build (unmodified product + test) | `cmake --build build --target zene ToolMenuAtPointerTest ArrangerKeysNavigatorTest -j4` | 0 | `28-build-final.log` |
| build final test (bounded waits removed) | `cmake --build build --target ToolMenuAtPointerTest -j4` | 0 | `37-build-final.log` |
| tests, 3 runs (from build/tests) | `ctest -R '^(ToolMenuAtPointerTest\|ArrangerKeysNavigatorTest)$' -V` | 0, 0, 0 | `38-test-final-{1,2,3}.log`: 2 tests run each time; ToolMenuAtPointerTest 6 passed / 0 failed, ArrangerKeysNavigatorTest 14 passed / 0 failed |
| gates (log-folder copy) | `python3 …r1/scripts/feature_check_logdir.py <worktree>` | 0 | `39-feature-check.log`: 8/8 PASS |
| gates (original script) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | `40-feature-check-original.log`: PermissionError creating `<worktree>-feature-check`, before any gate runs |

Note on the gate command. `feature_check.py` writes its logs to `<worktree>-feature-check`, a sibling of the
worktree, and this lane cannot write there (PermissionError, `32-feature-check.log`, exit 1). I ran a copy that
differs only in two lines: `ROOT` points at the original `qwen/` folder and the log folder is `logs/feature-check`
(`35-feature-check-copy.diff`). The gates themselves are unchanged. The first run of that copy
(`33-feature-check.log`, exit 1) failed fork-sources/all-sources-reproduce only because the new test was not yet
tracked. After `git add`, all 8 gates pass. Also, ccache's configured temp directory was not writable for this lane,
so builds ran with `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-gui1105`.

Files committed: `tests/src/gui/ToolMenuAtPointerTest.cpp`, `tests/CMakeLists.txt`, `tests/fork-sources.txt`,
`tests/all-sources.txt`, and this report.

## Self-check

- **The regression test FAILS with your product change reverted and PASSES with it (exit codes):** not applicable.
  The verdict is FALSE-POSITIVE, so there is no product change to revert and step 4b cannot apply. The guard test
  passes on the unmodified product (`38-test-final-{1,2,3}.log`, exit 0 three times). I checked the suggested product
  change live instead: it made no difference (run 2 vs run 1), and a person's launch of the unmodified code already
  works (run 4).
- **The test is deterministic:** no sleeps, no `QTRY_*`, no `qWait*` (I removed the bounded waits before committing).
  It uses synchronous `QTest::mouseClick` / `keyClick` on the menus' `QWindow` / `QMenu`, then
  `QCoreApplication::processEvents()`. The seam is `ControlRegistry::setHeadless()`, which sets a display (false) or an
  agent instance (true) explicitly instead of inferring it from the platform. The pointer position comes from the
  synthetic click.
- **The fix is at the cause and complete (callers and siblings):** no product change. The one interface caller of
  `window.tool_menu` is `src/gui/MenuBarExtras.cpp:146`. The handler is the only `popup(` in
  `src/gui/ControlCommands*.cpp`. The headless rule it follows is shared on purpose: `src/gui/CommandRun.cpp:90`,
  `src/gui/ControlCommandsTheme.cpp:301`, `src/gui/WorkflowRun.cpp:146`.
- **No collateral damage:** nothing in the product changed, so projects and saved data load as before. No limit or
  check added, no command added, and the commands snapshot is unchanged.
- **FALSE-POSITIVE evidence (file:line / contract):** `src/gui/ControlCommandsArrangerView.cpp:335-336` shows the
  popup only when not headless. `src/core/ControlRegistry.cpp:159` sets headless = `isUnattendedRun()`.
  `src/core/UnattendedRun.cpp:76-79` and `src/core/main.cpp:1264` make every `--control-socket` instance unattended
  (contract: `include/UnattendedRun.h`). `tests/control_socket_harness.py:257-259` shows that the sweep's launcher
  always passes `--control-socket`. Live runs 1–4 above (screenshots in the LOG FOLDER) confirm all of this.
- **A GUI test drives that GUI path:** yes. `ToolMenuAtPointerTest` clicks the real Navigate menu (opened from the
  real main window's menu bar) and uses the keyboard.
- **Gates:** 8/8 PASS with the log-folder copy (`39-feature-check.log`, exit 0). The original script cannot create its
  sibling log folder from this lane (`40-feature-check-original.log`, exit 1, PermissionError). For that reason
  `submit_check.py` reports "gates (feature_check.py)" as FAIL with no gate named. That FAIL is the lane's write
  restriction, not a gate result.
- **Suggestion for the sweep (outside this lane, not done):** treat interface-only popups in an agent instance as
  headless by design, or launch the sweep's instance without `--control-socket` for this item.
