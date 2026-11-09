FIXED

# gui-1105-menu-navigate-previous-marker - "Previous Marker" notification is positioned off-screen

## Verdict: REAL

Navigate > Previous Marker with no marker runs `marker.jump`, which refuses ("no marker to jump to",
`src/core/ControlCommandsMarkers.cpp:211`). The menu action goes through `runCommand(hostOf(menu), ...)`
(`src/gui/CommandRun.cpp:128`), `hostOf` walks up the menus to the **main window**, and `announce`
(`CommandRun.cpp:95`) passes that as `parent` to `TextFloat::displayMessage`. With a parent, the float was
moved to `QPoint(parent->width() + 2, 0)` (`src/gui/widgets/TextFloat.cpp:113`, before the fix): 2 px right of
the main window, outside the app. Same for every command run from a main-menu action (the parent is a window).

## Fix

`TextFloat::displayMessage` (`src/gui/widgets/TextFloat.cpp:111-121`): when the parent is itself a window
(`parent->isWindow()`), the message is floated over the lower left of that window - exactly the placement the
no-parent branch already used for the main window. Children (knobs, faders, clips, sliders: all non-window
widgets) keep the "right of the widget" placement, so nothing else moves. No signature change, so no header,
no audio-thread code, no new command, no undo/save-format effect.

Siblings checked: other `displayMessage` callers with a parent (`CommandRun.cpp:95`, `ControllerTemplateMenu.cpp`,
`ClipView.cpp`, `FileBrowser.cpp`, editors) - widget parents are unchanged, window parents get the fixed placement.
`moveGlobal` callers that place the other TextFloat instances (Fader, Knob, ClipView, SongEditor) do not go
through `displayMessage` and are not touched.

## Tests (tests/src/gui/GrooveMenuTest.cpp)

- `aMessageOverTheMainWindowIsShownInsideIt`: `displayMessage(..., parent = mainWindow)`; asserts the float's
  geometry lies inside the main window's frame. Deterministic (no timers; timeout 0).
- `aMessageOverAChildWidgetStaysBesideIt`: guards the unchanged child placement (`child.mapToGlobal(width+2, 0)`).

## Integration (round 2)

Accepted source `bugs/qwen-gui-1105-menu-navigate-previous-marker @ 5206c813ab` re-applied onto
`050/m0-train-26 @ af010bc3cc` (the three-way merge conflicted in `TextFloat.cpp`; the direct application of the
accepted patch applied all three files cleanly: `integration-apply.log`). The accepted design and both regression
tests are unchanged; no new design was needed.

## Evidence (all re-run on this integration tree; logs in the run's `logs/`)

Environment note: ccache's temp dir is not writable in this sandbox, so builds ran with
`CCACHE_TEMPDIR=<log folder>/cctmp` (`build-1.log`, EXIT=2, was that permission error, not code).
`feature_check.py` cannot create `<worktree>-feature-check/` (PermissionError, `gates-1.log`, EXIT=1), so the gates
ran through `feature_check_copy.py`, which differs only in the log directory and the baseline-cache directory
(copied from the original; same gate commands).

| Step | Command | Exit | Result |
|---|---|---|---|
| build | `cmake --build build --target zene GrooveMenuTest -j4` (`build-2.log`) | 0 | |
| GREEN | `cd build/tests && ctest -R '^GrooveMenuTest$' --output-on-failure` (`test-green.log`) | 0 | 1/1 passed |
| 4b revert | `git diff HEAD -- src/gui/widgets/TextFloat.cpp > fix.patch && git apply -R fix.patch` | 0 | |
| 4b build | `cmake --build build --target zene GrooveMenuTest -j4` (`build-3.log`) | 0 | |
| 4b RED | `ctest -R '^GrooveMenuTest$' --output-on-failure` (`test-red.log`) | 8 | FAIL `aMessageOverTheMainWindowIsShownInsideIt`: message 1602,0 158x76 is outside the window -2,-2 1604x1004 |
| 4b reapply | `git apply fix.patch` | 0 | |
| 4b build | `cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4` (`build-4.log`) | 0 | |
| 4b GREEN | `ctest -R '^GrooveMenuTest$' --output-on-failure` (`test-green2.log`) | 0 | 1/1 passed |
| family | `ctest -R '^(PluginShortcutsTest|GrooveMenuTest)$' --output-on-failure` (`test-family.log`) | 0 | 2/2 passed (the tests that include TextFloat) |
| gates | `python3 feature_check.py <worktree>` (`gates-1.log`) | 1 | sandbox PermissionError creating its log dir, no gate ran |
| gates | `python3 feature_check_copy.py <worktree>` (`gates-2.log`) | 0 | RESULT: PASS, all 8 rows PASS |

Registration: `tests/upstream-modifications.txt` gets a `src/gui/widgets/TextFloat.cpp` line with the reason
(the `GrooveMenuTest.cpp` line is `# `-style). No new files.

Files committed: `src/gui/widgets/TextFloat.cpp`, `tests/src/gui/GrooveMenuTest.cpp`,
`tests/upstream-modifications.txt`, this report.

## Round 3 re-verification (2026-11-09, logs in `runs/gui-1105-menu-navigate-previous-marker.r3/logs/`)

The round-3 brief repeats the integration job; the integration commit `6f54236c15` was already on this branch, so
nothing was re-merged. Semantic check against the accepted patch: the changed `+`/`-` lines of
`git diff af010bc3cc HEAD` and of the accepted branch's diff from the same base (`af010bc3cc`) are identical for
`src/gui/widgets/TextFloat.cpp` and `tests/src/gui/GrooveMenuTest.cpp` (`diff` of the two printed nothing). No new
design; accepted tests retained unchanged.

Affected failure paths and platform hazards (together): (1) window parent - every `runCommand` announcement from a
main-menu or window-level menu (`CommandRun.cpp:95`), the bug path, now inside the window; (2) widget parent - knobs,
faders, clips, file browser: unchanged right-of placement (guarded by `aMessageOverAChildWidgetStaysBesideIt`);
(3) no parent - unchanged (main window lower left). Platform: pure Qt geometry (`isWindow`, `height`, `moveGlobal`),
no platform-specific API; on Wayland, where a top-level cannot be positioned globally, both old and new placements
are equally advisory (general Qt behaviour, not verified here). Not checked on macOS/Windows hosts (no hosted matrix run here).

| Step | Command | Exit | Result |
|---|---|---|---|
| build | `CCACHE_TEMPDIR=<logs>/cctmp cmake --build build --target zene GrooveMenuTest PluginShortcutsTest -j4` (`build-1.log`) | 0 | |
| family | `cd build/tests && ctest -R '^(GrooveMenuTest\|PluginShortcutsTest)$' --output-on-failure` (`test-1-family.log`) | 0 | 2/2 passed |
| 4b revert | `git diff af010bc3cc HEAD -- src/gui/widgets/TextFloat.cpp > fix.patch && git apply -R fix.patch` | 0 | |
| 4b build | `cmake --build build --target zene GrooveMenuTest -j4` (`build-2-reverted.log`) | 0 | |
| 4b RED | `ctest -R '^GrooveMenuTest$' --output-on-failure` (`test-2-red.log`) | 8 | FAIL `aMessageOverTheMainWindowIsShownInsideIt`: message 1602,0 158x76 is outside the window -2,-2 1604x1004 |
| 4b reapply | `git apply fix.patch` | 0 | |
| 4b build | `cmake --build build --target zene GrooveMenuTest -j4` (`build-3-fixed.log`) | 0 | |
| 4b GREEN | `ctest -R '^GrooveMenuTest$' --output-on-failure` (`test-3-green.log`) | 0 | 1/1 passed |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (`gates-1.log`) | 1 | PermissionError writing `<worktree>-feature-check/fork-sources.log` (write jail), no gate ran |
| gates | `python3 runs/.../gatecopy/feature_check_copy.py <worktree>` (`gates-3.log`) | 0 | RESULT: PASS, 8/8 rows PASS (base af010bc3c, 4 changed files) |
| submit | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> gui-1105-menu-navigate-previous-marker` (`submit-1.log`) | 1 | only FAIL is the gates row (same PermissionError) |

`feature_check_copy.py` differs from the original in exactly two lines (`diff` shown in the session): `ROOT` points at
the original `qwen/` directory (so `FIX-BASE` and the `gate-baselines` cache are the original ones) and the log
directory is `<logs>/feature-check`. Same gate commands and pass rules.

## Self-check

- **RED with the fix reverted, GREEN with it (exit codes):** reverted (`git apply -R fix.patch`) -> rebuilt -> `ctest -R '^GrooveMenuTest$'` EXIT=8 (`test-red.log`, same assertion "message 1602,0 158x76 is outside the window -2,-2 1604x1004"); reapplied -> rebuilt -> EXIT=0 (`test-green2.log`).
- **Deterministic:** no timers, sleeps or races. `displayMessage` is called with `timeout = 0` (no close timer), and the assertion compares the float's geometry with the main window's `frameGeometry()` right after the synchronous `show()` + `move()` inside `displayMessage`. The offscreen platform gives fixed geometry (1604x1004).
- **Fix at the cause and complete:** the cause is the placement in `TextFloat::displayMessage` (`TextFloat.cpp:111-121`) for a parent that is a window. Callers with a parent: `CommandRun.cpp:95` (main-menu and editor-menu commands - the bug path), `ControllerTemplateMenu.cpp:71` (no parent), `ClipView.cpp`, `FileBrowser.cpp`, editors (widget parents -> unchanged). The same function serves every command-run announcement, so Previous/Next Marker and all other main-menu refusals are fixed together. The other `moveGlobal` users do not go through `displayMessage`.
- **No collateral damage:** UI placement only. No project/save data, no command, no header, no audio thread, no undo. Widget-parented floats keep the exact old position (guarded by `aMessageOverAChildWidgetStaysBesideIt`). The new position uses the same offsets the no-parent branch already used.
- **ALREADY-FIXED / FALSE-POSITIVE:** not applicable (verdict FIXED).
- **Gate row of `submit_check.py`:** it calls `feature_check.py`, which cannot create `<worktree>-feature-check/` in this sandbox (PermissionError, `submit-1.log` / `gates-1.log`). The identical gates run through the log-dir-only copy of the script all PASS (`gates-2.log`, EXIT=0).
- **Round 3:** RED EXIT=8 (`test-2-red.log`) / GREEN EXIT=0 (`test-3-green.log`), family 2/2 EXIT=0 (`test-1-family.log`), gates 8/8 PASS through the copy EXIT=0 (`gates-3.log`); the original `feature_check.py` and `submit_check.py` gate row still fail only on the write-jail PermissionError (`gates-1.log`, `submit-1.log`).
