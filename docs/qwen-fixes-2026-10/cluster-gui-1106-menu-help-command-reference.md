FIXED

# gui-1106-menu-help-command-reference - Command Reference opens behind existing dialogs

## Verdict: REAL

`HelpCenter::showHelp` (`src/gui/HelpCenter.cpp:227-244`) showed and raised the window but never activated it
(`QDialog::show()` + `raise()` at :241-242). Help > Command Reference reaches it through `addCommandAction(... "app.help")`
(`HelpCenter.cpp:480`) -> `ControlCommandsHelp.cpp:84` -> `showHelp`; the other callers
(`WorkspaceWindows.cpp:76`, `ControlCommandsHelp.cpp:65`) use the same function, so one fix covers all of them.
`raise()` only changes stacking; a window that is already visible does not take focus, so About / Keyboard Shortcuts
stayed the active windows (and on many window managers `raise()` of a non-active window is ignored). Every sibling that
re-opens an existing window in `src/gui` pairs `raise()` with `activateWindow()` (e.g. `MixerRoutingMatrix.cpp:218`,
`InstrumentTuningView.cpp:96`, `MidiClipView.cpp:172`).

## Fix

`src/gui/HelpCenter.cpp:243`: `s_center->activateWindow();` after `raise()`. Nothing else changes. `GuidedTour::raise()`
(:410) is a different widget (an overlay child of the main window), not in scope.

## Integration (r2: target 050/m0-train-26 @ af010bc3cc)

The accepted patch (`bugs/qwen-gui-1106-menu-help-command-reference` @ b515308) was applied here with a three-way
merge. Its merge base with the pinned target is af010bc3cc itself, so the merge is clean: the working tree is
byte-identical to the accepted branch head (`git diff b515308 --stat` is empty, 0 files). No conflict resolution
changed a line and no semantic difference from the accepted patch was introduced.

The driver's stated conflict - "the newer line in src/gui/HelpCenter.cpp" - is real on the newer train
(050/m0-train-27), which already carries an equivalent fix at the same spot with a different comment:

```diff
+	// Raise AND activate: an already-open centre behind other open windows (About, Keyboard Shortcuts) stays
+	// behind them when only raised, so Help > Manual seemed to do nothing (gui-1106).
 	s_center->raise();
+	s_center->activateWindow();
```

Merging the accepted patch into train-27 would conflict textually there, but the pinned target is train-26,
where that newer line does not exist; the accepted design (comment + call after `raise()`) is retained verbatim.
No new design was needed, so this is not BLOCKED.

Affected failure paths (all reach the one fixed function):
- Help > Command Reference: `app.help` + `reference=true` -> `ControlCommandsHelp.cpp:84` -> `showHelp`.
- Help > Manual: `app.help` -> `ControlCommandsHelp.cpp:84` -> `showHelp`.
- F1 / Help for This: `app.help` context -> `ControlCommandsHelp.cpp:65` -> `showHelp`.
- A manual link: `topic:` URL -> `WorkspaceWindows.cpp:76` -> `showHelp`.

Supported-platform hazards:
- `activateWindow()` is a no-op when the application itself is not active (macOS/Windows foreground rules); the
  finding is about a window already open inside the same app, so this does not regress it.
- Offscreen platform: `QApplication::activeWindow()` follows the platform activation event; the test's
  `QTRY_COMPARE` waits for that event rather than a fixed delay.
- Qt 5.15 / MSVC / macOS < 10.15: `QWidget::activateWindow()` exists in Qt 5.15; no Qt-6-only API is added.
- No audio-thread, undo, command-registration or saved-data code is touched.

## Regression test

`tests/src/gui/HelpAccessTest.cpp` `theHelpCenterIsActivatedOverOpenDialogs`: open the Help Center, then show two
top-level dialogs (About / shortcuts stand-ins) and activate the second, call `showHelp(QString(), true)` again and
require `QApplication::activeWindow()` to be the Help Center. No timing: `QTRY_COMPARE` only waits for the platform's
activation event. A first show of a window activates under the offscreen platform anyway (my first version of the test,
which showed the Help Center after the dialogs, passed without the fix - discarded), so the test uses the
already-visible case the finding describes.

## Evidence (logs in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-help-command-reference.r1/logs)

Note: ccache's temp dir (`/home/kruzzzzy/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`) is not writable in this
sandbox (build-1.log, EXIT=2), so every build below ran with `CCACHE_DISABLE=1`.

| Step | Command | EXIT | Result |
|---|---|---|---|
| RED (original code) | `cmake --build build --target HelpAccessTest -j4` (build-3.log) then `cd build/tests && ctest -R '^HelpAccessTest$' --output-on-failure` (test-red.log) | build 0, ctest 8 | FAIL! theHelpCenterIsActivatedOverOpenDialogs: Compared QObject pointers are not the same, Actual QDialog, Expected HelpCenter; 18 passed, 1 failed |
| GREEN (fix) | build-4.log, then `ctest -R Help` (test-green.log) | build 0 | HelpAccessTest Passed (1 test run; AccessibilityHelperTest, OnlineHelpNoticeTest, SfzSimdHelperAlignmentTest matched the pattern but are not built targets - "Not Run", unrelated) |
| 4b revert fix | `git diff -- src/gui/HelpCenter.cpp > logs/fix.patch && git apply -R logs/fix.patch`, rebuild (build-5.log), ctest `^HelpAccessTest$` (test-red2.log) | revert 0, build 0, ctest 8 | FAIL! same assertion |
| 4b re-apply | `git apply logs/fix.patch`, rebuild (build-6.log), ctest `^HelpAccessTest$` (test-green2.log) | apply 0, build 0, ctest 0 | 100% tests passed out of 1 (19 test functions pass) |
| Gates | `feature_check.py` (gates-1.log) | 0 | 8/8 PASS, RESULT PASS |

Gate note: `feature_check.py` hardcodes its log folder to a sibling of the worktree, which this sandbox cannot create
(PermissionError, first attempt). I ran a copy whose only changes are the log directory (redirected to
`logs/fc-logs`) and `ROOT` (pointed at the real qwen dir for FIX-BASE and the existing baseline). Gate commands, limits and
baselines are unchanged.

## Integration evidence (logs in /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1106-menu-help-command-reference.r2/logs)

| Step | Command | EXIT | Result |
|---|---|---|---|
| GREEN build (fix) | `cmake --build build --target zene HelpAccessTest -j4` (build-1.log) | 0 | zene + HelpAccessTest built |
| GREEN test | `cd build/tests && ctest -R '^HelpAccessTest$' --output-on-failure` (test-1.log) | 0 | 1/1 passed (19 test functions) |
| 4b product patch | `git diff HEAD -- src/gui/HelpCenter.cpp > logs/fix.patch` | 0 | 12-line patch, one added line |
| 4b revert | `git apply -R logs/fix.patch` | 0 | `activateWindow` gone from HelpCenter.cpp |
| 4b RED build | `cmake --build build --target HelpAccessTest -j4` (build-2.log) | 0 | rebuilt without the fix |
| 4b RED test | `ctest -R '^HelpAccessTest$' --output-on-failure` (test-2.log) | 8 | FAIL! theHelpCenterIsActivatedOverOpenDialogs: Compared QObject pointers are not the same (Actual QDialog, Expected HelpCenter); 0% tests passed, 1 failed out of 1 |
| 4b re-apply | `git apply logs/fix.patch` | 0 | fix restored at HelpCenter.cpp:243 |
| 4b GREEN build | `cmake --build build --target HelpAccessTest -j4` (build-3.log) | 0 | rebuilt with the fix |
| 4b GREEN test | `ctest -R '^HelpAccessTest$' --output-on-failure` (test-3.log) | 0 | 1/1 passed |
| Gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (gates-1.log) | 0 | 8/8 PASS, RESULT PASS |

Gate note (r2): the unmodified `feature_check.py` was run here; the parent directory is writable, so it created its
sibling log folder and exited 0, 8/8 PASS (gates-1.log). No path-redirected copy was needed for r2.

Registration: `src/gui/HelpCenter.cpp` is already in `tests/fork-sources.txt:2090`; `HelpAccessTest.cpp` already has
entries in `tests/upstream-modifications.txt`; `fork-sources` and `no-upstream-regression` gates pass.

Files committed: `src/gui/HelpCenter.cpp`, `tests/src/gui/HelpAccessTest.cpp`,
`docs/qwen-fixes-2026-10/cluster-gui-1106-menu-help-command-reference.md`.

## Self-check

- **RED/GREEN with the product change reverted/applied (r2):** `git apply -R logs/fix.patch` -> rebuild -> ctest exit 8,
  "Compared QObject pointers are not the same" (test-2.log); `git apply logs/fix.patch` -> rebuild -> ctest exit 0
  (test-3.log). The r1 session saw the same pair (test-red2.log / test-green2.log).
- **Deterministic:** no sleeps, timers or clock values. The only wait is `QTRY_COMPARE` on `QApplication::activeWindow()`,
  which polls for the window-system activation event rather than a fixed delay; the setup is explicit
  (`show()` + `activateWindow()` on the stand-in dialog, then verified active before the call under test).
- **Complete / siblings:** all three callers of `HelpCenter::showHelp` (`ControlCommandsHelp.cpp:65`, `:84`,
  `WorkspaceWindows.cpp:76`) and the Help menu's Command Reference action (`HelpCenter.cpp:480` -> `app.help`) go through
  the one function that now activates. `GuidedTour` (`HelpCenter.cpp:410`) is a different widget and is untouched.
- **No collateral damage:** one `activateWindow()` call on a UI dialog; no saved data, project format, command,
  audio-thread code or undo is touched, and no limit or check was added.
- **Verdict is FIXED (not ALREADY-FIXED / FALSE-POSITIVE):** evidence in "Verdict: REAL" above.
- **Integration:** the working tree is byte-identical to the accepted branch head (`git diff b515308 --stat` empty);
  the train-27 "newer line" is an equivalent fix at the same spot, and the pinned train-26 target does not have it,
  so the merge is clean and the accepted design is retained. See "Integration" above.
- **Gates (r2):** the unmodified `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exited 0,
  8/8 PASS (gates-1.log).
