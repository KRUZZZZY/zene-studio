# Cluster new-p10-154: negative hardware bus channel indices

Verdict: **FIXED**

## Evidence

`src/core/HardwareOutputBus.cpp:91` previously tested only `channel < side.used`; a negative channel therefore passed the test and was converted to `std::size_t` for the vector index. The condition now also requires `channel >= 0`. `tests/src/core/SurroundTest.cpp:196-202` adds a regression that expects `sample(-1, 0, master)` to return exact silence.

## Verification

Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-154.r1/logs`. Each build/test command below was run with stdout and stderr redirected to its listed log, followed by an `EXIT=<code>` line.

The exact shell form for each build/test row was `<listed command> > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-154.r1/logs/<listed log> 2>&1; status=$?; printf 'EXIT=%s\n' "$status" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-154.r1/logs/<listed log>; exit "$status"`. Gate rows used this same form with their listed `gate-*.log` path.

| Stage | Exact command | Working directory | Exit | Log/result |
|---|---|---|---:|---|
| Initial build with the first `QCOMPARE` assertion | `cmake --build build --target SurroundTest` | worktree root | 0 | `build-1.log` |
| Initial test attempt; unexpectedly passed, so not accepted as regression proof | `ctest -R '^SurroundTest$' --output-on-failure` | `build/tests` | 0 | `test-1.log` |
| Rebuild after changing the check to exact equality | `cmake --build build --target SurroundTest` | worktree root | 0 | `build-2.log` |
| **RED on original production code** | `ctest -R '^SurroundTest$' --output-on-failure` | `build/tests` | 8 | `test-2.log`: regression assertion failed; 7 passed, 1 failed |
| Fixed build, including `zene` | `cmake --build build --target zene SurroundTest` | worktree root | 0 | `build-3.log` |
| **GREEN** | `ctest -R '^SurroundTest$' --output-on-failure` | `build/tests` | 0 | `test-3.log`: 1/1 registered CTest test passed |
| Reverse the product fix: `git diff -- src/core/HardwareOutputBus.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-154.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-154.r1/logs/fix.patch` | worktree root | 0 | `proof-1.log` |
| Rebuild `zene` and test with fix reversed | `cmake --build build --target zene SurroundTest` | worktree root | 0 | `build-4.log` |
| **Reverse-patch RED** | `ctest -R '^SurroundTest$' --output-on-failure` | `build/tests` | 8 | `test-4.log`: same assertion failed; 7 passed, 1 failed |
| Restore product fix: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-154.r1/logs/fix.patch` | worktree root | 0 | `proof-2.log` |
| Rebuild restored `zene` and test | `cmake --build build --target zene SurroundTest` | worktree root | 0 | `build-5.log` |
| **Restore GREEN** | `ctest -R '^SurroundTest$' --output-on-failure` | `build/tests` | 0 | `test-5.log`: 1/1 registered CTest test passed |
| Complexity ratchet: `bash tests/complexity-gate.sh --check` | worktree root | 0 | `gate-1.log`, repeated after report creation in `gate-3.log` |
| File-length ratchet: `bash tests/file-length-gate.sh --check` | worktree root | 0 | `gate-2.log`, repeated after report creation in `gate-4.log` |

The affected CTest family contains one registered test (`SurroundTest`); its RED runs reported 7 passing and 1 failing QtTest rows, and its fixed runs passed. The first `QCOMPARE` attempt is recorded above because it passed on the buggy code; the exact-equality assertion replaced it before the accepted RED/GREEN proof.

## Files in the commit

- `src/core/HardwareOutputBus.cpp`
- `tests/src/core/SurroundTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-154.md`
