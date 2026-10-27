Verdict: FALSE-POSITIVE

new-p21-62: targetProject dereferences Engine::getSong() without a null check (src/core/ControlCommandsRevisions.cpp:86)

The null dereference is real as code, but it is not reachable through the control surface. Every revisions command keeps the default `requiresEngine = true`, and `ControlRegistry::invoke()` refuses the call with a typed `busy` reply before the handler runs whenever `Engine::getSong()` is null. No product change is made, so no regression test is added.

## Evidence (file:line on branch bugs/qwen-new-p21-62, base cf3618e61)

| Claim | Evidence |
| --- | --- |
| The unchecked dereference exists | `src/core/ControlCommandsRevisions.cpp:86`: `if (target.isEmpty()) { target = Engine::getSong()->projectFileName(); }` |
| Three revisions handlers reach it with no `project` arg | `src/core/ControlCommandsRevisions.cpp:256`, `:302`, `:374` (`targetProject(args, &error)` in the handlers of `revisions.list`, `revisions.compare`, `revisions.restore`; `registerRevisionsList` starts at `:226`) |
| The revisions commands keep the default `requiresEngine = true` | `include/ControlRegistry.h:104` (`bool requiresEngine = true;`). The revisions file never sets it: `rg -n requiresEngine src/core/ControlCommandsRevisions.cpp` returns nothing. Only `src/core/ControlCommandsControl.cpp:171,195` and `src/core/ControlCommandsCollab.cpp:203` opt out, to answer liveness calls before the engine exists |
| The registry gate needs a non-null song | `src/core/ControlRegistry.cpp:207`: `return s_ready && Engine::getSong() != nullptr && Engine::mixer() != nullptr;` |
| The gate runs before the handler, and refuses typed | `src/core/ControlRegistry.cpp:283-289`: `if (cmd->requiresEngine && !isEngineReady())` returns `ControlErrorKind::Busy` with "the engine is not addressable yet"; the handler is only queued after this (`:296`) |
| The handler runs synchronously on the UI thread, so the check and the call are one step from the socket's view | `src/core/ControlRegistry.cpp:249-260`: `runOnUiThread` uses `Qt::BlockingQueuedConnection` |
| The song cannot disappear mid-call | `src/core/main.cpp:1831` sets ready only after startup; `:1849` closes the control socket (`controlServer->close()`) before `:1862` `Engine::destroy()`. The song is created at `src/core/Engine.cpp:72` and deleted at `:123` only in `destroy()` |
| Direct handler calls only happen inside an already-gated command | `src/core/ControlCommandsNoteAcross.cpp:130` (`command->handler(call)` inside a note command), `src/core/ControlCommandsRecordTools.cpp:66-69` (`handlerOf`, record tools), `src/core/ControlCommandsSessionPerform.cpp:267` (`open->handler(...)`). None of these can name a revisions command with the song null, because the outer command's own gate already passed |
| Precedent: the same guard is tested for sibling commands | `tests/src/core/RecordToolsNoEngineTest.cpp` (listed at `tests/CMakeLists.txt:890`) asserts that record tools on an instance with no engine get typed refusals and no null deref. This is the fix that followed Luna 3946 for `record.remaining` |

Residual note (not a defect today): `targetProject` has no local null check of its own, so it depends on the registry gate above. Any future revisions handler marked `requiresEngine = false`, or a caller that bypasses `invoke()`, would reach the dereference. If that ever happens, the fix belongs in `targetProject` (a null check there), and that change would need its own regression test. Nothing in this cluster needs it now.

## Commands run and exit codes

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Build of the existing no-engine test, first try | `cmake --build build --target RecordToolsNoEngineTest -j4 > .../logs/build-1.log 2>&1; echo EXIT=$?` | 2 | `build-1.log`: sandbox ccache could not create its temp file (`ccache: error: failed to create temporary file ... Permission denied`). This is an environment problem, not product code |
| Same build with ccache disabled | `CCACHE_DISABLE=1 cmake --build build --target RecordToolsNoEngineTest -j4 > .../logs/build-2.log 2>&1; echo EXIT=$?` | 0 | `build-2.log` |
| Run that test from build/tests | `cd build/tests && ctest -R RecordToolsNoEngine --output-on-failure > .../logs/test-2.log 2>&1; echo EXIT=$?` | 0 | `test-2.log`: "100% tests passed out of 1". `test-1.log` (EXIT=8, "Not Run") is the same run after the failed build, and is superseded |
| Gate, as the procedure names it | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 1 | Not logged to the LOG FOLDER. It crashed with `PermissionError` creating `<worktree>-feature-check` next to the worktree, which is outside the write-jail. It wrote nothing |
| Attempted workaround: detached scratch worktree | `git worktree add --detach $TMPDIR/... HEAD` | 97 | `gate-worktree-add.log`: refused by the bunny-guard ("not allowed in a Bunny lane"). Not retried and not bypassed |
| Gate, log location moved | `python3 -I .../logs/feature_check_logdir.py <worktree> > .../logs/gate-1.log 2>&1; echo EXIT=$?` | 0 | `gate-1.log`, `gate-1/*.log`. `feature_check_logdir.py` is a copy of `feature_check.py` with two lines changed: the log folder and `ROOT` (the baseline cache) point at their original locations. The checks and the baseline comparison are unchanged |

The `ROOT` copy reads the baseline `gate-baselines/cf3618e61a61a44554f4fbac3be29a1027e3a776.json`, which already exists, so the run wrote nothing outside the LOG FOLDER.

## Gate result (feature_check logic, run through the copy above)

```
feature-check zene-qwen-new-p21-62 (base cf3618e61, 0 changed files)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

"0 changed files" is expected: this report is the only change on the branch, so the gate ran against the pre-commit HEAD.

## Self-check

- **Regression test fails with the change reverted and passes with it (exit codes):** not applicable. The verdict is FALSE-POSITIVE, so no product change exists and no regression test is added. The closest existing check is `RecordToolsNoEngineTest`, which runs the same registry refusal for sibling commands and passed (`test-2.log`, EXIT=0, 1 test).
- **Test is deterministic:** not applicable, since no test is added. The only test run (`RecordToolsNoEngineTest`) uses no timing or threads of its own.
- **Fix is at the cause and complete:** not applicable, since no fix is made. The caller check is in the evidence table above: the three revisions handlers (`:256`, `:302`, `:374`) and the direct `handler(...)` callers (`ControlCommandsNoteAcross.cpp:130`, `ControlCommandsRecordTools.cpp:69`, `ControlCommandsSessionPerform.cpp:267`) are all gated or already inside a gated command.
- **No collateral damage:** not applicable, since no product code changes. Nothing outside `docs/qwen-fixes-2026-10/cluster-new-p21-62.md` is committed.
- **Verdict evidence (file:line / contract):** yes. The evidence table cites the dereference (`ControlCommandsRevisions.cpp:86`), the default `requiresEngine = true` (`include/ControlRegistry.h:104`), the refusal before the handler (`src/core/ControlRegistry.cpp:207`, `:283-289`), and the shutdown order (`src/core/main.cpp:1849`, `:1862`).
- **Gates:** the `feature_check.py` run as the procedure names it exits 1 for an environment reason, not a product one. The script creates `<worktree>-feature-check` next to the worktree, which is outside this lane's write-jail, so it raises `PermissionError` before any check runs. The same check logic, run from `feature_check_logdir.py` with only the log location changed, returns RESULT: PASS (`gate-1.log`). The `submit_check.py` "gates" item still reports FAIL, because it runs the unmodified script. The gate result is therefore unverified in the form the loop expects, and that needs a fix outside this lane (the script should take a log directory argument).
- **Build note:** the first build failed on a sandbox ccache permission error (`build-1.log`, EXIT=2). The rebuild with `CCACHE_DISABLE=1` succeeded (`build-2.log`, EXIT=0). Product sources were not touched.
