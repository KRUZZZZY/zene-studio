FALSE-POSITIVE

Cluster new-p19-8: `oop.get_state` "can dereference a missing song" (Luna BUG 12, src/core/ControlCommandsOutOfProcess.cpp:141-142).

## Verdict

The null-song dereference at `allChains()` (ControlCommandsOutOfProcess.cpp:141) is real code, but no control-surface call can reach it on this branch. The control registry refuses `oop.get_state` with a typed `busy` failure whenever the song or mixer is missing, before the handler runs. The finding's second claim ("other commands in this slice handle a null song") is also not supported by the tree. No product change is made, so there is no RED/GREEN regression test: a test of this path would pass before and after any change, because the gate answers first.

## Evidence

1. **The command takes the engine gate.** `oop.get_state` is registered at src/core/ControlCommandsOutOfProcess.cpp (the `cmd.id = OutOfProcessGetStateId` block) and sets no `requiresEngine`. `grep -n requiresEngine src/core/ControlCommandsOutOfProcess*.cpp` returns nothing, so the default applies: `bool requiresEngine = true;` at include/ControlRegistry.h:104.
2. **The gate runs before the handler.** `ControlRegistry::invoke` at src/core/ControlRegistry.cpp:283-291: `if (cmd->requiresEngine && !isEngineReady())` returns `ControlErrorKind::Busy` with the readiness code and message. The handler is only reached by `runOnUiThread` at ControlRegistry.cpp:296, after this check.
3. **Readiness means a song exists.** `isEngineReady()` at src/core/ControlRegistry.cpp:205-207 is `s_ready && Engine::getSong() != nullptr && Engine::mixer() != nullptr`.
4. **Startup is covered.** The control server answers `busy` until `setReady(true)`, which runs only after the application is constructed: src/core/main.cpp:1829-1831 (`controlServer->registry()->setReady( true );`).
5. **Teardown is covered for the socket.** The socket is closed at src/core/main.cpp:1847-1849, before `delete app` and `Engine::destroy()` (src/core/main.cpp:1862). `Engine::destroy()` clears the song last (src/core/Engine.cpp:123, `deleteHelper( &s_song )`, and include/Engine.h:134-138 sets the pointer to nullptr).
6. **Precedent in this tree.** tests/src/core/RecordToolsNoEngineTest.cpp:20-27 states the same argument for `record.get_state`, `record.remaining` and `record.route_source` (Luna 3946 and 3950 reported crashes there too). Lines 84-100 are the executable form: the calls must fail typed and leave the process alive. That is the contract this finding should be closed under.
7. **The second claim is wrong.** In the slice, `ControlCommandsOutOfProcess.cpp` has one `getSong` use (line 141). `ControlCommandsOutOfProcessEdit.cpp` has none. The sibling walk `dsp.get_state` (src/core/ControlCommandsDsp.cpp:85, `Engine::getSong()->tracks()`) dereferences the same way with no check, and it relies on the same gate. A null check added only in `allChains()` would make `oop.get_state` answer differently from its siblings on a path that cannot be reached, which is a behaviour change without a bug to fix.

## What I did not establish (residual risk)

- The gate is evaluated on the calling thread; the handler runs later on the UI thread (ControlRegistry.cpp:296, `BlockingQueuedConnection`). A call admitted by the gate and still queued while `Engine::destroy()` runs on the UI thread would reach `handleGetState` with a null song. I found no path to that in this tree, because the socket closes before the engine is destroyed (main.cpp:1847-1862). I did not verify the ordering of `MainWindow` destruction (its `Engine::destroy()` call is src/gui/MainWindow.cpp:286) against `app->exec()`'s return.
- If that window is real, it is not specific to `oop.get_state`. Every `requiresEngine` command shares it. The right place for a fix is the registry (re-check readiness inside the UI-thread lambda), not one handler, and that is outside this cluster. Flagged for the owner, not changed here.

## Commands run and EXIT codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p19-8.r1/logs/`.

| Step | Command (run from the worktree) | EXIT |
| --- | --- | --- |
| Gate wrapper | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` (log `001-feature-check-pre-report.log`) | 1 - it stopped at feature_check.py:71 (`logs.mkdir`, PermissionError on `<worktree>-feature-check`, a sibling directory outside the write-jail). The wrapper's checks never ran. |
| fork-sources | `bash tests/fork-sources-gate.sh` (log `002-gate-fork-sources.log`) | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` (log `003-gate-all-sources-reproduce.log`) | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` (log `004-gate-no-upstream-regression.log`) | 0 |
| check-namespace | `tests/scripted/check-namespace` (log `005-gate-check-namespace.log`) | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` (log `006-gate-no-tautology.log`) | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` (log `007-gate-doc-figures.log`) | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` (log `008-gate-complexity.log`) | 1 - the output lists `FAIL: complexity ratchet regressed`, and those items are not in any file this branch changes. |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` (log `009-gate-file-length.log`) | 1 - the output lists three `REGRESSION` items, all in test files this branch does not touch (`UpnpMapperFakeRouterTest.cpp`, `VideoTrackTest.cpp`, `ArrangerTimelineTest.cpp`). |

The two ratchets are red on the train, as the brief says. The wrapper counts only items in files the branch changed, and this branch changes only the report, so both would count as "no new item in your files". I ran the raw scripts instead of the wrapper because the wrapper cannot create its log directory from inside the jail.

No build and no ctest were run: no product file changed, and the finding does not need a new test.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p19-8.md` (this report)

No product code, test, registration or header changed.

## Self-check

- **Regression test fails with the product change reverted and passes with it (exit codes):** not applicable. There is no product change to revert and no test was added, because the verdict is FALSE-POSITIVE (procedure step 2 goes straight to the report). Nothing in this branch is claimed to be regression-proofed.
- **Test is deterministic (seam or injected values):** not applicable, no test added.
- **Fix at the cause and complete; every caller and sibling checked:** not applicable, no fix made. The siblings were checked for the claim in the finding: `ControlCommandsOutOfProcess*.cpp` has one `getSong` use (ControlCommandsOutOfProcess.cpp:141), and `dsp.get_state` (ControlCommandsDsp.cpp:85) walks the song unchecked behind the same gate. The residual registry-level window is described above and is not fixed here.
- **No collateral damage:** nothing changed in product code, so no saved project, limit or caller is affected.
- **FALSE-POSITIVE evidence (file:line):** ControlCommandsOutOfProcess.cpp (registration of `oop.get_state`, no `requiresEngine` override); include/ControlRegistry.h:104 (default `true`); src/core/ControlRegistry.cpp:205-207 (`isEngineReady`) and 283-291 (the gate, `Busy`); src/core/main.cpp:1829-1831 (`setReady(true)` after startup) and 1847-1862 (server closed before engine destroy); tests/src/core/RecordToolsNoEngineTest.cpp:20-27 and 84-100 (same contract, tested for the sibling commands).
- **Gates row (FAIL in submit_check):** the gate wrapper cannot run from inside this write-jail. `feature_check.py:71` (`logs.mkdir`) needs `<worktree>-feature-check`, a sibling directory outside the worktree, and it raised PermissionError (log `001-feature-check-pre-report.log`, EXIT=1). I ran the same six simple gates and the two ratchets directly (table in the section above). All six simple gates exit 0. The two ratchets exit 1 on items in test files this branch does not touch. Under the wrapper's own rule (items in changed files only) these would count as PASS. The FAIL is a tooling limit of this session, not a failing gate on this branch. Please re-run the wrapper from outside the jail to confirm the row.
