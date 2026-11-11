FIXED

# Cluster gui-1105-menu-help-command-reference: Command Reference is refused in a visible agent GUI

## Verdict: REAL, fixed at the cause (integration retry onto 050/m0-train-26)

The accepted fix (`9c145d673883af98ee144f102da171a022798923`) is applied on the target commit
`af010bc3cc055d477045a54e2d866f59a95017d7` by the integration driver (`integration-apply.log`: every
file "Applied ... cleanly"; `integration-evidence.json` records `source_base == target_commit`). This
round re-proves it from scratch and commits it on `integration/gui-1105-menu-help-command-reference-af010bc3cc`.
The round-1 independent review was ACCEPT-NOTE with no blocking entries; its only note was that no live
desktop click was performed (the machine runs offscreen), which this report repeats honestly.

## Cause (file:line evidence)

- `app.help` declares `requires: display`: `src/gui/ControlCommandsHelp.cpp:142` turns `display=true`
  into `requiresDecl = {"display"}`, and the `app.help` registration passes it at
  `src/gui/ControlCommandsHelp.cpp:151-157`.
- The registry's only flag used to be `m_headless = isUnattendedRun()`
  (`src/core/ControlRegistry.cpp:160`). `isUnattendedRun()` is
  `s_agentInstance || platformHasNoDisplay()` (`src/core/UnattendedRun.cpp:81-84`), so it is true for
  any `--control-socket` run (`src/core/main.cpp:1264`) even on a real desktop.
- `checkRequires()` refused `"display"` whenever that single flag was true (now
  `src/core/ControlRegistry.cpp:237`), and `src/gui/CommandRun.cpp:90` then hid the refusal because
  `isHeadless()` was true - hence "menu closes, no window".
- The intended contract: SPEC A13 (`research/ableton-gap/SPEC-zene-studio.md:130`) reserves
  `requires: display` for commands that genuinely need a display. An agent instance on a desktop has
  one. The widening in `a244564d49` was for modal suppression (#625), not for the display requirement.

## The fix

Two concepts, two flags:

- `m_headless = isUnattendedRun()` (`src/core/ControlRegistry.cpp:160`): unchanged. It still suppresses
  GUI feedback and modals for agents and displayless runs (`src/gui/CommandRun.cpp:90`,
  `src/gui/CommandMetadata.cpp:446`, `src/gui/WorkflowRun.cpp:146`,
  `src/gui/ControlCommandsTheme.cpp:301`, `src/gui/ControlCommandsArrangerView.cpp:335`).
- `m_noDisplay = isDisplaylessPlatform()` (`src/core/ControlRegistry.cpp:163`): new. `checkRequires()`
  uses only this for `"display"` (`src/core/ControlRegistry.cpp:237`).
- `isDisplaylessPlatform()` is a thin exported wrapper over the existing `platformHasNoDisplay()`
  (`include/UnattendedRun.h:51`, `src/core/UnattendedRun.cpp:76-79`), so the platform test still lives
  in one place.
- The test seam `setHeadless(bool)` sets both flags (`include/ControlRegistry.h:176`), preserving the
  meaning of every existing test caller ("unattended + displayless" vs "attended on a display").

Unchanged: offscreen/minimal/vnc runs (both flags true), desktop runs without `--control-socket`
(both false), and `requires: human` (still always refused). No command was added, nothing is
serialised, no audio-thread or undo path changed.

## Test matrix

`tests/src/core/ControlRegistryDisplayTest.cpp` (GUI-less, `QTEST_GUILESS_MAIN`):

| path | stimulus | expected |
|---|---|---|
| normal: agent on a display | `setAgentInstance(true)`, `QT_QPA_PLATFORM` unset; invoke a fresh command with `requiresDecl={"display"}` | runs (`result.ok`), and `isHeadless()` is still true so modals stay suppressed |
| failure: agent without a display | agent + `QT_QPA_PLATFORM=offscreen` | refused, `ControlErrorKind::Requires`, no handler run |
| reconfiguration seam | `setHeadless(true/false)` | sets both flags; existing callers keep their meaning (five affected tests below) |
| audio thread | none of the changed code is on an audio path (`checkRequires` runs on command invocation; no allocation/lock/log added) | no realtime constraint touched |
| supported platforms | `platformHasNoDisplay()` reads `QT_QPA_PLATFORM` before a QGuiApplication and `QGuiApplication::platformName()` afterwards; offscreen/minimal/vnc are displayless, xcb/wayland/windows/cocoa are not | unchanged from before the fix; Qt 5.15-compatible API only |

The platform check is deterministic in the test because there is no `QGuiApplication` under
`QTEST_GUILESS_MAIN`, so the environment variable is the only platform source
(`src/core/UnattendedRun.cpp:47-60`); the value is saved in `init()` and restored in `cleanup()`.

## Evidence (all logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-command-reference.r3/logs`)

| step | exact command | result |
|---|---|---|
| build zene (fixed tree) | `CCACHE_DISABLE=1 cmake --build build --target zene -j4 > logs/build-1-green.log 2>&1; echo EXIT=$?` | EXIT=0 |
| build regression target | `cmake --build build --target ControlRegistryDisplayTest -j4 > logs/build-2-test-green.log 2>&1; echo BUILD_EXIT=$?` | BUILD_EXIT=0 |
| GREEN before reverse | `cd build/tests && ctest -R '^ControlRegistryDisplayTest$' -V --output-on-failure > logs/test-1-green.log 2>&1; echo TEST_EXIT=$?` | TEST_EXIT=0, Totals 4 passed / 0 failed |
| 4b save product hunks | `git diff -- include/ControlRegistry.h include/UnattendedRun.h src/core/ControlRegistry.cpp src/core/UnattendedRun.cpp > logs/fix.patch; echo PATCH_EXIT=$?` | PATCH_EXIT=0 (91 lines) |
| 4b revert | `git apply -R logs/fix.patch; echo REVERT_EXIT=$?` | REVERT_EXIT=0 |
| 4b rebuild reverted | `cmake --build build --target ControlRegistryDisplayTest -j4 > logs/build-3-reverted.log 2>&1; echo BUILD_EXIT=$?` | BUILD_EXIT=0 |
| 4b RED | `cd build/tests && ctest -R '^ControlRegistryDisplayTest$' -V --output-on-failure > logs/test-2-red.log 2>&1; echo TEST_EXIT=$?` | TEST_EXIT=8; `FAIL! agentInstanceOnDisplayRunsDisplayCommands() 'result.ok' returned FALSE. (command 'test.needs_display' requires a display (headless instance))`; 3 passed, 1 failed |
| 4b reapply | `git apply logs/fix.patch; echo APPLY_EXIT=$?` | APPLY_EXIT=0 |
| 4b rebuild reapplied | `cmake --build build --target ControlRegistryDisplayTest -j4 > logs/build-4-reapplied.log 2>&1; echo BUILD_EXIT=$?` | BUILD_EXIT=0 |
| 4b GREEN | `cd build/tests && ctest -R '^ControlRegistryDisplayTest$' -V --output-on-failure > logs/test-3-green-reapplied.log 2>&1; echo TEST_EXIT=$?` | TEST_EXIT=0, Totals 4 passed / 0 failed |
| affected family build | `cmake --build build --target zene ControlRegistryTest ControlRegistryNestingTest StartHubTest HelpAccessTest CollabPanelTest -j4 > logs/build-5-family.log 2>&1; echo BUILD_EXIT=$?` | BUILD_EXIT=0 |
| affected family | `cd build/tests && ctest -R '^(ControlRegistryTest\|ControlRegistryNestingTest\|StartHubTest\|HelpAccessTest\|CollabPanelTest)$' --output-on-failure > logs/test-4-family.log 2>&1; echo FAMILY_EXIT=$?` | FAMILY_EXIT=0, 5/5 passed |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py "$PWD" > logs/gates-1.log 2>&1; echo GATE_EXIT=$?` | GATE_EXIT=0, 8/8 PASS (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

## Files committed

- `include/ControlRegistry.h`
- `include/UnattendedRun.h`
- `src/core/ControlRegistry.cpp`
- `src/core/UnattendedRun.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/fork-sources.txt`
- `tests/src/core/ControlRegistryDisplayTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-gui-1105-menu-help-command-reference.md`

## Self-check

- **Regression test FAILS with the product change reverted and PASSES with it.** RED:
  `logs/test-2-red.log`, ctest EXIT=8, 3 passed / 1 failed, exact assertion
  `'result.ok' returned FALSE. (command 'test.needs_display' requires a display (headless instance))`.
  GREEN: `logs/test-3-green-reapplied.log`, EXIT=0, 4 passed / 0 failed. The same GREEN before the
  reverse proof is `logs/test-1-green.log`, EXIT=0.
- **Deterministic.** No wall-clock timing, sleeps, threads or races. The only seams are
  `setAgentInstance()` and the `QT_QPA_PLATFORM` value, saved/restored per test; under
  `QTEST_GUILESS_MAIN` there is no `QGuiApplication`, so `platformHasNoDisplay()` reads only that
  environment variable (`src/core/UnattendedRun.cpp:47-60`).
- **Fix at the cause and complete.** `"display"` is checked in exactly one place
  (`src/core/ControlRegistry.cpp:237`) and now uses the narrower flag. Every production reader of
  `isHeadless()` was reviewed and keeps modal suppression: `src/gui/CommandRun.cpp:90`,
  `src/gui/CommandMetadata.cpp:446`, `src/gui/WorkflowRun.cpp:146`,
  `src/gui/ControlCommandsTheme.cpp:301`, `src/gui/ControlCommandsArrangerView.cpp:335`.
  Every sibling that declares `requires: display` (`app.help`, `app.tour`, the plugin editor, the
  collab panel, the shell dialogs) goes through the same `checkRequires()` path and gets the same
  correction. All `setHeadless()` callers were audited; the seam sets both flags, so the five
  affected tests above still pass.
- **No collateral damage.** Nothing is serialised, so old projects and saved data load exactly as
  before; no limit or check was added. Offscreen/minimal/vnc runs keep both flags true and a normal
  desktop run without `--control-socket` keeps both false. The only behaviour change is the intended
  one: an agent instance on a real display now runs `requires: display` commands.
- **Verdict.** REAL and FIXED; no ALREADY-FIXED/FALSE-POSITIVE evidence applies. The cause is shown by
  the pre-fix `m_headless = isUnattendedRun()` path (`src/core/ControlRegistry.cpp:160`) and the RED run
  above.
- **Realtime / undo / commands.** None touched: no audio-thread code, no undo path, no new control
  command. The command count is unchanged.
- **Registration.** The new test is registered in `tests/CMakeLists.txt`, `tests/fork-sources.txt` and
  `tests/all-sources.txt`; `feature_check.py` passes 8/8 (`logs/gates-1.log`). The four product files
  were already fork sources.
- **Environment note (honest).** The live menu click was not performed by hand: this machine runs
  offscreen. The menu dispatch (`src/gui/HelpCenter.cpp:479` -> `app.help` ->
  `src/gui/ControlCommandsHelp.cpp:84`) was verified in code, and the display/agent distinction is
  covered deterministically by the regression above.


## Cumulative Terra integration — 2026-11-11

The owner asked for autonomous resolution of the stalled integrations. This branch
contains all 104 latest accepted Terra commits and resolves the thirty cumulative
conflicts as a coherent whole. This is integration of accepted work, not a reset of
issue identities or implementation budgets. The current focused suite is 29/29 after
rebuilding the affected targets; mixer container reversal fails all three real-window
mixer suites. See docs/RECOVERY-2026-11-11.md and the program workspace's
verification/recovery-20261111 evidence. Full gates, native-platform hosted checks and
independent acceptance remain outstanding. Original per-finding red/green evidence
is retained in each accepted branch's report; duplicates already fixed on the
combined base should be confirmed as such by the independent reviewer.

Integration outcomes (the existing original finding identities):
gui-1030-session-loop-a-range: FIXED
gui-1030-tab-mixer-density-compact: FIXED
gui-1030-tab-mixer-density-expanded: FIXED
gui-1030-tab-mixer-pick-the-channels-to-show: FIXED
gui-1030-tab-mixer-return: FIXED
gui-1030-tab-mixer-effects-chain: FIXED
gui-1030-tab-mixer-add-effect: FIXED
gui-1105-main-loop-points: FIXED
gui-1105-menu-navigate-nudge-clip-left: FIXED
gui-1105-menu-navigate-nudge-clip-right: FIXED
gui-1105-menu-navigate-next-clip: FIXED
gui-1105-menu-navigate-previous-clip: FIXED
gui-1105-menu-navigate-next-marker: FIXED
gui-1105-menu-navigate-previous-marker: FIXED
gui-1105-menu-window-channel-load: FIXED
gui-1105-menu-help-accessibility-check: FIXED
gui-1106-menu-navigate-nudge-clip-left: FIXED
gui-1106-menu-navigate-nudge-clip-right: FIXED
gui-1106-menu-navigate-next-clip: FIXED
gui-1106-menu-navigate-previous-clip: FIXED
gui-1106-menu-navigate-next-marker: FIXED
gui-1106-menu-navigate-previous-marker: FIXED
gui-1106-menu-help-command-reference: FIXED
gui-1106-menu-help-accessibility-check: FIXED
gui-1105-menu-help-help-for-this: FIXED
gui-1105-menu-help-manual: FIXED
gui-1105-menu-help-command-reference: FIXED
gui-1106-session-move-copy-and-delete-a-clip: FIXED
gui-1106-main-edit: FIXED
gui-1106-menu-navigate-zoom-out: FIXED
