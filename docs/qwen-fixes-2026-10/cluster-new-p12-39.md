# Cluster new-p12-39: safe-start state readiness

**Verdict: FIXED**

`ControlCommandsSafeStart.cpp:225` now sets `cmd.requiresEngine = false` for the read-only `safestart.get_state` command. The regression in `SafeStartTest.cpp:178-189` dispatches through the real registry with its startup flag enabled while `ControlRegistry::isEngineReady()` is false; it verifies the command succeeds and returns its state.

Before the fix, dispatch failed with `busy` / `engine_missing` before reaching the handler. Reversing only the production patch after the fix reproduced the same failure. Applying the patch again made the regression pass.

## Evidence

| Check | Exact command | Exit |
|---|---|---:|
| Initial RED | `QT_QPA_PLATFORM=offscreen ctest -R '^SafeStartTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-39.r1/logs/test-1.log 2>&1; code=$?; echo EXIT=$code` | 8 |
| Affected family GREEN | `ctest -R 'SafeStart' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-39.r1/logs/test-2.log 2>&1; code=$?; echo EXIT=$code` | 0 |
| No-fix rebuild | `cmake --build build --target zene SafeStartTest SafeStartLoadPathTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-39.r1/logs/build-5.log 2>&1; code=$?; echo EXIT=$code` | 0 |
| No-fix RED | `ctest -R '^SafeStartTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-39.r1/logs/test-3.log 2>&1; code=$?; echo EXIT=$code` | 8 |
| Restored-fix rebuild | `cmake --build build --target zene SafeStartTest SafeStartLoadPathTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-39.r1/logs/build-6.log 2>&1; code=$?; echo EXIT=$code` | 0 |
| Restored-fix GREEN | `ctest -R '^SafeStartTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-39.r1/logs/test-4.log 2>&1; code=$?; echo EXIT=$code` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-39.r1/logs/gate-1.log 2>&1; code=$?; echo EXIT=$code` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-39.r1/logs/gate-2.log 2>&1; code=$?; echo EXIT=$code` | 0 |

The affected CTest family executed **2 tests**: `SafeStartTest` and `SafeStartLoadPathTest`; both passed in the family run. The initial regression run reported 9 passed and 1 failed QtTest cases, with the new readiness assertion failing as intended.

## Reproduction patch sequence

From the worktree root:

```text
git diff -- src/core/ControlCommandsSafeStart.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-39.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-39.r1/logs/fix.patch; code=$?; echo EXIT=$code
```

Exit: **0**. After the no-fix RED run, the exact patch was restored:

```text
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-39.r1/logs/fix.patch; code=$?; echo EXIT=$code
```

Exit: **0**.

## Build history

The first two builds were test-authoring retries before the RED run; production code was unchanged. The first exposed unqualified `lmms` names and the second exposed the registry's private constructor. Both were corrected before the test ran.

- `cmake --build build --target zene SafeStartTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-39.r1/logs/build-1.log 2>&1; code=$?; echo EXIT=$code` — **2**
- `cmake --build build --target zene SafeStartTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-39.r1/logs/build-2.log 2>&1; code=$?; echo EXIT=$code` — **2**
- `cmake --build build --target zene SafeStartTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-39.r1/logs/build-3.log 2>&1; code=$?; echo EXIT=$code` — **0**
- `cmake --build build --target zene SafeStartTest SafeStartLoadPathTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-39.r1/logs/build-4.log 2>&1; code=$?; echo EXIT=$code` — **0**

The fff grep tool was not exposed in this session; build, test, and gate logs were searched with `rg`.

## Files committed

- `src/core/ControlCommandsSafeStart.cpp`
- `tests/src/core/SafeStartTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-39.md`
