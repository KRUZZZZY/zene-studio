FIXED (the silence); the refusal itself is intended behaviour and is kept

# gui-1030-menu-help-command-reference — Help > Command Reference closes with no word in an unattended session

## Decision and evidence

The finding has two halves; they get different verdicts.

1. **"No Help window opens" — NOT a bug, by contract (kept).** `app.help` is registered with `requires: display`
   (`src/gui/ControlCommandsHelp.cpp:151-160`, `add(..., display=true, ...)` sets `requiresDecl = {"display"}` at `:142`).
   `ControlRegistry::checkRequires` refuses a `display` command whenever `m_headless` is set (`src/core/ControlRegistry.cpp:232-235`),
   and `m_headless = isUnattendedRun()` (`:158`), which is true for `--control-socket` and for the offscreen/minimal/vnc
   platforms (`src/core/UnattendedRun.cpp`: `isUnattendedRun()`). The existing test says so on purpose: it must switch headless
   off to invoke `app.help` (`tests/src/gui/HelpAccessTest.cpp` `theHelpAndATourOpenFromTheirCommands`: "the offscreen platform is
   a display (both declare requires: display)"). Making the menu open the window anyway would change the A13 contract
   (no windows for an agent instance), so I did not.
2. **"Silently" — REAL.** The menu action calls `runCommand` -> `announce` (`src/gui/CommandRun.cpp`). `announce` returned at once
   when `isHeadless()`, so the refusal reached neither a floating message nor the log (the finding's `zene log: []`). The sibling
   menus do log an unattended refusal (`src/gui/TrackRenderMenu.cpp:53` `runBusy`, `TrackFolderMenu.cpp:49`
   `run`, `SessionTuningMenu.cpp:57`: `qWarning("%s refused: %s", ...)`); the shared helper did not.

## Fix

`src/gui/CommandRun.cpp` `announce()` calls a new file-local `logHeadlessRefusal()` before its headless early return: a failed
result in a headless registry is logged with `qWarning("%s refused: %s", call.id, result.errorMessage)`. (First version put the
`if` inline in `announce`, which made it CCN 11 - a new over-target function per the complexity gate - so it was extracted.) This is the one place every `runCommand`/`addCommandAction`/
`addCommandChoices` menu, the CommandForm (`CommandForm.cpp:371`), `GrooveMenu.cpp:76` and `RecordLaunchMenus.cpp:85,90` report a
failure through, so all of them become consistent. No floating message is added to headless sessions, no command is added or
changed, nothing runs on the audio thread, the menu action's enabled state is unchanged (disabling display-only items in
every headless GUI would be a wider behaviour change; not done).

## Regression test

`tests/src/gui/HelpAccessTest.cpp` `aRefusedHelpMenuItemSaysSoInTheLog`: builds a menu bar with a Help menu, runs the real
`installHelpEntries` on it, forces the registry headless, triggers the real "Command Reference" action, expects the
`app.help refused: ... requires a display` warning (`QTest::ignoreMessage`, so a missing message fails the test), and checks
no `HelpCenter` window was opened (contract preserved). No timing or sleeps.

## Commands and results (logs in `.../runs/gui-1030-menu-help-command-reference.r1/logs/`)

Environment note: ccache could not write its temp dir under the write-jail (`build-1.log`: "Permission denied"), so every
build used `CCACHE_DISABLE=1`.

| Step | Command | EXIT |
| --- | --- | --- |
| first build (ccache denied, env only) | `cmake --build build --target HelpAccessTest -j4` (`build-1.log`) | 2 |
| build with test | `CCACHE_DISABLE=1 cmake --build build --target HelpAccessTest -j4` (`build-3.log`) | 0 |
| RED (no fix) | `cd build/tests && ctest -R '^HelpAccessTest$' --output-on-failure` (`test-2-RED.log`: "Did not receive any message matching: app\.help refused: .*requires a display") | 8 |
| fix, build | `CCACHE_DISABLE=1 cmake --build build --target HelpAccessTest CommandFormTest -j4` (`build-4.log`) | 0 |
| GREEN | `ctest -R '^(HelpAccessTest\|CommandFormTest)$'` (`test-3-GREEN.log`, 2 tests, 100% passed) | 0 |
| 4b revert fix | `git diff -- src/gui/CommandRun.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b rebuild + run | `CCACHE_DISABLE=1 cmake --build build --target HelpAccessTest` (`build-5.log`) then `ctest -R '^HelpAccessTest$'` (`test-4-revert-RED.log`, 0% passed, 1 failed) | build 0 / ctest 8 |
| 4b reapply | `git apply logs/fix.patch` | 0 |
| 4b rebuild + run | `build-6.log`, then `ctest -R '^HelpAccessTest$'` (`test-5-reapply-GREEN.log`, 1 test passed) | build 0 / ctest 0 |
| family | `ctest -R '^(RecordModeMenuTest\|GrooveMenuTest\|CommandFormTest\|HelpAccessTest)$'` (`test-6-family.log`, 4 tests, 100% passed; the other `announce` callers) | 0 |
| gates, working tree (inline version) | `python3 logs/feature_check_local.py <worktree>` (`gates-2.log`): 8/8 PASS | 0 |
| gates, after commit (inline version) | same (`gates-3.log`): complexity FAIL `announce` CCN 11 - fixed by extracting `logHeadlessRefusal` | 1 |
| rebuild + family after extraction | `CCACHE_DISABLE=1 cmake --build build --target HelpAccessTest CommandFormTest RecordModeMenuTest GrooveMenuTest -j4` (`build-8.log`) then the family ctest (`test-7-family-final.log`, 4 tests, 100% passed) | 0 / 0 |
| gates, final | see Self-check (`gates-4.log`) | |

Gate note: the stock `feature_check.py` exits 1 here (`gates-1.log`) because it tries to create
`<worktree>-feature-check/` next to the worktree, which the write-jail forbids. I ran a copy (`logs/feature_check_local.py`) that
differs in exactly two lines: `ROOT` is the script's real directory and `logs` is the run's log folder. Gates, limits and the
cached base listing are untouched.

Files committed: `src/gui/CommandRun.cpp`, `tests/src/gui/HelpAccessTest.cpp`, `tests/upstream-modifications.txt`
(a reason line for `HelpAccessTest.cpp`; `CommandRun.cpp` is already in `tests/fork-sources.txt:2026`), this report.

## Round 0 note

The brief says earlier commits exist on this branch; there were none for this cluster (`git log` shows the train tip, a clean
tree). Reproduced first: the RED run above is the original behaviour.

## Self-check
- **RED/GREEN with the product change reverted:** yes. Reverted `CommandRun.cpp`: ctest EXIT=8 ("Did not receive any message matching ...", `test-4-revert-RED.log`); reapplied: EXIT=0 (`test-5-reapply-GREEN.log`). Done on the inline version of the fix; the later extraction into `logHeadlessRefusal` has identical behaviour and the final family run is green (`test-7-family-final.log`).
- **Deterministic:** no timers, sleeps or clocks. The seams are `ControlRegistry::setHeadless(true)` (the registry's own flag, restored at the end) and `QTest::ignoreMessage`, which fails the test when the warning is not emitted. The Help entries are installed synchronously by calling `installHelpEntries` on a local `QMenuBar` (the main window installs them from a zero timer).
- **Cause and callers:** the cause is `announce()`, the single reporting point for `runCommand`, `addCommandAction`, `addCommandChoices`, `CommandForm.cpp:371`, `GrooveMenu.cpp:76`, `RecordLaunchMenus.cpp:85,90`. All now log an unattended refusal as `TrackRenderMenu`/`TrackFolderMenu`/`SessionTuningMenu` already did. `GrooveMenuTest`, `RecordModeMenuTest`, `CommandFormTest` pass.
- **Collateral damage:** none to saved data (no project/config format touched). Behaviour change is one extra `qWarning` line on stderr when a command is refused in a headless registry; no new floating message, no new limit, no command added (canary untouched), nothing on the audio thread (GUI-thread menu code).
- **Verdict basis (contract kept):** `ControlCommandsHelp.cpp:142,151-160`, `ControlRegistry.cpp:158,232-235`, `UnattendedRun.cpp`, and the existing `HelpAccessTest` comment that app.help needs headless off. Not changed.
- **Open for the owner (not decided by me):** if an unattended GUI sweep is meant to be able to open the Help window, that is a contract change to `requires: display` / the A13 rule, or a sweep that clears the headless flag; the menu item also still shows enabled in such a session. `submit_check.py`'s gates row reports FAIL only because the stock `feature_check.py` cannot create `<worktree>-feature-check/` under the write-jail (PermissionError, `gates-1.log`); the equivalent run from the redirected copy is in `gates-4.log`.
