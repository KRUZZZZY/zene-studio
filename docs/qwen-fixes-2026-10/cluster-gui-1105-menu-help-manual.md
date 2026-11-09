FIXED

# cluster gui-1105-menu-help-manual — Help > Manual refused in a visible agent GUI

## Verdict: REAL, fixed at the cause

- `app.help` declares `requires: display`: `src/gui/ControlCommandsHelp.cpp:142` (the `add()` helper) and
  `:151` (`app.help`, `display = true`). Help > Manual dispatches it: `src/gui/HelpCenter.cpp:477-478`.
- `ControlRegistry::checkRequires` refuses a `display` command while `m_headless` is set:
  `src/core/ControlRegistry.cpp:234`.
- Before the fix, the constructor set `m_headless = isUnattendedRun()`. `isUnattendedRun()` is
  `s_agentInstance || platformHasNoDisplay()` (`src/core/UnattendedRun.cpp:76-79`), and `s_agentInstance` is set
  for `--control-socket` (`include/UnattendedRun.h`, `isAgentInstance`). So an instance on a real, visible display that
  was started with `--control-socket` refused every `display` command, `app.help` included. The refusal toast is
  suppressed while headless (`src/gui/CommandRun.cpp:90`), so the menu closed and nothing happened. That is
  exactly what Luna saw.
- The registry's own contract is display-only: `include/ControlRegistry.h:170-171`, "True when no display is
  available (offscreen/minimal/vnc platform)". `human` is the requirement for "a person must answer"
  (`ControlRegistry.cpp:230`).

## Fix

- `include/UnattendedRun.h:54` / `src/core/UnattendedRun.cpp:81`: new `isDisplayless()`, the display half of the
  existing predicate (it returns the same `platformHasNoDisplay()` that `isUnattendedRun()` already used). This is the
  platform test, not a copy of it.
- `src/core/ControlRegistry.cpp:160`: `m_headless = isDisplayless();`.
- Sibling `src/gui/CommandMetadata.cpp:449` (`confirmAction`) is the one consumer of `isHeadless()` that opens a
  *modal* `QMessageBox::question`. It now also returns "yes, go ahead" for `isAgentInstance()`, so an agent instance
  still asks no modal question (SPEC A13), exactly as before. Using `isUnattendedRun()` there instead would have
  broken `CommandPaletteTest`, which drives the dialog under offscreen with `setHeadless(false)`.
- No new control command; no snapshot change; no audio-thread code touched; no undo change.

### Every consumer of `ControlRegistry::isHeadless()` checked

| Consumer | What it does when not headless | Effect in a visible agent instance after the fix |
| --- | --- | --- |
| `ControlRegistry.cpp:234` `display` requirement | runs the command | the bug fix: `app.help`, `app.tour` and the other `display` commands run |
| `CommandRun.cpp:90` `announce` | non-modal `TextFloat` | the refusal/result toast is now visible, which is the "error suppressed" half of the finding |
| `ControlCommandsTheme.cpp:301` | non-modal `TextFloat` | notice shown; nothing blocks |
| `ControlCommandsArrangerView.cpp:335` `window.tool_menu` | `QMenu::popup` (non-modal) | menu pops; reply unchanged |
| `WorkflowRun.cpp:146` | `WorkflowRunReport` `show()` (non-modal) | report window shown |
| `CommandMetadata.cpp:449` `confirmAction` | modal `QMessageBox` | **guarded with `isAgentInstance()`**, unchanged: no dialog |

Offscreen/minimal/vnc runs (CI, the socket integration tests, the headless sweep) are still headless, so their
behaviour is unchanged. `isUnattendedRun()` itself is unchanged, so every dialog gate in `main.cpp`, `MainWindow`,
`DataFile`, `Song` and the rest still treats agent instances as unattended.

## Integration (round 2, 050/m0-train-26)

This branch integrates the accepted patch `bugs/qwen-gui-1105-menu-help-manual @ f8c7c0fc88af` onto the pinned
target `050/m0-train-26 @ af010bc3cc05`. The driver's `git apply --3way --index` fell back to direct application
(`integration-apply.log`: "Falling back to direct application... Applied patch to ... cleanly"). The fallback is
exact here: every preimage blob in `accepted.patch` is the target's own blob, so a three-way merge has no competing
train change to reconcile.

| File | `accepted.patch` preimage | target blob `af010bc3cc` |
| --- | --- | --- |
| `include/UnattendedRun.h` | `08a9f4b2f4` | `08a9f4b2f4cc5f34e9bf5a18ef18c10c8385e96f` |
| `src/core/ControlRegistry.cpp` | `75caadc34f` | `75caadc34f97144b2a54afe25fd13885eebdfada` |
| `src/core/UnattendedRun.cpp` | `5923c967c1` | `5923c967c15a7a489716546dea8037ea333b4b08` |
| `src/gui/CommandMetadata.cpp` | `0c84bac04f` | `0c84bac04fe234bfdee8be404ccda04abb6fcda7` |
| `tests/src/core/ControlRegistryNestingTest.cpp` | `aa166e8567` | `aa166e85677b0e253143aa98e44813e6a194c906` |

The five product/test files in the integrated tree are byte-identical to the accepted patch:
`git diff f8c7c0fc..HEAD -- include/UnattendedRun.h src/core/UnattendedRun.cpp src/core/ControlRegistry.cpp
src/gui/CommandMetadata.cpp tests/src/core/ControlRegistryNestingTest.cpp` is empty, and each blob hash matches
(`ControlRegistry.cpp` is `01507b165d` on both sides, `UnattendedRun.h` `442f60aac3`, `UnattendedRun.cpp` `854e2e6534`,
`CommandMetadata.cpp` `0726d58d0b`, `ControlRegistryNestingTest.cpp` `c99ad3f67a`). The report is the accepted
108-line report plus the round-2 integration sections below (this file is 192 lines), so the integration introduces
**no semantic difference** in product or test code; the only delta is the documentation of the integration itself.
The "newer line" the driver flagged in `include/UnattendedRun.h`, `src/core/ControlRegistry.cpp` and
`src/core/UnattendedRun.cpp` is the accepted patch's own context: `git merge-base af010bc3cc f8c7c0fc` is `af010bc3cc`
itself, so the branch was cut from the target and there is no second line to keep.

### Affected failure paths

| Path | Before the fix | After the fix |
| --- | --- | --- |
| `app.help` (`requires: display`) in a visible `--control-socket` instance | refused by `checkRequires` (`ControlRegistry.cpp:234`) | runs; Help > Manual opens |
| refusal/result toast (`CommandRun.cpp:90`) | suppressed while headless | shown in a visible agent instance |
| `confirmAction` (`CommandMetadata.cpp:449`) | would open a modal `QMessageBox::question` in a visible agent instance | still skipped: `isAgentInstance()` guard (SPEC A13) |
| `ControlCommandsTheme.cpp:301` | suppressed while headless | non-modal notice shown |
| `ControlCommandsArrangerView.cpp:335` `window.tool_menu` | suppressed while headless | non-modal `QMenu::popup` |
| `WorkflowRun.cpp:146` | suppressed while headless | non-modal report window shown |
| `human` requirement (`checkRequires`) | always refused | unchanged |
| offscreen / minimal / vnc | headless | headless, unchanged |

### Supported-platform hazards

- `isDisplayless()` is the `platformHasNoDisplay()` half of `isUnattendedRun()`: it reads `QT_QPA_PLATFORM` and, once a
  `QGuiApplication` exists, `platformName()`. Linux `offscreen`/`minimal`/`vnc` → headless; `xcb`/`wayland` and the
  macOS/Windows platform names → a display. The API is already used by the target (`UnattendedRun.cpp:57`), so Qt 5.15,
  MSVC and macOS see no new call.
- A user who sets `QT_QPA_PLATFORM=offscreen` on a machine that has a display still gets a headless registry; that is the
  explicit offscreen choice, not a regression.
- The regression runs under `QTEST_GUILESS_MAIN` (a `QCoreApplication`), so `platformName()` is never consulted; the only
  seam is the `QT_QPA_PLATFORM` environment variable, which the test saves and restores.

## Regression test

`tests/src/core/ControlRegistryNestingTest.cpp:611` `agentInstanceOnADisplayIsNotHeadless`. The binary is a
`QTEST_GUILESS_MAIN` (`QCoreApplication`), so with `QT_QPA_PLATFORM` unset the platform test is false. The test sets the
agent flag (`setAgentInstance(true)`, which is what `main()` does for `--control-socket`), recreates the registry and
asserts it is not headless, then restores the environment and the flag. There is no timing, sleep or thread: the seam is
the environment variable plus the agent flag. It sits in the "recreated registry" family because `ControlRegistryTest.cpp`
is at the 500-code-line limit; a slot there put it at 534, and the file-length gate failed (log 18).

## Commands and exit codes

### Round 3 — re-verification (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-manual.r3/logs`)

The round-3 brief lists no new review point beyond the integration job text, so I re-verified the committed
integration on the same tree (HEAD `102ced752d`, base `af010bc3cc`) and changed no product or test code. The driver's
three-way merge has no conflict to resolve: `git merge-base af010bc3cc f8c7c0fc` is `af010bc3cc`, the accepted branch's
parent chain is `af010bc3cc` → `3d9466fbb9` → `f8c7c0fc`, and the five product/test blobs at HEAD equal the accepted
patch's blobs. The "newer line" in the three files is the patch's own context.

| Step | Command | EXIT |
| --- | --- | --- |
| build | `cmake --build build --target zene ControlRegistryNestingTest ControlRegistryTest StartHubTest CommandPaletteTest HelpAccessTest -j4` (build-1.log) | 0 |
| focused family | `ctest -R '^(ControlRegistryTest\|ControlRegistryNestingTest\|StartHubTest\|CommandPaletteTest\|HelpAccessTest)$' --output-on-failure` (test-1.log): 100% of 5 | 0 |
| 4b patch | `git diff af010bc3cc HEAD -- include/UnattendedRun.h src/core/UnattendedRun.cpp src/core/ControlRegistry.cpp src/gui/CommandMetadata.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b build reverted | `cmake --build build --target zene ControlRegistryNestingTest -j4` (build-2-reverted.log) | 0 |
| 4b RED | `ctest -R '^ControlRegistryNestingTest$' --output-on-failure` (test-2-red.log): `FAIL! ... '!headless' returned FALSE`, 17 passed 1 failed | 8 |
| 4b re-apply | `git apply logs/fix.patch` | 0 |
| 4b build restored | `cmake --build build --target zene ControlRegistryNestingTest -j4` (build-3-reapplied.log) | 0 |
| 4b GREEN | `ctest -R '^ControlRegistryNestingTest$' --output-on-failure` (test-3-green.log) | 0 |
| family re-run | same ctest as test-1 (test-4-family.log): 100% of 5 | 0 |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (gates-1.log): 8/8 rows PASS, RESULT PASS | 0 |
| completion | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> gui-1105-menu-help-manual` (submit-1.log): RESULT PASS - submit | 0 |

The focused-family selector is written with `\|` in the table only to keep the Markdown cell intact; the shell form is
`^(ControlRegistryTest|ControlRegistryNestingTest|StartHubTest|CommandPaletteTest|HelpAccessTest)$`.

### Round 2 — integration (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-manual.r2/logs`)

| Step | Command | EXIT |
| --- | --- | --- |
| build | `cmake --build build --target zene ControlRegistryNestingTest ControlRegistryTest StartHubTest CommandPaletteTest HelpAccessTest -j4` (log 01) | 0 |
| focused family | `ctest -R '^(ControlRegistryTest\|ControlRegistryNestingTest\|StartHubTest\|CommandPaletteTest\|HelpAccessTest)$' --output-on-failure` (log 02): 100% of 5 | 0 |
| 4b revert | `git apply -R logs/fix.patch` | 0 |
| 4b build reverted | `cmake --build build --target zene ControlRegistryNestingTest -j4` (log 03) | 0 |
| 4b RED | `ctest -R '^ControlRegistryNestingTest$' --output-on-failure` (log 04): `FAIL! ... '!headless' returned FALSE`, 17 passed 1 failed | 8 |
| 4b re-apply | `git apply logs/fix.patch` | 0 |
| 4b build restored | `cmake --build build --target zene ControlRegistryNestingTest -j4` (log 05) | 0 |
| 4b GREEN | `ctest -R '^ControlRegistryNestingTest$' --output-on-failure` (log 06) | 0 |
| family re-run | same ctest as log 02 (log 07): 100% of 5 | 0 |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (log 08): every row PASS, RESULT PASS | 0 |

### Round 1 — original fix (logs in `~/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-manual.r1/logs`)

The build used `CCACHE_DISABLE=1`: ccache's temp dir (`~/.local/share/zene-bunny-fix/dsh/home/ccache-tmp-r6`) is not
writable from this lane (log 01, `Permission denied`).

| Step | Command | EXIT |
| --- | --- | --- |
| RED (first form, in ControlRegistryTest) | `ctest -R '^ControlRegistryTest$' --output-on-failure` (log 04): `FAIL! agentInstanceOnADisplayIsNotHeadless() '!agentOnDisplay' returned FALSE` | 8 |
| 4b revert | `git diff -- include/UnattendedRun.h src/core/UnattendedRun.cpp src/core/ControlRegistry.cpp src/gui/CommandMetadata.cpp > logs/fix.patch && git apply -R logs/fix.patch` | 0 |
| 4b build reverted | `cmake --build build --target ControlRegistryNestingTest -j4` (log 19) | 0 |
| 4b RED | `ctest -R '^ControlRegistryNestingTest$' --output-on-failure` (log 20): `FAIL! ... '!headless' returned FALSE`, 17 passed 1 failed | 8 |
| 4b re-apply | `git apply logs/fix.patch` | 0 |
| GREEN build | `cmake --build build --target zene ControlRegistryNestingTest ControlRegistryTest StartHubTest CommandPaletteTest HelpAccessTest -j4` (log 23) | 0 |
| GREEN + family | `ctest -R '^(ControlRegistryTest\|ControlRegistryNestingTest\|StartHubTest\|CommandPaletteTest\|HelpAccessTest)$'` (log 24): 100% of 5 | 0 |
| Wider sweep | 17 tests incl. CommandFormTest, WorkflowFeedbackTest, AgentSurfaceReportTest, PerformViewTest, ThemeContrastTest, ControlShutdownHookTest, SafeStartTest, ControlShell, ControlWorkflowBindings, ControlSocketIntegration (log 16): 16/17 | 8 |
| Gates | `feature_check.py <worktree>` (log 25): every row PASS, RESULT PASS | 0 |

The one failure in the wider sweep is `ControlSocketIntegration`: "plugin.list is not broken down by format:
{'ladspa': 10}" (`tests/control-socket-integration.py:686` needs a `builtin` entry). This build tree has built just one
plugin library (`find build/plugins -name '*.so'` → `build/plugins/libmidiexport.so`), so no built-in instruments exist.
That is the environment: `plugin.list` has no `display` requirement and does not read the headless flag, and that run is
offscreen, so the registry is headless both before and after the fix. I did not re-run it against the unfixed code.

In round 1 the gate script's hard-coded log directory (`<worktree>-feature-check`, a sibling of the worktree) could not
be created from that lane (`PermissionError`, log 17); the gates were then run through `logs/feature_check_logdir.py`,
which replaces only the `logs = ...` line and execs the original. In round 2 the direct run created the directory and
passed unchanged (log 08).

Files committed: `include/UnattendedRun.h`, `src/core/UnattendedRun.cpp`, `src/core/ControlRegistry.cpp`,
`src/gui/CommandMetadata.cpp`, `tests/src/core/ControlRegistryNestingTest.cpp`, this report. All product files are
fork-new and already in `tests/fork-sources.txt`; no new files, so no registration changes.

## Self-check

### Round 3 (re-verification)

- **Fails reverted / passes with the fix:** 4b above: reverted → `ControlRegistryNestingTest` EXIT 8 (`'!headless'
  returned FALSE`, test-2-red.log); re-applied → EXIT 0 (test-3-green.log; family test-4-family.log, 5/5).
- **Deterministic:** no wall-clock, sleeps, threads or races. The seam is the `QT_QPA_PLATFORM` environment variable
  (saved with `qgetenv`, cleared with `qunsetenv`, restored with `qputenv`) plus `setAgentInstance(true)`, both restored
  before the assertion (`ControlRegistryNestingTest.cpp:613-618`).
- **At the cause and complete:** the cause is the predicate the registry's headless flag is built from
  (`ControlRegistry.cpp:160`). Every consumer of `isHeadless()` is in the table above. The only modal one
  (`confirmAction`) keeps its agent-instance behaviour through `isAgentInstance()`. `isUnattendedRun()` and its callers
  are unchanged. The accepted design and the regression test are retained unchanged: the five product/test blobs at
  HEAD equal the accepted patch's blobs.
- **No collateral damage:** no project, save or config format is touched, so old projects load as before. No limit or
  check was added. Displayless runs are still headless. In a visible agent instance, only non-modal surfaces (toasts,
  popup menu, the workflow report window) now appear, and no modal dialog can open (`confirmAction` guard,
  `isUnattendedRun()` unchanged).
- **Semantic difference from the accepted patch:** none in product or test code (blob-identical). The report is the
  accepted report plus the integration sections.
- **ControlSocketIntegration (round-2 QA note):** not rerun; it is outside this patch (`plugin.list` catalogue format,
  no `display` requirement, offscreen registry unchanged). The round-1 wider sweep's failure is environmental (no
  built-in instrument libraries in this build tree: `find build -name '*.so'` is empty).
- **submit_check "gates" row:** PASS (gates-1.log, EXIT 0; submit-1.log, EXIT 0).

### Round 2 (integration)

- **Fails reverted / passes with the fix:** 4b above: reverted → `ControlRegistryNestingTest` EXIT 8 (`'!headless'
  returned FALSE`); re-applied → EXIT 0 (log 06; family log 07, 5/5).
- **Semantic difference from the accepted patch:** none. `git diff --cached` equals `git diff af010bc3cc..f8c7c0fc`
  byte for byte; every preimage blob in `accepted.patch` is the target's blob.
- **Deterministic:** no wall-clock, sleeps, threads or races. The seam is the `QT_QPA_PLATFORM` environment variable in
  a `QCoreApplication`, plus `setAgentInstance(true)`, both restored before the assertion.
- **At the cause and complete:** the cause is the predicate the registry's headless flag is built from
  (`ControlRegistry.cpp:160`). Every consumer of `isHeadless()` is in the table above. The only modal one
  (`confirmAction`) keeps its agent-instance behaviour through `isAgentInstance()`. `isUnattendedRun()` and its 40+
  callers are unchanged.
- **No collateral damage:** no project, save or config format is touched, so old projects load as before. No limit or
  check was added. Displayless runs are still headless. In a visible agent instance, only non-modal surfaces (toasts,
  popup menu, the workflow report window) now appear, and no modal dialog can open (`confirmAction` guard,
  `isUnattendedRun()` unchanged).
- **submit_check "gates" row:** PASS in round 2 (log 08, EXIT 0); the round-1 `PermissionError` on
  `<worktree>-feature-check` did not recur.

### Round 1 (original fix)

- **Fails reverted / passes with the fix:** 4b above: reverted → `ControlRegistryNestingTest` EXIT 8 (`'!headless'
  returned FALSE`); re-applied → EXIT 0 (log 24, 5/5).
- **Deterministic:** no wall-clock, sleeps, threads or races. The seam is the `QT_QPA_PLATFORM` environment variable in
  a `QCoreApplication`, plus `setAgentInstance(true)`, both restored before the assertion.
- **At the cause and complete:** the cause is the predicate the registry's headless flag is built from
  (`ControlRegistry.cpp:160`). Every consumer of `isHeadless()` is in the table above. The only modal one
  (`confirmAction`) keeps its agent-instance behaviour through `isAgentInstance()`. `isUnattendedRun()` and its 40+
  callers are unchanged. Tests that toggle `setHeadless()` still pass (CommandPaletteTest, HelpAccessTest,
  AgentSurfaceReportTest, PerformViewTest).
- **No collateral damage:** no project, save or config format is touched, so old projects load as before. No limit or
  check was added. Displayless runs are still headless. In a visible agent instance, only non-modal surfaces (toasts,
  popup menu, the workflow report window) now appear, and no modal dialog can open (`confirmAction` guard,
  `isUnattendedRun()` unchanged).
- **ALREADY-FIXED / FALSE-POSITIVE evidence:** not applicable (verdict FIXED).
- **submit_check "gates" row:** FAIL here only because `submit_check.py` runs `feature_check.py` directly, and that
  script's first act is `mkdir <worktree>-feature-check` outside this lane's write jail: `PermissionError` (log 28,
  EXIT 1). The same gates run on the committed tree with only the log directory moved (log 29): RESULT PASS, EXIT 0.
  Run from outside the jail, the row is expected to pass. That is not verified here.
