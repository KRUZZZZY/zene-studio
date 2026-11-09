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

## Round 3 (re-verification on ff6117d2a6; logs in runs/gui-1105-menu-navigate-nudge-clip-left.r3/logs)
The review asked again for the integration evidence. No product or test code changed in this round. `git diff af010bc3cc HEAD --stat`
still lists only the four files above, and TextFloat.cpp differs from the target only by the accepted `keepOnScreen` hunk.
The only failing submit_check row was `gates`. The cause is again the write-jail: the shipped script raises PermissionError
writing `<worktree>-feature-check/fork-sources.log` (gates-1.log). The first build attempt in this round failed because
ccache's temp dir is also outside the jail (build-1.log/build-2.log, exit 2: `ccache: error: failed to create temporary file`).
I redid the builds with CCACHE_DISABLE=1.

| step | command | exit |
| --- | --- | --- |
| gates (as shipped) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (gates-1.log) | 1: PermissionError (write-jail) |
| gates (log destination redirected) | `python3 logs/fc_wrap.py <worktree> logs/feature-check` (gates-2.log) | 0: 8/8 PASS, RESULT: PASS |
| 4b revert | `git diff af010bc3cc HEAD -- src/gui/widgets/TextFloat.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b RED | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4` (build-3-red.log); `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure` (test-3-red.log) | build 0, ctest 8: `FAIL! GrooveMenuTest::aMessageOverTheMainWindowIsOnScreen() 'screen.contains(shown)' returned FALSE. (message 804,2 144x76 is outside the screen 0,0 800x800)` |
| 4b reapply | `git apply logs/fix.patch` | 0 |
| 4b GREEN, affected family | same build (build-4-green.log); `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure` (test-4-green.log) | build 0, ctest 0, 2/2 passed |

`fc_wrap.py` imports the shipped `feature_check.py` without editing it. It wraps only `run()` so that each gate's log is written
into the logs folder rather than the out-of-jail sibling directory. Gate commands, baselines and the PASS/FAIL logic are the shipped ones.
After the reapply, `git status --short` was empty.

## Self-check
- Reverted/applied: the test FAILS without the fix (ctest exit 8, test-2-red.log) and PASSES with it (exit 0, test-3-green.log).
  Round 3 reproduced this: r3 test-3-red.log exit 8, test-4-green.log exit 0 (2/2).
- Seam: `displayMessage(..., timeout 0, window)` places the message synchronously. The test sizes the window to
  `screen()->availableGeometry()` rather than relying on the platform's maximise, so there are no timers, sleeps or event-loop waits.
- Only one site places a message at a parent's right edge: `TextFloat.cpp:134`, the anchored branch. The no-parent branch
  (`TextFloat.cpp:141`, inside the main window) is unchanged. `displayMessage` has callers in 20 files under src/ and plugins/,
  and they all reach this one placement.
- Deterministic: no timers, waits or sleeps; the message is positioned synchronously inside displayMessage.
- Cause and callers: the only placement at a parent's right edge is `displayMessage` (TextFloat.cpp:134). All callers
  (CommandRun.cpp:95, ClipView, TimeLineWidget, MainWindow, Vestige/VstEffect, ...) go through it, so the clamp covers
  every one of them.
- No collateral damage: the clamp moves a message only when it would leave the screen. No saved data, audio thread,
  commands or headers are involved.
- Integration: the index equals the accepted source tree (`git diff --cached 3fb35066c5` is empty), so no semantic difference.
- The shipped `feature_check.py` fails only because of the write-jail (gates-1.log). The loop needs to rerun it outside the jail.
