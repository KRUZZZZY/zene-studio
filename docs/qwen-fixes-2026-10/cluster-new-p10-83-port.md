# Cluster new-p10-83-port: a throwing handler leaves the registry's dispatch depth raised

**Verdict: FIXED**

## Evidence

The bug is real in this tree, before the change: `src/core/ControlRegistry.cpp:413-418` incremented `m_depth` and the two class counts, called `handler(args)`, then restored them on the straight-line path only. A throw unwound past both restores. `m_depth` gates the command observer at `include/ControlRegistry.h:299-302` (`m_depth == 0`), and `m_mutatingDepth`/`m_wrapperDepth` (`include/ControlRegistry.h:304-307`) decide whether the next mutating call records its own step (`src/core/ControlRegistry.cpp:405-409`, `recordCall` at `486-505`).

The fix at `src/core/ControlRegistry.cpp:411-439` runs the handler under `DepthGuard`: its constructor increments `m_depth` and its destructor decrements it and restores `m_mutatingDepth`/`m_wrapperDepth` on every exit, including exception unwinding. The guard is declared after `EditBracket` inside the handler scope, so normal returns keep the previous order (counts restored before `ProjectJournal::endEdit`).

Handler exceptions still propagate unchanged. The error vocabulary is closed (`include/ControlRegistry.h:44-55`) and `ControlServer::dispatchLine` invokes the registry without a local translation (`src/core/ControlServer.cpp:512`), so no new exception-to-control-error policy is invented here; the round-1 QA accepted that note (`ACCEPT-NOTE`, `qa/reviews/new-p10-83.r1.md`). This fix restores the registry's invariants on unwinding and changes no other behaviour.

## Regression test

`tests/src/core/ControlRegistryNestingTest.cpp:82-161` adds two deterministic cases; the handler is the seam, and it throws a fixed `std::runtime_error` (no wall-clock timing, sleeps or races):

- `throwingHandlerRestoresDispatchDepth` throws from a non-mutating handler, catches it at the caller, then verifies the next top-level mutating command reaches the observer and records its transaction. Before the fix the observer list is empty and the transaction list is empty (the leaked wrapper count holds the record).
- `throwingMutatingHandlerRestoresMutatingDepth` throws from a mutating handler, then verifies the next top-level mutating command records its own step. Before the fix the leaked mutating count makes it look nested, so nothing is recorded.

## Commands run and exit codes

All build logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-83-port.r2/logs/`. RED was produced by reversing only the product hunk, rebuilding, and running the test; GREEN is the same commands after reapplying it.

| Phase | Exact command | Exit |
|---|---|---:|
| Produce reverse patch | `git diff -- src/core/ControlRegistry.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-83-port.r2/logs/fix.patch` (worktree) | 0 |
| Reverse the fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-83-port.r2/logs/fix.patch` (worktree) | 0 |
| RED build | `cmake --build build --target zene ControlRegistryNestingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-83-port.r2/logs/build-6-red-nesting.log 2>&1` | 0 |
| RED test | `ctest -R '^ControlRegistryNestingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-83-port.r2/logs/test-6-red-nesting.log 2>&1` (cwd `build/tests`) | 8 |
| Reapply the fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-83-port.r2/logs/fix.patch` (worktree) | 0 |
| GREEN build | `cmake --build build --target zene ControlRegistryNestingTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-83-port.r2/logs/build-7-green-nesting.log 2>&1` | 0 |
| GREEN test | `ctest -R '^ControlRegistryNestingTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-83-port.r2/logs/test-7-green-nesting.log 2>&1` (cwd `build/tests`) | 0 |
| Family build | `cmake --build build --target zene ControlRegistryTest ControlRegistryNestingTest MpeInputActionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-83-port.r2/logs/build-8-family.log 2>&1` | 0 |
| Family test | `ctest -R '^(ControlRegistryTest|ControlRegistryNestingTest|MpeInputActionTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-83-port.r2/logs/test-8-family.log 2>&1` (cwd `build/tests`) | 0 |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-83-port > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-83-port.r2/logs/gates-2.log 2>&1` | 0 |

The RED run failed at both new assertions: `throwingHandlerRestoresDispatchDepth` saw an empty observer list (`Actual size: 0`, `Expected size: 1`) and `throwingMutatingHandlerRestoresMutatingDepth` saw `transactions.size()` 0 instead of 1. The GREEN run passed 1/1 CTest test; the family run passed 3/3 tests (`ControlRegistryTest`, `ControlRegistryNestingTest`, `MpeInputActionTest`).

An earlier placement of the same two cases in `ControlRegistryTest.cpp` pushed that file to 574 code lines; the first gate run (`logs/gates-1.log`) correctly failed file-length. The cases were moved to `ControlRegistryNestingTest.cpp` (396 code lines) and the final `gates-2.log` is PASS on every row: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity (no new item), file-length (no new item).

## Files committed

- `src/core/ControlRegistry.cpp` - the scope guard.
- `tests/src/core/ControlRegistryNestingTest.cpp` - the two regression cases.
- `docs/qwen-fixes-2026-10/cluster-new-p10-83-port.md` - this report.

## Self-check

- **Regression fails reverted, passes restored:** reverse product patch EXIT 0, rebuild EXIT 0, `ctest -R '^ControlRegistryNestingTest$'` EXIT 8 with the two assertions above; reapply EXIT 0, rebuild EXIT 0, same test EXIT 0. Exact commands are in the table.
- **Deterministic:** both cases inject a fixed `std::runtime_error` from the registered handler and catch it at the call site; there is no wall-clock timing, sleep, thread or race. The assertions read the observer list and the transaction array synchronously on the UI thread.
- **Cause and completeness:** `runHandler` is the only place that calls a command handler (`src/core/ControlRegistry.cpp:438`), and `m_depth`/`m_mutatingDepth`/`m_wrapperDepth` are only written there (`src/core/ControlRegistry.cpp:405-437`). The guard restores all three counts on return and on exception, so the observer gate, collaboration interception depth and the nested/wrapped record rules all see a balanced depth after a throw. The declaration/definitions are in the existing header (`include/ControlRegistry.h:304-307`) and required no signature change.
- **No collateral damage:** the fix neither adds nor changes a persistent format, limit, command or schema; it only changes what happens while unwinding a handler exception. Normal handler calls execute the same increments/decrements in the same order as before, and the family tests plus all gates pass.
- **Undo/realtime:** no audio-thread code is touched; the guard has no allocation, lock, wait, I/O or logging, only integer writes. A successful command still records exactly its previous one step; a throwing command records nothing, as before.
- **Verdict FIXED:** the pre-fix code at `src/core/ControlRegistry.cpp:413-418` skipped the decrements on a throw, and the fixed scope guard at `src/core/ControlRegistry.cpp:411-439` restores them; the regression proves both the depth and the sibling class counts.
