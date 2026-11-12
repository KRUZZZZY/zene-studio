FIXED

# Cluster gui-1105-menu-navigate-nudge-clip-right: the Navigate refusal notification was placed off-screen

## Integration onto 050/m0-train-26 (round 3, 2026-11-09)

Integration job: the accepted patch (`bugs/qwen-gui-1105-menu-navigate-nudge-clip-right` @ `41327bece6`) onto
`050/m0-train-26` @ `af010bc3cc`. Logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs`.

- **Conflict resolution:** there is no semantic conflict to resolve. The driver's three-way apply falls back to a
  direct apply, and the direct apply is clean. The reason is mechanical, not a content conflict: the accepted patch
  creates a new file (`docs/qwen-fixes-2026-10/cluster-gui-1105-menu-navigate-nudge-clip-right.md`), and
  `git apply --3way` cannot three-way merge a new file, so it prints "Falling back to direct application..." and
  applies directly. Reproduced in this tree with a one-line new-file patch: `git apply --3way --check
  "$TMPDIR/newfile-test.patch"` printed "Falling back to direct application..." and exited 0. The patch's preimage
  blobs match the target exactly: `git rev-parse af010bc3cc:src/gui/widgets/TextFloat.cpp` is
  `225e3650137877d335c10079e093afd3a2dfc97d`, the same `225e365013` preimage in the patch's index line, and the
  postimage `f50124446a` matches `git rev-parse 41327bece6:src/gui/widgets/TextFloat.cpp`
  (`f50124446ab88be9aa8f7bbdcb77c3ab65a602d6`). So every hunk applies where it was written.
- **The newer line is preserved.** The train's theme-17 screen-reader block (`a11y::announceStatus`,
  `TextFloat.cpp:144-148`) comes after the new clamp call at `TextFloat.cpp:142` and is untouched. The float is
  positioned before it is announced, exactly as in the accepted patch. The integrated file is byte-identical to the
  accepted branch: `git diff 41327bece6 HEAD -- src tests` printed 0 lines (`02-diff-vs-accepted.txt`).
- **Semantic difference from the accepted patch:** none. No design change was made in this round.
- **Failure paths and platform hazards:** the parent branch (a refusal anchored to a maximized window,
  `TextFloat.cpp:134`) and the no-parent branch (`:140`) both go through `keepOnScreen` (`:49`). `screen() ==
  nullptr` returns early, so the float stays where it was put. A float larger than the screen pins to the top-left
  of the area (the `std::max` guard). On multi-monitor setups the clamp uses the anchor's own screen. This is
  Qt-only code (`QWidget::screen()`, Qt >= 5.14; the tree is Qt6) with no platform `#ifdef`. It is GUI-thread only,
  with nothing on the audio thread and no undo, save-data or control-command change.

| Step | Command (run from the worktree unless stated) | EXIT |
| --- | --- | --- |
| Integration diff | `git diff --stat af010bc3cc HEAD > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs/01-diff-stat.txt` | 0 (4 files, 187 insertions) |
| Identity with accepted | `git diff 41327bece6 HEAD -- src tests > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs/02-diff-vs-accepted.txt` | 0 (0 lines) |
| Product hunk | `git diff af010bc3cc HEAD -- src/gui/widgets/TextFloat.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs/03-textfloat-hunk.txt` | 0 (47 lines) |
| Build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs/04-build.log 2>&1` | 0 |
| GREEN | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc/build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs/05-test-green.log 2>&1` | 0 (1/1) |
| 4b revert | `git diff af010bc3cc HEAD -- src/gui/widgets/TextFloat.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs/fix.patch` | 0 |
| 4b RED build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs/06-build-red.log 2>&1` | 0 |
| 4b RED test | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc/build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs/07-test-red.log 2>&1` | 8 (`screen.contains(placed)` FALSE: "refusal placed at 835,2 146x80, screen 0,0 800x800", GrooveMenuTest.cpp:198; 4 passed, 1 failed) |
| 4b reapply | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs/fix.patch` | 0 |
| 4b GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs/08-build-green.log 2>&1` | 0 |
| 4b GREEN test | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc/build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs/09-test-green.log 2>&1` | 0 (1/1) |
| Family build | `CCACHE_DISABLE=1 cmake --build build --target PianoRollBenchmarkTest Vst3InstrumentIntegrationTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs/10-build-family.log 2>&1` | 0 |
| Family test | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc/build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest\|PianoRollBenchmarkTest\|Vst3InstrumentIntegrationTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs/11-test-family.log 2>&1` | 0 (4/4) |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs/12-feature-check.log 2>&1` | 0 (8/8 PASS, RESULT: PASS) |
| Completion check | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc gui-1105-menu-navigate-nudge-clip-right > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs/13-submit-check.log 2>&1` | 0 (RESULT: PASS - submit) |

The round-2 and round-1 evidence follows below. The round-2 environment could not create the feature-check log
directory next to the worktree, so its gate row used a wrapper; in round 3 the direct gate run and `submit_check.py`
both pass, and the wrapper is gone.

## Integration onto 050/m0-train-26 (round 2, 2026-11-09)

Integration job: the accepted patch (`bugs/qwen-gui-1105-menu-navigate-nudge-clip-right` @ `41327bece6`) onto
`050/m0-train-26` @ `af010bc3cc`. Logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs`.

- **Conflict resolution:** none was needed. The driver's three-way apply fell back to a direct apply, and all three
  files applied cleanly (`integration-apply.log`: "Applied patch to 'src/gui/widgets/TextFloat.cpp' cleanly"). The
  train's newer `displayMessage` code (the theme-17 `a11y::announceStatus` block, `TextFloat.cpp:144-148`) comes
  after the clamp call at `TextFloat.cpp:142` and is untouched. The float is positioned before it is announced, as
  in the accepted patch.
- **Semantic difference from the accepted patch:** none. `git diff 41327bece6 --cached -- src tests` printed 0 lines
  (`01-diff-vs-accepted.txt`), so the integrated product and test code is byte-identical to the accepted branch.
- **Failure paths and platform hazards:** the parent branch (a refusal anchored to a maximized window,
  `TextFloat.cpp:134`) and the no-parent branch (`:140`) both go through `keepOnScreen` (`:49`). `screen() ==
  nullptr` returns early, so the float stays where it was put. A float larger than the screen pins to the top-left
  of the area (the `std::max` guard). On multi-monitor setups the clamp uses the anchor's own screen. This is
  Qt-only code (`QWidget::screen()`, Qt >= 5.14; the tree is Qt6) with no platform `#ifdef`. It is GUI-thread only,
  with nothing on the audio thread and no undo, save-data or control-command change.

| Step | Command (run from the worktree unless stated) | EXIT |
| --- | --- | --- |
| Build (ccache) | `cmake --build build --target zene GrooveMenuTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs/02-build.log 2>&1` | 2 (ccache temp dir outside write jail: "Permission denied"; not a compile error) |
| Build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs/03-build.log 2>&1` | 0 |
| GREEN | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc/build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs/04-test-green.log 2>&1` | 0 (1/1) |
| 4b revert | `git diff --cached -- src/gui/widgets/TextFloat.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs/fix.patch` | 0 |
| 4b RED build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs/05-build-red.log 2>&1` | 0 |
| 4b RED test | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc/build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs/06-test-red.log 2>&1` | 8 (`screen.contains(placed)` FALSE: "refusal placed at 835,2 146x80, screen 0,0 800x800", GrooveMenuTest.cpp:198; 4 passed, 1 failed) |
| 4b reapply | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs/fix.patch` | 0 |
| 4b GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs/07-build-green.log 2>&1` | 0 |
| 4b GREEN test | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc/build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs/08-test-green.log 2>&1` | 0 (2/2) |
| Family build | `CCACHE_DISABLE=1 cmake --build build --target PianoRollBenchmarkTest Vst3InstrumentIntegrationTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs/11-build-family.log 2>&1` | 0 |
| Family test | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc/build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest\|PianoRollBenchmarkTest\|Vst3InstrumentIntegrationTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs/12-test-family.log 2>&1` | 0 (4/4) |
| Gates (direct) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs/09-feature-check.log 2>&1` | 1 (PermissionError creating the sibling feature-check log directory, outside the write jail) |
| Gates | `FC_LOGS=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs/fc python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs/fc_wrapper.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r2/logs/10-feature-check.log 2>&1` (unchanged script source, log dir redirected only) | 0 (8/8 PASS, RESULT: PASS) |

The round-1 evidence for the accepted patch follows below.

## Verdict: REAL

- `src/gui/MenuBarExtras.cpp:87` – Navigate > Nudge Clip Right runs `arrangement.nudge` through
  `addCommandAction`. With no clip selected the command refuses, and `announce()` reports it with
  `TextFloat::displayMessage(..., parent)` (`src/gui/CommandRun.cpp:95`). Here `parent` is the menu's host, which
  is the main window (`hostOf`, `src/gui/CommandRun.cpp:49-53`).
- `src/gui/widgets/TextFloat.cpp:134` (was line 113) places a float that has a parent at
  `parent->width() + 2`, just past the parent's right edge. That position works for a knob, but not for a
  maximized main window: the float lands past the screen's right edge.
- Reproduced in the test before the fix: `refusal placed at 835,2 146x80, screen 0,0 800x800`.

## Fix

`src/gui/widgets/TextFloat.cpp:49` adds `keepOnScreen()`. It clamps the float's frame inside the anchor screen's
`availableGeometry()`. `displayMessage` calls it once, after either placement (`TextFloat.cpp:142`), so the
parent branch and the no-parent branch behave the same way. A float that already fits on screen is not moved,
so every other caller places its message exactly as before. Nothing here runs on the audio thread. No undo
step, saved data or control command changes.

Siblings I checked:
- `FloatModelEditorBase.cpp:137` and `Fader.cpp:544` use the same `width() + 2` offset through `moveGlobal`, but
  their anchor is a small control inside a window, not a top-level window. I left them unchanged to keep this fix
  narrow. A knob sitting against the screen's right edge could in principle show the same thing; this report
  does not cover that.
- All 31 `displayMessage` call sites now go through the clamp. On-screen placements are unchanged.

## Regression test

`tests/src/gui/GrooveMenuTest.cpp:177` `aMenuRefusalOverAFullScreenWindowStaysOnScreen`. It stretches the main
window across the screen, triggers the real Navigate > Nudge Clip Right action with no clip selected, and
requires exactly one TextFloat whose frame lies inside the screen's available geometry (line 198). It has no
timing and no sleeps.

## Evidence (logs in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r1/logs)

ccache's temporary directory is outside this lane's write jail (`01-build-red.log`: "Permission denied"), so
every build after that ran with `CCACHE_DISABLE=1`.

| Step | Command (run from the worktree unless stated) | EXIT |
| --- | --- | --- |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target GrooveMenuTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r1/logs/07-build-red.log 2>&1` | 0 |
| RED test | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc/build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r1/logs/08-test-red.log 2>&1` | 8 (`screen.contains(placed)` FALSE: 835,2 146x80 on 0,0 800x800) |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r1/logs/11-build-green.log 2>&1` | 0 |
| GREEN test | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc/build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r1/logs/12-test-green.log 2>&1` | 0 (2/2) |
| 4b revert | `git diff -- src/gui/widgets/TextFloat.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r1/logs/fix.patch` | 0 |
| 4b without fix build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r1/logs/13-build-4b-without.log 2>&1` | 0 |
| 4b without fix test | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc/build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r1/logs/14-test-4b-without.log 2>&1` | 8 (same failure, 835,2) |
| 4b reapply | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r1/logs/fix.patch` | 0 |
| 4b with fix build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r1/logs/15-build-4b-with.log 2>&1` | 0 |
| 4b with fix test | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc/build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r1/logs/16-test-4b-with.log 2>&1` | 0 (1/1) |
| Family test | `cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc/build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest\|PianoRollBenchmarkTest\|Vst3InstrumentIntegrationTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r1/logs/18-test-family.log 2>&1` | 0 (4/4) |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r1/logs/run_feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-integrate-gui-1105-menu-navigate-nudge-clip-right-af010bc3cc /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r1/logs/feature-check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r1/logs/20-gates.log 2>&1` | 0 (8/8 PASS) |

About the gates: `feature_check.py` creates a sibling `<worktree>-feature-check` log directory. In round 1 that
was outside this lane's write jail, so the direct run failed with `PermissionError` (EXIT 1, `19-gates.log`).
`run_feature_check.py` executed the unchanged script source and changed only that log directory, to
`LOGS/feature-check`. No gate, limit or baseline was changed. In round 3 the direct run and `submit_check.py` both
pass without a wrapper.

Files committed: `src/gui/widgets/TextFloat.cpp`, `tests/src/gui/GrooveMenuTest.cpp`,
`tests/upstream-modifications.txt` (registers the inherited TextFloat.cpp change), and this report.

## Self-check

- **The test fails with the fix reverted and passes with it:** yes. Round 3 reverted the product hunk with
  `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-navigate-nudge-clip-right.r3/logs/fix.patch`
  (EXIT 0), rebuilt (EXIT 0), then `ctest -R '^GrooveMenuTest$'` gave EXIT 8: `screen.contains(placed)` FALSE,
  refusal at 835,2 on 0,0 800x800 (`07-test-red.log`). Reapplied with `git apply
  .../r3/logs/fix.patch` (EXIT 0), rebuilt (EXIT 0), and the same ctest gave EXIT 0 (`09-test-green.log`).
- **The test is deterministic:** it has no timers, sleeps or waits. The seam is geometry. The main window is set
  to the offscreen screen's `availableGeometry()`, and the test checks that it reaches the right edge. It calls
  `QAction::trigger()`, which runs `arrangement.nudge` synchronously. `displayMessage` positions the float
  synchronously before it returns. The test then reads `frameGeometry()` straight away.
- **The fix is at the cause and complete:** the off-screen position is computed in `displayMessage`
  (`TextFloat.cpp:134`), and the clamp sits there, at `TextFloat.cpp:142`. It covers both branches and all 31
  `displayMessage` call sites (`rg -n displayMessage src plugins`). `CommandRun.cpp:95` still passes the menu's host
  window, which is correct, so it needs no change. The knob and fader floats (`FloatModelEditorBase.cpp:137`,
  `Fader.cpp:544`) do not go through `displayMessage` and anchor to small controls. I left them unchanged on
  purpose and noted them above as out of scope.
- **No collateral damage:** the change only affects GUI placement. Project load and save, saved data, undo and
  control commands are untouched. The clamp moves a float only when part of it would fall outside the screen's
  available area. `std::max` guards a float larger than the screen, which then pins to the top-left corner of the
  area instead of producing an inverted clamp range. Every test that uses TextFloat passed (4/4,
  `11-test-family.log`).
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable; the verdict is FIXED.
- **The "gates (feature_check.py)" row in submit_check:** in round 3 the direct gate run passes 8/8 with EXIT 0
  (`12-feature-check.log`) and `submit_check.py` reports PASS with EXIT 0 (`13-submit-check.log`). The round-2
  sandbox could not create the sibling log directory; that limitation is gone, so the wrapper used in rounds 1-2 is
  no longer needed.
- **Round 3 (integration) self-check:** the integrated code is byte-identical to the accepted patch
  (`02-diff-vs-accepted.txt`, empty). The newer theme-17 line is present at `TextFloat.cpp:144-148`, after the
  clamp at `:142`. The regression is RED with the fix reverted (`07-test-red.log`, EXIT 8, GrooveMenuTest.cpp:198)
  and GREEN with it (`09-test-green.log`, EXIT 0). The TextFloat family passed 4/4 (`11-test-family.log`). The
  driver's "merge conflict" reason is the mechanical new-file fallback described above, not a content conflict.
