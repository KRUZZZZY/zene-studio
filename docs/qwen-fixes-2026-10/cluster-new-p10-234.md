# Cluster new-p10-234

Verdict: FIXED

## Evidence

In [McuSurface.cpp](../../src/core/McuSurface.cpp#L192), `applyBank()` used the same track-index clamp for bank and channel movement. With ten bankable tracks, that let bank-right from index 8 land at 9; bank-left then landed at 1. The bank-button path now caps at the last eight-track page start. Channel-left/right still use the track-index clamp.

[McuSurfaceTest.cpp](../../tests/src/core/McuSurfaceTest.cpp#L168) adds a ten-track regression. It verifies paging from offset 8 stays at 8, bank-left returns to 0, and channel-right can still reach 9.

The regression failed before the fix with `afterBankRight` actual 9, expected 8. Removing the saved production patch reproduced the same failure after rebuilding both `zene` and `McuSurfaceTest`; restoring it made the family pass.

## Command evidence

Build commands ran from the worktree root. CTest commands ran from `build/tests`. Test family count is 1 CTest test (`McuSurfaceTest`), and it passed 1/1 with the fix.

| Evidence | Exact command | Exit |
|---|---|---:|
| Initial regression build | `cmake --build build --target McuSurfaceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-234.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Initial RED | `ctest -R '^McuSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-234.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 |
| First GREEN build | `cmake --build build --target zene McuSurfaceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-234.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| First GREEN | `ctest -R '^McuSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-234.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Remove production patch | `git diff -- src/core/McuSurface.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-234.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-234.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Reversal-proof build | `cmake --build build --target zene McuSurfaceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-234.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Reversal-proof RED | `ctest -R '^McuSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-234.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-234.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Final build | `cmake --build build --target zene McuSurfaceTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-234.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Final GREEN, 1/1 family test | `ctest -R '^McuSurfaceTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-234.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-234.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-234.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |

The `fff` grep tool was not available in this environment. I searched the saved logs with `rg`; the regression assertion and CTest family count are recorded above.

## Files committed

- `src/core/McuSurface.cpp`
- `tests/src/core/McuSurfaceTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-234.md`

