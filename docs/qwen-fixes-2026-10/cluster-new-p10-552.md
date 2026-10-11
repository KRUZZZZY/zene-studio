# Cluster new-p10-552

Verdict: **FIXED**

## Evidence

- `plugins/ClapEffect/ClapHost.cpp:354-355` now returns false for negative frame counts and true for zero, preserving the zero-frame no-op.
- `tests/src/plugins/ClapHostTest.cpp:358-362` tests both outcomes on the prepared host. Before the fix, the assertion for `-1` failed because `process()` returned true. After the fix, `ClapHostTest` passed.
- The reverse-patch proof also made the same assertion fail; restoring the production patch made it pass again.
- `plugins/ClapEffect/ClapHost.cpp` is already registered in `tests/fork-sources.txt`. The test change is recorded in `tests/upstream-modifications.txt:768`.

Affected family: `ctest -R '^ClapHostTest$'` executed 1 registered test target. The pre-fix QtTest run reported 18 passed and 1 failed; the failing case was `testFrameCountValidation`. The post-fix CTest run passed 1/1.

The requested fff search tool was not available in this session. Log searches used `rg` against the specified log folder; the pre-fix assertion was found in `test-2.log` and reproduced in `test-4.log`.

## Commands and results

All logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-552.r1/logs`.

| Stage | Exact command | Working directory | Exit |
|---|---|---|---:|
| Initial test build | `cmake --build build --target ClapHostTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-552.r1/logs/build-1.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | Worktree | 0 |
| Initial RED | `ctest -R '^ClapHostTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-552.r1/logs/test-1.log 2>&1; test_exit=$?; echo EXIT=$test_exit` | `build/tests` | 8 |
| Confirm unchanged-code build | `cmake --build build --target zene ClapHostTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-552.r1/logs/build-2.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | Worktree | 0 |
| Confirmed RED | `ctest -R '^ClapHostTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-552.r1/logs/test-2.log 2>&1; test_exit=$?; echo EXIT=$test_exit` | `build/tests` | 8 |
| Fixed build | `cmake --build build --target zene ClapHostTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-552.r1/logs/build-3.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | Worktree | 0 |
| GREEN | `ctest -R '^ClapHostTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-552.r1/logs/test-3.log 2>&1; test_exit=$?; echo EXIT=$test_exit` | `build/tests` | 0 |
| Reverse production patch | `git diff -- plugins/ClapEffect/ClapHost.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-552.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-552.r1/logs/fix.patch; patch_exit=$?; echo EXIT=$patch_exit` | Worktree | 0 |
| Build with fix removed | `cmake --build build --target zene ClapHostTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-552.r1/logs/build-4.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | Worktree | 0 |
| Reverse-patch RED | `ctest -R '^ClapHostTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-552.r1/logs/test-4.log 2>&1; test_exit=$?; echo EXIT=$test_exit` | `build/tests` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-552.r1/logs/fix.patch; patch_exit=$?; echo EXIT=$patch_exit` | Worktree | 0 |
| Restored build | `cmake --build build --target zene ClapHostTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-552.r1/logs/build-5.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | Worktree | 0 |
| Restored GREEN | `ctest -R '^ClapHostTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-552.r1/logs/test-5.log 2>&1; test_exit=$?; echo EXIT=$test_exit` | `build/tests` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-552.r1/logs/gate-1.log 2>&1; gate_exit=$?; echo EXIT=$gate_exit` | Worktree | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-552.r1/logs/gate-2.log 2>&1; gate_exit=$?; echo EXIT=$gate_exit` | Worktree | 0 |

## Files committed

- `plugins/ClapEffect/ClapHost.cpp`
- `tests/src/plugins/ClapHostTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-552.md`
