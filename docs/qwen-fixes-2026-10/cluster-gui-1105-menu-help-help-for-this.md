FIXED

# gui-1105-menu-help-help-for-this - Help > Help for This rejected in a visible agent GUI

## Decision: REAL
`ControlRegistry::ControlRegistry` set its one headless flag from `isUnattendedRun()` (src/core/ControlRegistry.cpp:159),
which is `s_agentInstance || platformHasNoDisplay()` (src/core/UnattendedRun.cpp:81-84). `main()` sets
`setAgentInstance(true)` for every `--control-socket` start (src/core/main.cpp:1264). `checkRequires` then refused any
command declaring `requires: display` when that flag was set (src/core/ControlRegistry.cpp:235 before the fix), and
`app.help` declares it (src/gui/ControlCommandsHelp.cpp:142, the `display` argument is `true` for app.help). So on a
real display with `--control-socket`, Help > Help for This (src/gui/HelpCenter.cpp:473) got `Requires: ... requires a
display (headless instance)`; the menu error is suppressed for headless (src/gui/CommandRun.cpp:90) and no window
appeared. "Display" means a screen exists; "unattended" means nobody can answer a dialog. They are different questions.

## Fix (at the cause)
- `include/UnattendedRun.h`, `src/core/UnattendedRun.cpp`: new `hasNoDisplay()` - only the platform test
  (offscreen/minimal/vnc), src/core/UnattendedRun.cpp:76-79.
- `include/ControlRegistry.h`, `src/core/ControlRegistry.cpp`: new `m_displayless` (from `hasNoDisplay()`,
  src/core/ControlRegistry.cpp:164), accessor `isDisplayless()`; `checkRequires` uses it for `display`
  (src/core/ControlRegistry.cpp:238). `setHeadless(bool)` sets both flags, so tests that fake a display
  (Vst3/Clap integration tests, ControlRegistryTest::displayRequirementFollowsHeadlessState) behave as before.
- Unchanged: `isHeadless()` and every dialog-suppression caller (WorkflowRun.cpp:146, ControlCommandsTheme.cpp:301,
  ControlCommandsArrangerView.cpp:335, CommandRun.cpp:90, CommandMetadata.cpp:446) keep "agent instance or no
  display". Offscreen/CI and no-DISPLAY runs still refuse display commands, so siblings (window.*, collab.show_panel,
  plugin editor, telemetry.consent with `human`) are unchanged there and now also work on a visible agent GUI. `human`
  is still always refused. No new commands; no realtime code touched.

## Affected failure paths and supported-platform hazards
Failure paths that share the one condition:
1. `app.help` (Help > Help for This) on a visible GUI started with `--control-socket` - the reported failure.
2. Every other command declaring `requires: display` on the same start: `app.tour` (ControlCommandsHelp.cpp:142), the
   plugin-editor commands (ControlCommandsPluginActions.cpp:178). The `window.*` shell commands
   (ControlCommandsShell.cpp:186) also declare `human`, so they stay refused either way.
3. `human` is still always refused (unchanged).
4. Offscreen/minimal/vnc and no-DISPLAY runs still refuse display commands (unchanged).
5. `setHeadless(bool)` callers (tests) set both flags, so their fake-display behaviour is unchanged.

Platform hazards:
- `platformHasNoDisplay()` (src/core/UnattendedRun.cpp:47-60) reads `QT_QPA_PLATFORM` and, once it exists,
  `QGuiApplication::platformName()`. A visible GUI under `--control-socket` leaves `QT_QPA_PLATFORM` unset or
  "xcb"/"wayland", so `hasNoDisplay()` is false there.
- offscreen/minimal/vnc (CI) and a missing DISPLAY still report no display.
- The regression test uses `QT_QPA_PLATFORM=xcb` to stand in for a real display; the guiless test app has no
  QGuiApplication, so the environment variable decides, as `main()` sees it before the app object exists.
- macOS ("cocoa") and Windows ("windows") report a display; no platform-specific build requirement was added.
- Header + core only: no new files, no new commands, no realtime path, no saved-data format change.

## Regression test
`ControlRegistryNestingTest::displayRequirementIgnoresAgentInstance` (tests/src/core/ControlRegistryNestingTest.cpp:608):
`QT_QPA_PLATFORM=xcb` stands in for a real display (the guiless app has no platform), `setAgentInstance(true)`, fresh
registry; asserts `isHeadless()` stays true and a display command runs. No timing. It is in this file because
ControlRegistryTest.cpp has 1 code line of headroom under the 500 gate and the build tree cannot be reconfigured in
this lane (a new test target was not possible; "bunny-guard: configuring is the parent's job"). No-display refusal is
still covered by `ControlRegistryTest::displayRequirementFollowsHeadlessState`.

## Integration (050/m0-train-26 @ af010bc3cc)
Source `bugs/qwen-gui-1105-menu-help-help-for-this @ 744720eeec` (fix 904b381 + docs 744720eeec), target pin
`050/m0-train-26 @ af010bc3cc`. The pin's blobs are exactly the patch pre-images (ControlRegistry.h `83863c6c20`,
ControlRegistry.cpp `75caadc34f`, UnattendedRun.h `08a9f4b2f4`, UnattendedRun.cpp `5923c967c1`,
ControlRegistryNestingTest.cpp `aa166e8567`), so the accepted patch applies with no conflict:
`git merge-tree --write-tree --merge-base=af010bc3cc af010bc3cc 744720eeec` exits 0 and its tree `58a7a9a4b1` equals
`744720eeec^{tree}`. The five product/test files at HEAD are blob-identical to the accepted patch's
(`git diff 744720eeec HEAD -- include/ControlRegistry.h include/UnattendedRun.h src/core/ControlRegistry.cpp
src/core/UnattendedRun.cpp tests/src/core/ControlRegistryNestingTest.cpp` is empty; ControlRegistry.h `0b0584f4ba`,
ControlRegistry.cpp `21fa088a7d`, UnattendedRun.h `3818a2ceb5`, UnattendedRun.cpp `a145f23285`,
ControlRegistryNestingTest.cpp `5e25b9cffc` on both sides). The only delta is this report, which is the accepted
report plus the integration sections below. The driver's three-way merge fell back to direct application
(integration-apply.log: all files "cleanly"); the fallback is exact, not a re-resolution. No design change was
needed and none was made: the accepted design and its regression test are retained byte-for-byte.

The driver's flagged "newer line" in `include/ControlRegistry.h` and `src/core/ControlRegistry.cpp` is not a second
line against the pin: `git merge-base af010bc3cc 744720eeec` is `af010bc3cc` itself, and `git merge-tree --write-tree
af010bc3cc 2e07fb6a26` exits 0. It is the cumulative overlap with the sibling Help-menu fixes that patch the same
predicate (`gui-1105-menu-help-manual` `3d9466fbb9`/`102ced752d`, `gui-1105-menu-help-command-reference`
`175efd764c`/`a863fadb94`): `git merge-tree` of this branch with those conflicts in `src/core/ControlRegistry.cpp`
(and `include/ControlRegistry.h` for the command-reference sibling). That is a train-level selection matter, not a
conflict against the pinned target, so this lane has nothing to resolve.

## Evidence (integration worktree; all logs in the run LOG FOLDER)
| step | command | exit |
|---|---|---|
| merge check | `git merge-tree --write-tree --merge-base=af010bc3cc055d477045a54e2d866f59a95017d7 af010bc3cc055d477045a54e2d866f59a95017d7 744720eeec0ba38dee34ebeb2dcfb9d0595a7024` | 0; tree `58a7a9a4b1` == accepted tree |
| accepted product/test diff empty | `git diff 744720eeec0ba38dee34ebeb2dcfb9d0595a7024 HEAD -- include/ControlRegistry.h include/UnattendedRun.h src/core/ControlRegistry.cpp src/core/UnattendedRun.cpp tests/src/core/ControlRegistryNestingTest.cpp` | 0 (no output); the report file differs (accepted report + integration sections) |
| GREEN build | `cmake --build build --target zene ControlRegistryTest ControlRegistryNestingTest StartHubTest HelpAccessTest -j4` (logs/build-1.log) | 0 |
| GREEN family | `cd build/tests && ctest -R '^(ControlRegistryTest\|ControlRegistryNestingTest\|StartHubTest\|HelpAccessTest)$' --output-on-failure` (logs/test-1-green.log) | 0: 4/4 passed |
| RED revert | `git diff HEAD -- include src > logs/fix.patch && git apply -R logs/fix.patch` -> rebuild `ControlRegistryNestingTest` (logs/build-2-red.log) -> `ctest -R '^ControlRegistryNestingTest$' --output-on-failure` (logs/test-2-red.log) | REVERT=0, BUILD=0, CTEST=8: `FAIL! displayRequirementIgnoresAgentInstance() 'result.ok' returned FALSE. (command 'test.agent_display' requires a display (headless instance))`, 17 passed 1 failed |
| GREEN re-apply | `git apply logs/fix.patch` -> rebuild (logs/build-3-green.log) -> `ctest -R '^ControlRegistryNestingTest$' --output-on-failure` (logs/test-3-green.log) | APPLY=0, BUILD=0, CTEST=0 |
| GREEN family after re-apply | rebuild all four targets (logs/build-4-green.log) -> `ctest -R '^(ControlRegistryTest\|ControlRegistryNestingTest\|StartHubTest\|HelpAccessTest)$' --output-on-failure` (logs/test-4-green.log) | BUILD=0, CTEST=0: 4/4 passed |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (logs/gates-1.log) | 0, all 8 rows PASS, RESULT: PASS |

The stock gate runs in this worktree (the round-1 PermissionError creating `<worktree>-feature-check` is gone: the
parent directory is writable and the gate created its log directory normally).

## Round 3 — re-verification (no code change)

The round-3 brief lists no new review point beyond the integration job text, so I re-verified the committed
integration on the same tree (HEAD `2e07fb6a26`, base `af010bc3cc`) and changed no product or test code. The
driver's three-way merge has no conflict to resolve against the pin: `git merge-base af010bc3cc 744720eeec` is
`af010bc3cc`, and `git merge-tree --write-tree af010bc3cc 2e07fb6a26` exits 0. The five product/test blobs at HEAD
equal the accepted patch's blobs. The flagged "newer line" is the cumulative overlap with the sibling Help-menu
fixes that patch the same predicate (Integration section above), not a second line against the target. Logs:
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/gui-1105-menu-help-help-for-this.r3/logs`.

| Step | Command | EXIT |
|---|---|---|
| GREEN build | `cmake --build build --target zene ControlRegistryTest ControlRegistryNestingTest StartHubTest HelpAccessTest -j4` (logs/build-1.log) | 0 |
| GREEN family | `cd build/tests && ctest -R '^(ControlRegistryTest\|ControlRegistryNestingTest\|StartHubTest\|HelpAccessTest)$' --output-on-failure` (logs/test-1.log) | 0: 4/4 passed |
| 4b patch | `git diff af010bc3cc HEAD -- include/ControlRegistry.h include/UnattendedRun.h src/core/ControlRegistry.cpp src/core/UnattendedRun.cpp > logs/fix.patch` | 0 |
| 4b revert | `git apply -R logs/fix.patch` | 0 |
| 4b RED build | `cmake --build build --target zene ControlRegistryNestingTest -j4` (logs/build-2-red.log) | 0 |
| 4b RED | `ctest -R '^ControlRegistryNestingTest$' --output-on-failure` (logs/test-2-red.log) | 8: `FAIL! displayRequirementIgnoresAgentInstance() 'result.ok' returned FALSE. (command 'test.agent_display' requires a display (headless instance))`, 17 passed 1 failed |
| 4b re-apply | `git apply logs/fix.patch` | 0 |
| 4b GREEN build | `cmake --build build --target zene ControlRegistryNestingTest -j4` (logs/build-3-green.log) | 0 |
| 4b GREEN | `ctest -R '^ControlRegistryNestingTest$' --output-on-failure` (logs/test-3-green.log) | 0 |
| family re-run | same ctest as GREEN family (logs/test-4-family.log) | 0: 4/4 passed |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (logs/gates-2.log, post-report-edit; logs/gates-1.log is the same run before it) | 0: 8/8 rows PASS, RESULT PASS |
| completion | `python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> gui-1105-menu-help-help-for-this` (logs/submit-1.log) | 0: RESULT PASS - submit |
| post-commit gates | same `feature_check.py` run after the report commit (logs/gates-3.log) | 0: 8/8 rows PASS, RESULT PASS |
| post-commit completion | same `submit_check.py` run after the report commit (logs/submit-2.log) | 0: RESULT PASS - submit |

Files committed: include/ControlRegistry.h, include/UnattendedRun.h, src/core/ControlRegistry.cpp,
src/core/UnattendedRun.cpp, tests/src/core/ControlRegistryNestingTest.cpp, this report.
(All five source files are already fork-owned in tests/fork-sources.txt; no registration change.)

## Self-check
- **Fails reverted / passes with fix:** yes - RED CTEST=8 with `include`+`src` reverted (`git apply -R logs/fix.patch`),
  GREEN CTEST=0 with it reapplied (table above; logs/test-2-red.log, logs/test-3-green.log). The test compiles against
  the old code (it uses no new API), so the RED is an assertion failure, not a build error.
- **Deterministic:** no clocks, sleeps or threads. The seams are `QT_QPA_PLATFORM` (read at registry construction by
  `platformHasNoDisplay()`), `setAgentInstance()`, and `ControlRegistry::destroy()` to build a fresh registry; all
  restored at the end of the slot.
- **Cause and completeness:** the single consumer of the `display` requirement is `ControlRegistry::checkRequires`
  (src/core/ControlRegistry.cpp:238, `requirement == "display"`); every command declaring it (app.help, app.tour,
  collab.show_panel, plugin editor, telemetry.consent, window.* shell commands) goes through it, so all are consistent.
  The other `isHeadless()` readers (WorkflowRun.cpp:146, ControlCommandsTheme.cpp:301,
  ControlCommandsArrangerView.cpp:335, CommandRun.cpp:90, CommandMetadata.cpp:446) mean "no human to answer / show a
  dialog" and intentionally keep the unattended predicate.
- **Collateral:** no saved-data or project-format change. Offscreen/minimal/vnc and `setHeadless(true)` still refuse
  display commands (ControlRegistryTest::displayRequirementFollowsHeadlessState passes); `human` is still always
  refused. No limit or check was added.
- **Integration faithfulness:** the five product/test files at HEAD are blob-identical to the accepted patch's
  (`git diff 744720eeec HEAD -- <the five files>` is empty); the only delta is this report (the accepted report plus
  the integration sections), so there is no semantic difference in product or test code.
- **Gates:** the stock `feature_check.py` passes all 8 rows from this worktree (logs/gates-1.log, exit 0).

### Round 3 (re-verification)

- **Fails reverted / passes with the fix:** 4b above: reverted → `ControlRegistryNestingTest` EXIT 8
  (`'result.ok' returned FALSE. (command 'test.agent_display' requires a display (headless instance))`,
  logs/test-2-red.log); re-applied → EXIT 0 (logs/test-3-green.log; family logs/test-4-family.log, 4/4).
- **Deterministic:** no wall-clock, sleeps, threads or races. The seams are `QT_QPA_PLATFORM=xcb` (read by
  `platformHasNoDisplay()` at registry construction), `setAgentInstance(true)`, and `ControlRegistry::destroy()` to
  build a fresh registry; all restored at the end of the slot (tests/src/core/ControlRegistryNestingTest.cpp:608-616).
- **At the cause and complete:** the single consumer of the `display` requirement is
  `ControlRegistry::checkRequires` (src/core/ControlRegistry.cpp:238); every command declaring it goes through it.
  The other `isHeadless()` readers (WorkflowRun.cpp:146, ControlCommandsTheme.cpp:301,
  ControlCommandsArrangerView.cpp:335, CommandRun.cpp:90, CommandMetadata.cpp:446) mean "no human to answer /
  show a dialog" and intentionally keep the unattended predicate. The accepted design and the regression test are
  retained: the five product/test blobs at HEAD equal the accepted patch's.
- **No collateral damage:** no project, save or config format is touched, so old projects load as before. No limit
  or check was added. Offscreen/minimal/vnc and `setHeadless(true)` still refuse display commands
  (`ControlRegistryTest::displayRequirementFollowsHeadlessState` passes); `human` is still always refused.
- **Semantic difference from the accepted patch:** none in product or test code (blob-identical). The only delta is
  this report (the accepted report plus the integration sections).
- **Verdict:** REAL, and the accepted fix is present and verified; this round is a re-verification of the
  already-committed integration, not an ALREADY-FIXED or FALSE-POSITIVE case.
