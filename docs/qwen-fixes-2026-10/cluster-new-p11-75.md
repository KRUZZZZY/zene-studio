# Cluster new-p11-75

Verdict: **FIXED**

## Evidence

`src/core/ControlServer.cpp:482` now sends every nonblank dispatch response through `finishExchange`, which emits `exchanged` and returns the response. The malformed JSON path at line 492, request-ID and protocol refusals at lines 499–500, command validation at line 505, argument validation at line 510, and normal response at line 519 all use it. Blank input still returns without an exchange because it has no response.

`tests/src/core/ControlShutdownHookTest.cpp:74` adds a `QSignalSpy` regression for malformed JSON and a parsed request missing its ID. Before the fix the first assertion observed `exchanged.size() == 0`.

The affected ctest family is `^ControlShutdownHookTest$`: **1 ctest test ran**. The pre-fix and fix-reverted runs each failed that test; the fixed and restored runs each passed it.

## Commands and results

Commands below were run from the worktree unless the row says `build/tests`. Each build or test wrote its output to a new numbered file under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-75.r1/logs`.

| Stage | Exact command | EXIT |
|---|---|---:|
| Rebuild `zene` before RED (`build-2.log`) | `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-75.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Build regression target before RED (`build-3.log`) | `cmake --build build --target ControlShutdownHookTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-75.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| RED on original code (`test-1.log`, from `build/tests`) | `ctest -R '^ControlShutdownHookTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-75.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8; 1/1 failed, signal count was 0 |
| Build fixed code (`build-4.log`) | `cmake --build build --target zene ControlShutdownHookTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-75.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| GREEN (`test-2.log`, from `build/tests`) | `ctest -R '^ControlShutdownHookTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-75.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0; 1/1 passed |
| Remove product patch | `git diff -- src/core/ControlServer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-75.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-75.r1/logs/fix.patch` | 0 |
| Rebuild with fix removed (`build-5.log`) | `cmake --build build --target zene ControlShutdownHookTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-75.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| RED with fix removed (`test-3.log`, from `build/tests`) | `ctest -R '^ControlShutdownHookTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-75.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8; 1/1 failed, signal count was 0 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-75.r1/logs/fix.patch` | 0 |
| Rebuild restored code (`build-6.log`) | `cmake --build build --target zene ControlShutdownHookTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-75.r1/logs/build-6.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| GREEN after restore (`test-4.log`, from `build/tests`) | `ctest -R '^ControlShutdownHookTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-75.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0; 1/1 passed |
| Complexity gate (`gate-3.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-75.r1/logs/gate-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| File-length gate (`gate-4.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-75.r1/logs/gate-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |

An earlier initial `zene` build attempt did not return its wrapper result to the session. It is not used as evidence; the captured repeat above exited 0. The `fff` grep tool was not available in this session, so log checks used single-term `rg` searches.

## Files committed

- `src/core/ControlServer.cpp`
- `tests/src/core/ControlShutdownHookTest.cpp`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p11-75.md`
