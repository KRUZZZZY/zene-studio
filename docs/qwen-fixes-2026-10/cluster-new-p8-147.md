# Cluster new-p8-147: Invalid marker actions fail silently

Verdict: **FIXED**

## Evidence

In [MarkerActions.cpp](../../src/core/MarkerActions.cpp#L172), `runMarkerAction` now checks JSON parsing and requires a JSON object before invoking the command. It logs parse errors, non-object arguments, and failed `ControlResult`s. Empty `args` still means an empty object, preserving existing no-argument behavior. `advance` calls the helper for each crossed marker at line 220.

The regression in [SessionPerformTest.cpp](../../tests/src/core/SessionPerformTest.cpp#L177) crosses one marker with malformed JSON, an array, and an object rejected by command validation. It asserts a warning for each case. Before the production fix, the warning count was zero.

Affected family: `SessionPerformTest` — **1 CTest target executed (1/1, 0 skipped)**.

## Verification commands

All build commands ran from the worktree root. All ctest commands ran from `build/tests`. Build output is in the named log file; ctest exit codes are the shell exit codes.

| Purpose | Exact command | Exit |
| --- | --- | ---: |
| Regression build against original production code | `cmake --build build --target zene SessionPerformTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-147.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Initial RED against original production code | `ctest -R '^SessionPerformTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-147.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 8 |
| First production-fix build | `cmake --build build --target zene SessionPerformTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-147.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| First fix test attempt; its message substring assertion needed correction (`invalid JSON` to `args JSON`) | `ctest -R '^SessionPerformTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-147.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 8 |
| Final test build before first GREEN | `cmake --build build --target zene SessionPerformTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-147.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| GREEN with production fix | `ctest -R '^SessionPerformTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-147.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Save production patch | `git diff -- src/core/MarkerActions.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-147.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Reverse production patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-147.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Reversion-proof build | `cmake --build build --target zene SessionPerformTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-147.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| RED with production patch reversed; warning-count assertion failed (0 actual, 1 expected) | `ctest -R '^SessionPerformTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-147.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-147.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Restored build | `cmake --build build --target zene SessionPerformTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-147.r1/logs/build-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| GREEN after restoring production patch | `ctest -R '^SessionPerformTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-147.r1/logs/test-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-147.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-147.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Final complexity-gate rerun | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-147.r1/logs/gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Final file-length-gate rerun | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-147.r1/logs/gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |

## Files committed

- `src/core/MarkerActions.cpp`
- `tests/src/core/SessionPerformTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-147.md`
