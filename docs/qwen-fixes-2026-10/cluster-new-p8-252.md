# Cluster new-p8-252

Verdict: **FIXED**

Retrying a failed final step after the saved workflow shrinks could queue `from == steps.size()` and report a successful zero-step run. The retry path now refuses when `failedAt >= steps.size()`; skip still permits `from == steps.size()`.

## Evidence

| File | Evidence |
| --- | --- |
| `src/gui/WorkflowRun.cpp:230` | Resume bounds check distinguishes retry from skip. |
| `tests/src/core/CustomAccessTest.cpp:469` | Regression runs a three-step workflow whose last step fails, shortens it to two steps, expects retry to return `InvalidArgs`, and checks that skipping an unchanged final step remains valid. |

The regression failed before the fix: `retried.errorKind` was `0`, expected `ControlErrorKind::InvalidArgs` (`3`). It failed again with the production patch reversed, then passed after the patch was restored. Each `ctest -R CustomAccessTest` invocation ran 1 CTest test (`CustomAccessTest`, 1/1).

## Commands and exit codes

CTest commands ran from `build/tests`. Build, test, and gate output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-252.r1/logs`.

| Purpose | Exact command | Exit | Log |
| --- | --- | ---: | --- |
| Build regression test before fix | `cmake --build build --target CustomAccessTest` | 0 | `build-2.log` |
| Original-code RED | `ctest -R CustomAccessTest --output-on-failure` | 8 | `test-1.log` |
| Build fixed code | `cmake --build build --target zene CustomAccessTest -j3` | 0 | `build-3.log` |
| Fixed-code GREEN | `ctest -R CustomAccessTest --output-on-failure` | 0 | `test-2.log` |
| Save production patch and reverse it | `git diff -- src/gui/WorkflowRun.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-252.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-252.r1/logs/fix.patch` | 0 | `fix.patch` |
| Rebuild with fix reversed | `cmake --build build --target zene CustomAccessTest -j3` | 0 | `build-4.log` |
| Reversed-patch RED | `ctest -R CustomAccessTest --output-on-failure` | 8 | `test-3.log` |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-252.r1/logs/fix.patch` | 0 | `fix.patch` |
| Rebuild restored fix | `cmake --build build --target zene CustomAccessTest -j3` | 0 | `build-5.log` |
| Restored-code GREEN | `ctest -R CustomAccessTest --output-on-failure` | 0 | `test-4.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |

An initial duplicate pre-fix build wrote `build-1.log`, but its shell exit was not captured by the tool poll. The recorded `build-2.log` invocation is the build evidence used above.

The `fff grep` tool was not available in this tool context. Logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `src/gui/WorkflowRun.cpp`
- `tests/src/core/CustomAccessTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-252.md`
