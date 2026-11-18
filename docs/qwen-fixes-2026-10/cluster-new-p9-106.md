# Cluster new-p9-106

Verdict: **FIXED**

`font()` used `TextRole` directly as an index into its five-entry pixel-size table. An out-of-range value could read beyond the array. [DesignTokens.cpp:310](../../src/gui/shell/DesignTokens.cpp#L310) now maps invalid roles to `TextRole::Body` before selecting size or weight. The regression at [DesignTokensTest.cpp:143](../../tests/src/gui/DesignTokensTest.cpp#L143) checks that role value `5` has Body's pixel size. The enum's five declared roles are at [DesignTokens.h:130](../../include/DesignTokens.h#L130).

Both changed source and test files were already registered in `tests/fork-sources.txt`; no registration change was needed.

## Evidence

| Check | Command and working directory | Exit |
|---|---|---:|
| Build regression on original production code (worktree) | `cmake --build build --target DesignTokensTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-106.r1/logs/build-1.log 2>&1` | 0 |
| RED on original code (`build/tests`) | `ctest -R '^DesignTokensTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-106.r1/logs/test-1.log 2>&1` | 8 |
| Build with fix (`zene` and test target; worktree) | `cmake --build build --target zene DesignTokensTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-106.r1/logs/build-2.log 2>&1` | 0 |
| GREEN with fix (`build/tests`) | `ctest -R '^DesignTokensTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-106.r1/logs/test-2.log 2>&1` | 0 |
| Save and reverse product fix (worktree) | `git diff -- src/gui/shell/DesignTokens.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-106.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-106.r1/logs/fix.patch` | 0 |
| Rebuild with fix reversed (`zene` and test target; worktree) | `cmake --build build --target zene DesignTokensTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-106.r1/logs/build-3.log 2>&1` | 0 |
| RED with fix reversed (`build/tests`) | `ctest -R '^DesignTokensTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-106.r1/logs/test-3.log 2>&1` | 8 |
| Reapply product fix (worktree) | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-106.r1/logs/fix.patch` | 0 |
| Rebuild restored fix (`zene` and test target; worktree) | `cmake --build build --target zene DesignTokensTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-106.r1/logs/build-4.log 2>&1` | 0 |
| GREEN after restoring fix (`build/tests`) | `ctest -R '^DesignTokensTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-106.r1/logs/test-4.log 2>&1` | 0 |
| Complexity gate (worktree) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-106.r1/logs/complexity-gate-1.log 2>&1` | 0 |
| File-length gate (worktree) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-106.r1/logs/file-length-gate-1.log 2>&1` | 0 |

The affected ctest family executed one test case (`DesignTokensTest`) per run. The original and reversed-fix runs each failed specifically in `anInvalidTextRoleFallsBackToBody()`; the original run logged a zero-pixel-size warning and a failed comparison. The fixed runs each report `100% tests passed, 0 tests failed out of 1`.

`fff` grep was not exposed in this session; captured logs were searched with `rg` for `error`, `FAILED`, and `Assertion`, plus the test summaries.

## Committed files

- `src/gui/shell/DesignTokens.cpp`
- `tests/src/gui/DesignTokensTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-106.md`
