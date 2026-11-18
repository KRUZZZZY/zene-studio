# Cluster new-p9-82

Verdict: **FIXED**

`PathUtil::baseLookup()` now distinguishes a recognized prefix from a legacy
relative path in `resolveAssetPath()`. If `PathUtil::toAbsolute()` cannot make
that recognized prefix absolute, the scanner leaves its path empty, marks it
`unresolved-prefix`, and does not join it to the project directory.

## Evidence

| Location | Evidence |
| --- | --- |
| `src/core/ControlProjectAssets.cpp:115` | Capture the recognized `PathUtil::Base` before conversion. |
| `src/core/ControlProjectAssets.cpp:123-127` | Return an unresolved prefix before the legacy `project-dir` fallback. |
| `include/ControlProjectAssets.h:72-73` | Document the new `resolvedVia` value. |
| `tests/src/core/ControlProjectArchiveTest.cpp:290-315` | Regression: a relative user-sample base and `usersample:missing.wav` must yield `unresolved_count == 1`, `resolved == false`, and an empty resolved path. |

The regression was RED before the fix: `unresolved_count` was 0, expected 1.
Removing the production patch reproduced the same failure; restoring it passed.
The affected CTest family executed **1/1** test (`ControlProjectArchiveTest`).

## Commands and results

Commands below were run from the worktree root unless a working directory is
shown. Build and test output was redirected to the listed new log file; each
runner appended and printed the command exit code.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| Baseline RED build (root) | `cmake --build build --target ControlProjectArchiveTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/red-build-1.log' 2>&1; result=$?; printf '\nEXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/red-build-1.log'; printf 'EXIT=%s\n' "$result"` | 0 |
| Baseline RED test (cwd `build/tests`) | `ctest -R '^ControlProjectArchiveTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/red-test-1.log' 2>&1; result=$?; printf '\nEXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/red-test-1.log'; printf 'EXIT=%s\n' "$result"` | 8 |
| Fixed GREEN build (root) | `cmake --build build --target zene ControlProjectArchiveTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/green-build-2.log' 2>&1; result=$?; printf '\nEXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/green-build-2.log'; printf 'EXIT=%s\n' "$result"` | 0 |
| Fixed GREEN test (cwd `build/tests`) | `ctest -R '^ControlProjectArchiveTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/green-test-2.log' 2>&1; result=$?; printf '\nEXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/green-test-2.log'; printf 'EXIT=%s\n' "$result"` | 0 |
| Save production patch | `git diff -- src/core/ControlProjectAssets.cpp include/ControlProjectAssets.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/fix.patch` | 0 |
| Reverse production patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/fix.patch` | 0 |
| Reverse-patch RED build (root) | `cmake --build build --target zene ControlProjectArchiveTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/proof-red-build-3.log' 2>&1; result=$?; printf '\nEXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/proof-red-build-3.log'; printf 'EXIT=%s\n' "$result"` | 0 |
| Reverse-patch RED test (cwd `build/tests`) | `ctest -R '^ControlProjectArchiveTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/proof-red-test-3.log' 2>&1; result=$?; printf '\nEXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/proof-red-test-3.log'; printf 'EXIT=%s\n' "$result"` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/fix.patch` | 0 |
| Restored GREEN build (root) | `cmake --build build --target zene ControlProjectArchiveTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/proof-green-build-4.log' 2>&1; result=$?; printf '\nEXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/proof-green-build-4.log'; printf 'EXIT=%s\n' "$result"` | 0 |
| Restored GREEN test (cwd `build/tests`) | `ctest -R '^ControlProjectArchiveTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/proof-green-test-4.log' 2>&1; result=$?; printf '\nEXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/proof-green-test-4.log'; printf 'EXIT=%s\n' "$result"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/complexity-gate-5.log' 2>&1; result=$?; printf '\nEXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/complexity-gate-5.log'; printf 'EXIT=%s\n' "$result"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/file-length-gate-6.log' 2>&1; result=$?; printf '\nEXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-82.r1/logs/file-length-gate-6.log'; printf 'EXIT=%s\n' "$result"` | 0 |

The `fff` grep tool was not available in this session; the required log checks
were performed with `rg` against the log folder.

## Files committed

- `include/ControlProjectAssets.h`
- `src/core/ControlProjectAssets.cpp`
- `tests/src/core/ControlProjectArchiveTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-82.md`
