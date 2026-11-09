FIXED

# gui-1106-menu-navigate-nudge-clip-left - refusal float placed outside the app window

## Integration round 3 (re-verification on 050/m0-train-26 @ af010bc3cc)
Round-2 QA verdict was ACCEPT-NOTE with no blocking items (r2 `qa-session-2.log`). Its one note was about the gate-log workaround. This round's source is this branch at 56e6b7d0ec, and the target is pinned at af010bc3cc. `git merge-base --is-ancestor af010bc3cc HEAD` gave EXIT=0, so the branch already sits on the target: no conflict was left, and nothing was re-resolved. The `+`/`-` lines of `git diff af010bc3cc -- src tests` match the r2 `accepted.patch` exactly. The only lines that differ are in this report (docs/), which I left out of the comparison on purpose. Failure paths and platform hazards are unchanged from round 2 (listed below). No header changed, nothing touches the audio thread, and no command was added.

| Step (logs in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-navigate-nudge-clip-left.r3/logs) | Command | Result |
| --- | --- | --- |
| Build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > 01-build.log 2>&1` | EXIT=0 |
| GREEN, family | `cd build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > 02-test-green.log 2>&1` | EXIT=0, 100% of 2 tests passed |
| 4b revert | `git diff af010bc3cc -- src/gui/widgets/TextFloat.cpp > fix.patch && git apply -R fix.patch` (EXIT=0); `CCACHE_DISABLE=1 cmake --build build --target GrooveMenuTest -j4 > 03-build-reverted.log` (EXIT=0); `ctest -R '^GrooveMenuTest$' --output-on-failure > 04-test-reverted.log` | **EXIT=8**: `FAIL! GrooveMenuTest::aFloatOverTheMainWindowStaysInsideIt() 'bounds.contains(placed)' returned FALSE. (float 1602,0 142x76 outside window 0,0 1600x1000)` |
| 4b reapply | `git apply fix.patch` (EXIT=0); `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest -j4 > 05-build-reapplied.log` (EXIT=0); `ctest -R '^GrooveMenuTest$' --output-on-failure > 06-test-reapplied.log` | EXIT=0, 100% of 1 test passed; `git status` clean afterwards |
| Gates (as shipped) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > 07-gates.log 2>&1` | EXIT=1: `PermissionError` writing `<worktree>-feature-check/fork-sources.log` (a sibling of the worktree, outside this lane's write jail; not a gate failure) |
| Gates (log dir redirected) | `python3 -I <r3>/gatecopy/feature_check.py <worktree> > 08-gates.log 2>&1`. `diff` against the original shows exactly two changed lines: `ROOT` is pinned to the original qwen dir, and `logs` points to r3 logs/gate-logs | EXIT=0, RESULT: PASS, 8/8 (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

Files committed this round: this report only. The product, test and registration files are unchanged since 150b8e73fe.

## Integration (round 2) onto 050/m0-train-26 @ af010bc3cc
Accepted source `bugs/qwen-gui-1106-menu-navigate-nudge-clip-left` @ d290a9c0de. The driver's three-way merge reported a conflict in `src/gui/widgets/TextFloat.cpp`, then fell back to direct application, which applied all three files cleanly (`integration-apply.log`: "Applied patch to 'src/gui/widgets/TextFloat.cpp' cleanly"). No conflict needed resolving by hand. **No semantic difference from the accepted patch:** the `+`/`-` lines of the staged diff (logs/01-staged.patch) are identical to `accepted.patch` (`diff` of the two line sets: no output, EXIT=0). On this line the only parent-taking caller is still `announce()` (src/gui/CommandRun.cpp:95-96). I re-checked all 30 `displayMessage(` call sites in src/ and plugins/: every other one ends at the timeout argument, for example ArrangerFileDrop.cpp:138, ClipLockFeedback.cpp:159, MainWindow.cpp:2030 and VstEffect.cpp:150. the no-parent branch (`TextFloat.cpp:147`, `moveGlobal(mw, QPoint(32, ...))`) and `TextFloat::moveGlobal` (include/TextFloat.h:56) are unchanged.

Failure paths and supported-platform hazards, all together:
- Anchor = main window, or any widget filling its window (the finding): no room to the right or left, so the float goes over the anchor's lower left inside the window (the test checks this).
- Anchor with room to the right: placed exactly as before at `width()+2` (the test checks this).
- Anchor near the right edge with room to the left: placed to the left of it, inside the window.
- Float taller than the window: `std::max` guards keep the clamp bounds ordered (no UB in `std::clamp`), and the float is pinned to the window's top/left.
- Platforms: only `<algorithm>`, QRect, QPoint and `mapToGlobal`, the same Qt API the old `moveGlobal` used. The coordinates are global, so Wayland, X11, Windows and macOS behave as before for a top-level float (Qt and the window system still decide where a top-level actually lands on Wayland, as with the old code). No header change, nothing on the audio thread, no new command.

### Round-2 evidence (logs in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-navigate-nudge-clip-left.r2/logs)
| Step | Command | Result |
| --- | --- | --- |
| Build | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > 02-build.log` | EXIT=2: ccache could not write its temp dir (sandbox), not a compile error |
| Build | `CCACHE_DISABLE=1 cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > 03-build.log` | EXIT=0 |
| GREEN, family | `cd build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > 04-test-green.log` | EXIT=0, 100% of 2 tests passed |
| 4b revert | `git diff --cached -- src/gui/widgets/TextFloat.cpp > fix.patch && git apply -R fix.patch` (EXIT=0); rebuild 05-build-reverted.log EXIT=0; `ctest -R '^GrooveMenuTest$' --output-on-failure > 06-test-reverted.log` | **EXIT=8**: `FAIL! GrooveMenuTest::aFloatOverTheMainWindowStaysInsideIt() 'bounds.contains(placed)' returned FALSE. (float 1602,0 142x76 outside window 0,0 1600x1000)` |
| 4b reapply | `git apply fix.patch` (EXIT=0); rebuild 07-build-reapplied.log EXIT=0; `ctest -R '^GrooveMenuTest$' --output-on-failure > 08-test-reapplied.log` | EXIT=0, 100% of 1 test passed |
| Gates (as shipped) | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > 09-gates.log` | EXIT=1: `PermissionError` creating `<worktree>-feature-check` (a sibling of the worktree, outside the write jail) |
| Gates (log dir redirected) | `python3 -I <run>/gatecopy/feature_check.py <worktree> > 10-gates.log`: the copy differs only in `ROOT` (pinned to the original qwen dir) and `logs` (to logs/gate-logs), as `diff` shows | EXIT=0, RESULT: PASS, 8/8 (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

Files committed: src/gui/widgets/TextFloat.cpp, tests/src/gui/GrooveMenuTest.cpp, tests/upstream-modifications.txt, this report.

The round-1 report follows; its evidence was gathered on the source branch.

## Decision: REAL
`runCommand` -> `announce` (src/gui/CommandRun.cpp:95) calls `TextFloat::displayMessage(..., parent)` with the widget the command was run from. The parent branch placed the float at `QPoint(parent->width() + 2, 0)` from the parent's top-left (src/gui/widgets/TextFloat.cpp, formerly line 113). When the parent is the main window (or any widget filling its window) that is right of the window's right edge: measured in the offscreen app, the float sat at x=1602 with the window 0..1600 wide, so the "no clip is selected" refusal was never visible.

## Fix (src/gui/widgets/TextFloat.cpp)
New file-local `besideWithin(anchor, bounds, size)`: right of the anchor if it fits inside the anchor's top-level window, else left of it, else over the anchor's lower left (same spot as the no-parent branch: x+32, bottom-8); the top is clamped into the window. `displayMessage` uses it with `parent->window()` as bounds. A small widget well inside the window keeps the old placement exactly (beside it). No header change, no new command, nothing on the audio thread. Other callers of `displayMessage` pass no parent (unchanged branch); the `moveGlobal(this, width()+2 ...)` users (Fader, knob base, ClipView, ...) place their own floats and are not touched.

## Tests (tests/src/gui/GrooveMenuTest.cpp)
- `aFloatOverTheMainWindowStaysInsideIt`: float anchored to the main window must lie inside the window rect.
- `aFloatBesideASmallWidgetStaysBesideIt`: a small child widget still gets the float at `width()+2` (behaviour preserved).
Deterministic: geometry only, no timers (timeout 0).

## Evidence
| Step | Command (logs in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-navigate-nudge-clip-left.r1/logs) | Result |
| --- | --- | --- |
| Build, test target (pre-fix) | `cmake --build build --target GrooveMenuTest -j4 > build-2.log 2>&1` | EXIT=0 |
| RED | `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure > test-1-red.log 2>&1` | EXIT=8: `aFloatOverTheMainWindowStaysInsideIt` 'bounds.contains(placed)' FALSE (float 1602,0 142x76 outside window 0,0 1600x1000); 5 passed, 1 failed |
| Build after fix | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4 > build-3.log 2>&1` | EXIT=0 |
| GREEN, family | `ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure > test-2-green.log 2>&1` | EXIT=0, 2/2 tests passed |
| 4b revert | `git diff -- src/gui/widgets/TextFloat.cpp > fix.patch && git apply -R fix.patch`, rebuild (build-4-reverted.log) EXIT=0, `ctest -R '^GrooveMenuTest$'` (test-3-reverted.log) | EXIT=8, same assertion fails (float 1602,0) |
| 4b reapply | `git apply fix.patch`, rebuild (build-5.log) EXIT=0, `ctest -R '^GrooveMenuTest$'` (test-4-green.log) | EXIT=0 |
| Gates | feature_check.py (see note) -> gates-1.log | EXIT=0, 8/8 PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

Environment notes: ccache could not write its temp dir in this sandbox, so builds ran with `CCACHE_DISABLE=1`. `feature_check.py` writes its logs to a sibling directory of the worktree, which is outside the write jail; I ran an identical copy whose only changes are the log directory (into the run's logs/gate-logs) and `ROOT` pinned to the original location. Gate logic, limits and baselines are untouched.

Files committed: src/gui/widgets/TextFloat.cpp, tests/src/gui/GrooveMenuTest.cpp, tests/upstream-modifications.txt (reason appended to the existing TextFloat.cpp entry), this report.

## Self-check
- **RED/GREEN with the fix reverted/applied:** reverted -> `ctest -R '^GrooveMenuTest$'` EXIT=8 (float 1602,0 outside window 0,0 1600x1000); reapplied -> EXIT=0 (logs test-3-reverted.log / test-4-green.log; family GrooveMenuTest+PluginShortcutsTest 2/2 passed, test-2-green.log).
- **Deterministic:** the tests read only widget geometry right after `displayMessage(..., timeout=0, parent)`; no timers, sleeps or timestamps (timeout 0 means no auto-close; the test deletes the float itself). The offscreen main window is a fixed 1600x1000.
- **Cause and completeness:** the cause is the parent branch of `TextFloat::displayMessage` (TextFloat.cpp) assuming room to the right of the anchor. The only caller passing a parent is `announce()` (CommandRun.cpp:95), reached by every `runCommand` site; all of them benefit (menu parents resolve to the main window or a panel; panel/card parents with room still get the old beside-placement, tested). Callers without a parent use the unchanged lower-left branch; the `moveGlobal(this, width()+2 ...)` sites (Fader.cpp:544, FloatModelEditorBase.cpp:137, ClipView.cpp, SongEditor.cpp, PianoRoll.cpp) position their own SimpleTextFloat/TextFloat and are not the finding.
- **Collateral:** pure UI placement. No project/saved-data, command, undo or audio-thread code touched; no header change; no new control command (snapshot canary unchanged). Placement for an anchor with room on the right is byte-identical to before.
- **Gates:** `feature_check.py` as shipped cannot create its log directory (sibling of the worktree, outside the write jail: `PermissionError ... zene-qwen-gui-1106-menu-navigate-nudge-clip-left-feature-check`), so submit_check's gate row reports FAIL for that environmental reason. An identical copy with only the log directory redirected passed 8/8 (EXIT=0, gates-1.log and gates-2.log after the final edits).
- **Round 2 (integration, logs in the r2 run's logs/):** reverted -> `ctest -R '^GrooveMenuTest$'` EXIT=8 (06-test-reverted.log, float 1602,0 outside window 0,0 1600x1000); reapplied -> EXIT=0 (08-test-reapplied.log); family GrooveMenuTest+PluginShortcutsTest 2/2 EXIT=0 (04-test-green.log). Same seam: geometry read right after `displayMessage(..., timeout 0, parent)`. No timer fires, and nothing sleeps or races. All 30 `displayMessage(` call sites re-checked: only CommandRun.cpp:95-96 passes a parent. The integrated diff is identical to the accepted patch. submit_check's gate row is FAIL for the same environmental reason as round 1 (09-gates.log: `PermissionError` creating `<worktree>-feature-check` outside the write jail). The gate logic run with only `ROOT` and the log directory redirected gave RESULT: PASS, 8/8, EXIT=0 (10-gates.log).
- **ALREADY-FIXED / FALSE-POSITIVE:** not applicable. The verdict is FIXED, and the bug reproduces on af010bc3cc without the patch (06-test-reverted.log).
- **Round 3 (re-verification, logs in the r3 run's logs/):** with the fix reverted, `ctest -R '^GrooveMenuTest$'` gave EXIT=8 (04-test-reverted.log, float 1602,0 outside window 0,0 1600x1000). With it reapplied: EXIT=0 (06-test-reapplied.log). Family GrooveMenuTest+PluginShortcutsTest: 2/2, EXIT=0 (02-test-green.log). The seam is the same: the test reads geometry right after `displayMessage(..., timeout 0, parent)`, with no timers and no sleeps. The product, test and registration files are byte-identical to round 2 and to the accepted patch, so the caller and sibling analysis above still holds: CommandRun.cpp:95-96 is the only caller that passes a parent. Collateral damage is still none: placement is the only change, with no saved data, limits or commands touched. submit_check's gate row is FAIL only because `feature_check.py` as shipped raises `PermissionError` writing `<worktree>-feature-check/` outside the write jail (07-gates.log). The same gate logic, with only `ROOT` and the log directory redirected, gave RESULT: PASS, 8/8, EXIT=0 (08-gates.log). Round-2 QA also ran the prescribed command directly: EXIT=0, 8/8.
