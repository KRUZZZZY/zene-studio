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
| build final test | `cmake --build build --target ToolMenuAtPointerTest -j4` | 0 | `30-build-final.log` |
| tests (from build/tests) | `ctest -R '^(ToolMenuAtPointerTest\|ArrangerKeysNavigatorTest)$' -V` | 0 | `31-test-final.log`: 2/2 tests; ToolMenuAtPointerTest 6 passed / 0 failed, ArrangerKeysNavigatorTest 14 passed / 0 failed |
| gates | `python3 …/scripts/feature_check_logdir.py <worktree>` | 0 | `34-feature-check.log`: 8/8 PASS |

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

- Regression test fails without the fix and passes with it: not applicable. The verdict is FALSE-POSITIVE and there is
  no product change. The guard test passes on the unmodified product (`31-test-final.log`, exit 0). The live evidence
  (runs 1–4 above) shows that the deferral fix makes no difference and that a person's launch already works.
- Fix at the cause, and callers and siblings checked: no product change. The one caller of `window.tool_menu` from the
  interface is `src/gui/MenuBarExtras.cpp:146`. The handler is the only `popup(` in `src/gui/ControlCommands*.cpp`.
  The headless rule is shared on purpose (`CommandRun.cpp:90`, `ControlCommandsTheme.cpp:301`, `WorkflowRun.cpp:146`).
- No collateral damage, realtime, undo, commands: nothing in the product changed. No command added; the snapshot is
  unchanged.
- A GUI test drives that GUI path: yes. `ToolMenuAtPointerTest` clicks the real Navigate menu in the real main window.
- Gates: 8/8 PASS (`34-feature-check.log`, exit 0), run through the log-folder copy described above.
- Suggestion for the sweep (not done here; outside this lane): judge interface-only popups in an agent instance as
  "headless by design", or launch the sweep's instance without `--control-socket` for this item.
