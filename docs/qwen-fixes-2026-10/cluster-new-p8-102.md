# Cluster new-p8-102: locked clips and track removal

**Verdict: FIXED**

`track.remove` was accepted for a track whose clip was individually locked. The guard in [EditLocks.cpp](../../src/core/EditLocks.cpp#L128) now resolves the target track and refuses removal when any clip on it has an individual lock. The check runs in `EditLocks::refuses()` after the existing track lock check and before the command handler ([EditLocks.cpp](../../src/core/EditLocks.cpp#L181)). The regression in [ProjectStateTest.cpp](../../tests/src/core/ProjectStateTest.cpp#L216) confirms refusal and that the track and clip remain present.

Before the fix, `ProjectStateTest::aLockedClipRefusesTrackRemoval()` failed at line 225: the command returned `errorKind` 0 instead of `ControlErrorKind::Refused`. Reversing only the production patch reproduced the same failure; reapplying it passed.

## Verification evidence

Build and test output was written to new files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r1/logs`. Each build command rebuilt both `zene` and `ProjectStateTest`; ctest ran from `build/tests`.

| Stage | Command | Exit | Log |
| --- | --- | ---: | --- |
| Original build | `cmake --build build --target zene ProjectStateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r1/logs/build-1.log 2>&1` | 0 | `build-1.log` |
| Original RED | `ctest -R '^ProjectStateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r1/logs/test-1.log 2>&1` (cwd: `build/tests`) | 8 | `test-1.log` |
| Fixed build | `cmake --build build --target zene ProjectStateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r1/logs/build-2.log 2>&1` | 0 | `build-2.log` |
| Fixed GREEN | `ctest -R '^ProjectStateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r1/logs/test-2.log 2>&1` (cwd: `build/tests`) | 0 | `test-2.log` |
| Reverse production patch: `git diff -- src/core/EditLocks.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r1/logs/fix.patch` | 0 | — |
| Reversed build | `cmake --build build --target zene ProjectStateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r1/logs/build-3.log 2>&1` | 0 | `build-3.log` |
| Reversed RED | `ctest -R '^ProjectStateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r1/logs/test-3.log 2>&1` (cwd: `build/tests`) | 8 | `test-3.log` |
| Reapply production patch: `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r1/logs/fix.patch` | 0 | — |
| Restored build | `cmake --build build --target zene ProjectStateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r1/logs/build-4.log 2>&1` | 0 | `build-4.log` |
| Restored GREEN | `ctest -R '^ProjectStateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r1/logs/test-4.log 2>&1` (cwd: `build/tests`) | 0 | `test-4.log` |
| Complexity gate: `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r1/logs/gate-1.log 2>&1` | 0 | `gate-1.log` |
| File length gate: `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-102.r1/logs/gate-2.log 2>&1` | 0 | `gate-2.log` |

Each ctest invocation ran exactly **1/1 affected-family test**, `ProjectStateTest`. The RED runs failed on the new regression; both GREEN runs passed. Build and gate exit codes are recorded in their respective logs.

The `fff` grep tool was not available in this session (`fff` was absent from the callable tools and `command -v fff` returned no path). I searched the relevant logs with `rg -n`, one term at a time (`error`, `FAILED`, and `Assertion`).

## Files committed

- `src/core/EditLocks.cpp`
- `tests/src/core/ProjectStateTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-102.md`
