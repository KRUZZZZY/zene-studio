FALSE-POSITIVE

new-p21-77: FALSE-POSITIVE

Finding: `VcaNest::change` calls `Engine::getSong()->setModified()` without a null guard at [ControlCommandsVcaNest.cpp:108](src/core/ControlCommandsVcaNest.cpp:108). The line is unguarded, but no command can reach it with no song: the registry refuses `vca.set_parent` and `vca.set_links` before their handlers run. No product code is changed on this branch.

## Evidence

1. **Both commands are registered as engine-gated.** `add()` at [ControlCommandsVcaNest.cpp:159-163](src/core/ControlCommandsVcaNest.cpp:159) calls `control::defineCommandWithSchema(..., true, handler)` and does not pass `requiresEngine`. The default is `true` at [include/ControlCommandDef.h:53](include/ControlCommandDef.h:53), and [src/core/ControlCommandDef.cpp:34-45](src/core/ControlCommandDef.cpp:34) stores it in `cmd.requiresEngine`.
2. **Dispatch refuses before the handler.** [ControlRegistry.cpp:283-285](src/core/ControlRegistry.cpp:283): `if (cmd->requiresEngine && !isEngineReady())` returns `ControlErrorKind::Busy`, and the handler is only read afterwards at [ControlRegistry.cpp:293](src/core/ControlRegistry.cpp:293).
3. **Readiness requires a song.** [ControlRegistry.cpp:205-207](src/core/ControlRegistry.cpp:205): `isEngineReady()` returns `s_ready && Engine::getSong() != nullptr && Engine::mixer() != nullptr`.
4. **The check and the handler are separated only by a queue hop, and a song cannot disappear inside that hop.** `invoke` evaluates the gate on the caller's thread, then runs the handler through `runOnUiThread` with `Qt::BlockingQueuedConnection` ([ControlRegistry.cpp:249-259](src/core/ControlRegistry.cpp:249), handler lambda at [:296](src/core/ControlRegistry.cpp:296)). The song is deleted only in `Engine::destroy()` ([src/core/Engine.cpp:123](src/core/Engine.cpp:123), `deleteHelper(&s_song)`). Both callers run after the event loop has stopped: `~MainWindow` ([src/gui/MainWindow.cpp:271-286](src/gui/MainWindow.cpp:271), `Engine::destroy()` at :286) and `main.cpp` after `delete app` ([src/core/main.cpp:1858](src/core/main.cpp:1858) and [:1862](src/core/main.cpp:1862)). A queued invoke cannot run in that teardown.
5. **Nothing calls the handlers directly.** A search for `->handler(` and `.handler(` across `src/` and `tests/` finds only assignments to `cmd.handler` and the registry's own invoke path. The only route to `change()` is `invoke`.
6. **The codebase's convention is that handlers presume a song.** About fifty control files call `Engine::getSong()->setModified()` unguarded, for example [ControlCommandsArrangeTools.cpp:201](src/core/ControlCommandsArrangeTools.cpp:201) and [ControlCommandsCompGroup.cpp:183](src/core/ControlCommandsCompGroup.cpp:183). The sibling VCA files do not call `setModified` at all (`rg setModified src/core/ControlCommandsVca*.cpp` finds only [ControlCommandsVcaNest.cpp:108](src/core/ControlCommandsVcaNest.cpp:108)). Adding a guard here would be dead code that departs from the convention.
7. **The finding's note is out of date.** It says no test covers this file. [tests/src/core/ControlVcaNestTest.cpp:69](tests/src/core/ControlVcaNestTest.cpp:69) and [:116](tests/src/core/ControlVcaNestTest.cpp:116) exercise `vca.set_links` and `vca.set_parent`, and the target is registered at [tests/CMakeLists.txt:802](tests/CMakeLists.txt:802) and [:3059](tests/CMakeLists.txt:3059).

Conclusion: the call site is unguarded, but the guard is provided one layer up, in `ControlRegistry::invoke` ([ControlRegistry.cpp:283](src/core/ControlRegistry.cpp:283)). The "when" in the finding (`vca.set_parent` or `vca.set_links` run with no song) cannot occur through the control surface.

## Commands run and exit codes

Run from the worktree `zene-qwen-new-p21-77`. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-77.r1/logs/`.

| Check | Command | Result |
| --- | --- | --- |
| Feature-check gate script | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/001-feature-check.log 2>&1` | EXIT=1. The script aborted before running any gate: it calls `logs.mkdir()` on `<worktree>-feature-check`, which is `projects/zene-studio-program/zene-qwen-new-p21-77-feature-check`, outside my writable paths. `PermissionError`. Not a gate result. |
| fork-sources | `bash tests/fork-sources-gate.sh > logs/010-fork-sources.log 2>&1` | EXIT=0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh > logs/011-all-sources.log 2>&1` | EXIT=0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh > logs/012-no-upstream.log 2>&1` | EXIT=0 |
| check-namespace | `tests/scripted/check-namespace > logs/013-check-namespace.log 2>&1` | EXIT=0 |
| no-tautology | `bash tests/no-tautology-gate.sh > logs/014-no-tautology.log 2>&1` | EXIT=0 |
| doc-figures | `python3 tests/doc-figures-gate.py > logs/015-doc-figures.log 2>&1` | EXIT=0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check > logs/016-complexity.log 2>&1` | EXIT=1. Output lists 68 functions over CCN 10 in fork scope, none in a file this branch changed (no product file changed). `rg VcaNest` on the log: no match. |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check > logs/017-file-length.log 2>&1` | EXIT=1. Output lists 25 files over 500 lines, none in a file this branch changed. `rg VcaNest` on the log: no match. |
| Completion check (before this report) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-77 > logs/020-submit-check-before-report.log 2>&1` | EXIT=1. FAIL: commits on the branch, report missing, Self-check missing, tests added or changed, gates. |

The feature-check script's own rule for the two ratchet gates is "fail only on items NEW in files the branch changed". This branch changes no product file, so by that rule they would pass. I did not run the script itself, so this is not a PASS from it.

I did not build any target or run any ctest. This is a verdict with no product change, so there was no RED/GREEN cycle to run. Step 4b (revert-and-rerun) does not apply.

## Self-check

- **Regression test RED then GREEN (exit codes):** Not applicable. No product change, so no regression test was added. The nearest existing coverage is `ControlVcaNestTest` (no-op and real-change undo paths), which I did not build or run here.
- **Test is deterministic:** Not applicable; no test added or changed. I did not add a test that tears the engine down mid-run to simulate a missing song: that would pin a path the registry already refuses, and it would depend on fragile `Engine::destroy` / `Engine::init` re-entry.
- **Fix is at the cause and complete:** Not applicable; no fix. The cause, if any, is the registry gate at [ControlRegistry.cpp:283](src/core/ControlRegistry.cpp:283), which is consistent for every engine-gated command. Siblings checked: the other VCA files (no `setModified`) and about fifty control files with the same unguarded pattern (item 6).
- **No collateral damage:** No code changed, so no saved data, limit or caller is affected.
- **Verdict evidence:** Code citations are in items 1-6 above.
- **Open gap:** The feature-check script could not run inside the write-jail (its log directory is outside the writable paths). I ran its six simple gates and two ratchet gates by hand, with logs in the LOG FOLDER. The submit check's "tests added or changed" and "gates (feature_check.py)" rows remain FAIL for these reasons, and I did not invent a test or a gate result to clear them.
