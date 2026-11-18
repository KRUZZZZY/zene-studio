# Cluster new-p8-259

Verdict: **FIXED**

`resolveEssence()` now appends an unsupported-decoder warning only while the
report has fewer than `AafMaxWarnings` entries, matching `collectLog()`; it
still clears the unsupported media path. The guard is at
[`src/core/AafImportRead.cpp:139`](../../src/core/AafImportRead.cpp#L139).

The regression at [`tests/src/core/AafImportTest.cpp:310`](../../tests/src/core/AafImportTest.cpp#L310)
passes 37 unsupported external essence paths through the resolver. Before the
fix, it observed 37 warnings instead of the limit of 32. The same assertion
failed after the production patch was reversed, then passed after it was
reapplied.

## Verification evidence

CTest commands below were run from `build/tests`; build, patch, and gate
commands were run from the worktree root. Each build and test output was saved
in the requested log folder. The affected family selected and executed one
CTest test, `AafImportTest` (1/1). Its original-code and reverse-patch runs
failed on the new assertion; the restored-code run passed (1/1).

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Build regression on original product code | `cmake --build build --target AafImportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-259.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 | Built `AafImportTest`. |
| RED on original product code | `ctest -R AafImportTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-259.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | 11 QtTest cases passed; the regression failed with actual 37, expected 32. |
| Reverse the product patch | `git diff -- src/core/AafImportRead.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-259.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-259.r1/logs/fix.patch; echo EXIT=$?` | 0 | Restored the original resolver behavior. |
| Rebuild without the fix | `cmake --build build --target zene AafImportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-259.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 | Rebuilt `zene` and the affected test target. |
| RED after reversing the fix | `ctest -R AafImportTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-259.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 8 | Same assertion failed with actual 37, expected 32. |
| Reapply the product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-259.r1/logs/fix.patch; echo EXIT=$?` | 0 | Restored the warning cap. |
| Rebuild with the fix | `cmake --build build --target zene AafImportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-259.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 | Rebuilt `zene` and the affected test target. |
| GREEN | `ctest -R AafImportTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-259.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 0 | `AafImportTest` passed (1/1). |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-259.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 | Passed. |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-259.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 | Passed. |

The `fff` grep tool was not exposed in this session and no `fff` executable
was available, so saved logs were searched with `rg -n` for `error`, `FAILED`,
and `Assertion`.

## Files committed

- `src/core/AafImportRead.cpp` — cap unsupported-media warnings at the report limit.
- `tests/src/core/AafImportTest.cpp` — add the resolver regression test.
- `docs/qwen-fixes-2026-10/cluster-new-p8-259.md` — this verification report.

The source and test are already registered in `tests/fork-sources.txt`.
