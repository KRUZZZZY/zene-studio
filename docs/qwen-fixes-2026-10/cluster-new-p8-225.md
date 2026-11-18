# Cluster new-p8-225

**Verdict: FIXED**

`Shell::restorePersonal()` previously cleared `ui/shell.personal` before parsing it. The function now parses and checks the JSON object first; an empty or malformed object returns `false` while leaving the saved text available. A usable object is still cleared before it is restored.

## File evidence

- [src/gui/shell/ShellLayoutScope.cpp:54](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-225/src/gui/shell/ShellLayoutScope.cpp:54): `restorePersonal()` parses at line 57, returns on an empty object at line 58, and only then clears the aside at line 59.
- [tests/src/gui/ShellHomesTest.cpp:317](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p8-225/tests/src/gui/ShellHomesTest.cpp:317): the regression supplies truncated JSON, checks `restorePersonal()` returns false, verifies the exact saved text remains, and verifies the visible view is unchanged.

## Regression evidence

The test was run against the original implementation first. It failed because the actual stored value was empty while the expected value was `{"view":`. The `ShellHomesTest` family contains one CTest entry; it ran once in each invocation below.

| Run | Command (working directory shown) | Exit |
|---|---|---:|
| Original build | `cmake --build build --target zene ShellHomesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-225.r1/logs/build-1-red.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` (worktree) | 0 |
| Original regression (RED) | `ctest -R ShellHomesTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-225.r1/logs/test-1-red.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` (`build/tests`) | 8; 1 test failed |
| Fixed build | `cmake --build build --target zene ShellHomesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-225.r1/logs/build-2-green.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` (worktree) | 0 |
| Fixed regression (GREEN) | `ctest -R ShellHomesTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-225.r1/logs/test-2-green.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` (`build/tests`) | 0; 1 test passed |

## Reversibility proof

The production-only patch was removed, rebuilt, and the same regression failed again; reapplying the patch, rebuilding, and rerunning passed.

| Step | Command (working directory shown) | Exit |
|---|---|---:|
| Save production patch and reverse it | `git diff -- src/gui/shell/ShellLayoutScope.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-225.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-225.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"` (worktree) | 0 |
| Rebuild reverted code | `cmake --build build --target zene ShellHomesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-225.r1/logs/build-3-proof-red.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` (worktree) | 0 |
| Reverted regression (RED) | `ctest -R ShellHomesTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-225.r1/logs/test-3-proof-red.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` (`build/tests`) | 8; 1 test failed on the same assertion |
| Reapply production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-225.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"` (worktree) | 0 |
| Rebuild fixed code | `cmake --build build --target zene ShellHomesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-225.r1/logs/build-4-proof-green.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` (worktree) | 0 |
| Reapplied regression (GREEN) | `ctest -R ShellHomesTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-225.r1/logs/test-4-proof-green.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` (`build/tests`) | 0; 1 test passed |

## Required gates

| Command | Exit |
|---|---:|
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-225.r1/logs/gate-complexity.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-225.r1/logs/gate-file-length.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |

## Files committed

- `src/gui/shell/ShellLayoutScope.cpp`
- `tests/src/gui/ShellHomesTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-225.md`
