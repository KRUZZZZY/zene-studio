FALSE-POSITIVE

# cluster gui-1106-main-pop-out-the-arranger-panel - "Arranger pop-out click leaves the panel docked"

## Verdict and evidence

The click did float the Arranger. The new window opened exactly over the panel's old place in the main window, at the
same size. The sweep's display has no window manager, so there was no title bar or frame to show it was a separate
window. The judge saw what looked like an unchanged screen.

1. **The sweep's own "after" screenshot shows the panel floating.** In
   `~/.local/share/zene-bunny-fix/terra-pilot/buttons/runs/m0-train-26/shots/0039-pair.png` (right half) the
   Arranger header's button reads **"Dock"**, where it was an icon before the click. `Shell::syncFloatButton` sets that
   text only when `isDetached(id)` is true (`src/gui/shell/ShellFloating.cpp:430-434`).
   `isDetached` is `m_floating.contains(id)` (`include/Shell.h:130`), and `detachPanel` inserts the panel
   into it (`src/gui/shell/ShellFloating.cpp:254`). So `window.detach` succeeded.
2. **By design, the window opens where the panel was.** `detachPanel` places the window at
   `frame->mapToGlobal(QPoint(0, 0))` (`src/gui/shell/ShellFloating.cpp:246-248`, "a pop-out never sends the panel
   far from the eye") with size `floatSize(frame)` = the frame's own size, clamped to 480x320..1200x800
   (`src/gui/shell/ShellFloating.cpp:193-196, 260`). The docked Arranger is about 1080x560 at the sweep's size, which is inside
   those bounds, so the window covers the old spot pixel for pixel. The centre placeholder behind it is hidden.
3. **The discarded result cannot have hidden a refusal here.** `window.detach` refuses only for an unknown panel
   id or a bad `screen` (`src/gui/ControlCommandsLayouts.cpp:228-243`). The button sends the panel's own id, `arranger`
   (`src/gui/shell/ShellFloating.cpp:424`), and no `screen`. `detachPanel` fails only when there is no such
   frame (`ShellFloating.cpp:239-244`). Point 1 also shows the call succeeded.

No product code is changed. The finding was a limit of what a screenshot can show. A judge that wants to see the pop-out
needs a window manager in the sweep, or needs to read `window.get_state`'s floating list.

## Regression test (pins the user path)

`tests/src/gui/ShellTest.cpp` `thePopOutClickFloatsTheArrangerOverItsOwnPlace` clicks the Arranger header's own
`panelFloat` button, which is what the sweep clicked. It checks that the panel is now in a visible window of its own,
with geometry equal to the panel's old global rect and `floatSize`. That is exactly why the screenshot looked unchanged. It
passes on unchanged product code, as a FALSE-POSITIVE test should. There is no fix to revert, so step 4b
(revert and see it fail) does not apply.

## Commands and results

Logs: `~/.local/share/zene-bunny-fix/qwen/runs/gui-1106-main-pop-out-the-arranger-panel.r1/logs/`

| Step | Command | EXIT |
| --- | --- | --- |
| build (1st) | `cmake --build build --target ShellTest -j4 > build-1.log` | 2 (ccache could not write its temp dir under the sandbox: `Permission denied`, not a compile error) |
| build | `CCACHE_DISABLE=1 cmake --build build --target ShellTest -j4 > build-2.log` | 0 |
| test | from `build/tests`: `ctest -R '^ShellTest$' --output-on-failure > test-2.log` | 0 (1/1 tests passed) |
| new case | `../tests/ShellTest thePopOutClickFloatsTheArrangerOverItsOwnPlace > test-3.log` | 0 (PASS) |
| family | `ctest -R 'Shell' > test-4.log` | 8. `ShellLayoutTest` and `ShellHomesTest` are "Not Run": they are not built in this tree and I did not build them. I changed only `ShellTest.cpp`. |
| gates | `feature_check.py <worktree>` | 1. It crashed with `PermissionError` creating `<worktree>/../...-feature-check` because that folder is outside the write sandbox. I ran its gates one by one instead (below). |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity | `bash tests/complexity-gate.sh --check` | 1 (red on the train on purpose; none of its REGRESSION lines names `ShellTest.cpp`) |
| file-length | `bash tests/file-length-gate.sh --check` | 1 (red on the train on purpose; none of its REGRESSION lines names `ShellTest.cpp`, which is 496 lines) |

Files committed: `tests/src/gui/ShellTest.cpp`, `docs/qwen-fixes-2026-10/cluster-gui-1106-main-pop-out-the-arranger-panel.md`.
No new files need registering. ShellTest.cpp is an existing fork test, and the report sits with the other cluster reports.

## Self-check

- **Fails without the fix, passes with it:** does not apply, because there is no product change to revert (FALSE-POSITIVE).
  The test passes on the unchanged product code: `ShellTest thePopOutClickFloatsTheArrangerOverItsOwnPlace`, EXIT=0
  (test-3.log); `ctest -R '^ShellTest$'`, EXIT=0 (test-2.log). It pins the contract that explains the screenshot.
- **Deterministic:** yes. It uses no timers, sleeps or event-loop waits. `QToolButton::click()` emits `clicked`
  synchronously. With no `MainWindow`, `dispatchFloat` calls `detachPanel` directly (`ShellFloating.cpp:222-227`), and the
  geometry it compares is computed from the same frame before the click. Platform: offscreen.
- **Fix at the cause, complete:** nothing in the product changed. I checked every path into `dispatchFloat`: the panel
  button (`ShellFloating.cpp:424`), the Ctrl+Shift+F action (`ShellFloating.cpp:210-216`), tear-off drag and double-click
  and the context menu (`ShellTearOff.cpp:235, 340-342, 395`). All of them go through `window.detach`, then `detachPanel`
  (`ControlCommandsLayouts.cpp:219-243`), which floats the panel as described above.
- **No collateral damage:** the only change is one new test case, so saved data and other behaviour are unaffected.
- **FALSE-POSITIVE evidence:** see "Verdict and evidence". The "after" shot shows "Dock", which `syncFloatButton` sets only
  when the panel is detached (`ShellFloating.cpp:430-434`, `include/Shell.h:130`). The window opens at the panel's own
  place and size (`ShellFloating.cpp:246-248, 260, 193-196`).
- **Gates row (FAIL in submit_check):** `feature_check.py` itself cannot run in this lane. It raises `PermissionError`
  making `<worktree>/../<name>-feature-check`, which is outside the write sandbox. I did not change the script. I ran
  each of its gate commands from the worktree instead: 6/6 simple gates EXIT=0. Complexity and file-length are EXIT=1, but
  none of their REGRESSION lines names `ShellTest.cpp` (logs `gate-*.log`). An unsandboxed rerun of `feature_check.py`
  is still needed.
