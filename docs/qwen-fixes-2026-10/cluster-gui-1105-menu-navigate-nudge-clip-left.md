FIXED

# cluster gui-1105-menu-navigate-nudge-clip-left - refusal toast placed off-screen

## Integration (round 2, onto 050/m0-train-26 @ af010bc3cc)
Source: `bugs/qwen-gui-1105-menu-navigate-nudge-clip-left` @ `3fb35066c5` (accepted). Target: `af010bc3cc`.
The source branch is already based on the target (`git merge-base --is-ancestor af010bc3cc 3fb35066c5` -> true), and the
three-way merge in this worktree left no conflict markers. The integrated index is byte-identical to the accepted source:
`git diff --cached 3fb35066c5 --stat` prints nothing. So there is **no semantic difference** from the accepted patch;
no conflict needed resolving here and no new design was introduced. The reported TextFloat.cpp conflict did not
reproduce against this pinned target; I rebuilt and re-verified everything below on the integrated tree.

## Verdict and evidence
REAL. `Navigate > Nudge Clip Left` runs `arrangement.nudge` (src/gui/MenuBarExtras.cpp:85); a refusal is shown by
`announce` (src/gui/CommandRun.cpp:95-96), which passes the menu's host - the main window (`hostOf`, CommandRun.cpp:50) -
as `parent` to `TextFloat::displayMessage`. That function placed the message at `QPoint(parent->width() + 2, 0)`
(src/gui/widgets/TextFloat.cpp:134). For a main window that fills its screen, that position is off the screen, so the
"no clip selected" text was never visible.

## Fix (accepted design, unchanged)
`src/gui/widgets/TextFloat.cpp:47-58` adds `keepOnScreen` (anonymous namespace). `displayMessage` calls it after the
anchored `moveGlobal` (TextFloat.cpp:135). It clamps the message's frame into the available geometry of the anchor's
screen. Messages that already fit stay exactly where they were, and the no-parent branch is untouched. The fix adds no
audio-thread code, no command and no header change.

## Failure paths and platform hazards reviewed
- Anchor fills the screen (the main window, maximised): the bug; the message is clamped back inside. Regression covered.
- Anchor not at the screen edge: the clamp is a no-op, so the position is unchanged.
- Message larger than the screen: `std::max(area.left(), ...)` wins, so the message is pinned to the top-left of the area, never placed at a negative offset.
- `anchor->screen()` null (headless or no screen): early return; same behaviour as before.
- Multi-monitor setups: the clamp uses the anchor's own screen, so a message never jumps to another monitor.
- Wayland: absolute `move()` positions for top-level windows are compositor-controlled. The clamp is then just a hint and does no harm; X11, Windows and macOS honour it.
- Realtime/undo/commands: none touched (GUI thread only).

## Regression test
`tests/src/gui/GrooveMenuTest.cpp` `aMessageOverTheMainWindowIsOnScreen`: sizes the main window to its screen's
availableGeometry, shows a message anchored to it (timeout 0, no timer) and asserts that the message lies inside the
screen. The test is deterministic: no sleeps, no waits.

## Evidence table (round 2; logs in runs/gui-1105-menu-navigate-nudge-clip-left.r2/logs; CCACHE_DISABLE=1 because ccache's temp dir is outside the write-jail)
| step | command | exit |
| --- | --- | --- |
| build integrated tree | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4` (build-1.log) | 0 |
| GREEN, affected family | `cd build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure` (test-1-green.log) | 0, 2/2 passed |
| 4b revert | `git diff HEAD -- src/gui/widgets/TextFloat.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b RED | `cmake --build build --target zene GrooveMenuTest -j4` (build-2.log); `ctest -R '^GrooveMenuTest$' --output-on-failure` (test-2-red.log) | build 0, ctest 8: `FAIL! GrooveMenuTest::aMessageOverTheMainWindowIsOnScreen() 'screen.contains(shown)' returned FALSE. (message 804,2 144x76 is outside the screen 0,0 800x800)` |
| 4b reapply | `git apply logs/fix.patch` | 0 |
| 4b GREEN | `cmake --build build --target zene GrooveMenuTest -j4` (build-3.log); `ctest -R '^GrooveMenuTest$' --output-on-failure` (test-3-green.log) | build 0, ctest 0, 1/1 passed |
| gates (as shipped) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (gates-1.log) | 1: PermissionError creating `<worktree>-feature-check/` (write-jail, environmental) |
| gates (logs dir moved) | `python3 runs/.../feature_check_local.py <worktree>` (gates-2.log) | 0: 8/8 PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

`feature_check_local.py` is the shipped script with two lines changed (shown by `diff`): ROOT pinned to the original
script directory, and the logs dir moved into the run's logs folder. Gate logic and baselines are unchanged.

Files committed: src/gui/widgets/TextFloat.cpp, tests/src/gui/GrooveMenuTest.cpp, tests/upstream-modifications.txt,
this report.

## Self-check
- Reverted/applied: the test FAILS without the fix (ctest exit 8, test-2-red.log) and PASSES with it (exit 0, test-3-green.log).
- Deterministic: no timers, waits or sleeps; the message is positioned synchronously inside displayMessage.
- Cause and callers: the only placement at a parent's right edge is `displayMessage` (TextFloat.cpp:134). All callers
  (CommandRun.cpp:95, ClipView, TimeLineWidget, MainWindow, Vestige/VstEffect, ...) go through it, so the clamp covers
  every one of them.
- No collateral damage: the clamp moves a message only when it would leave the screen. No saved data, audio thread,
  commands or headers are involved.
- Integration: the index equals the accepted source tree (`git diff --cached 3fb35066c5` is empty), so no semantic difference.
- The shipped `feature_check.py` fails only because of the write-jail (gates-1.log). The loop needs to rerun it outside the jail.
