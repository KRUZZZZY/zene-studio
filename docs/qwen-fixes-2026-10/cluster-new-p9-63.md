# Cluster new-p9-63: malformed theme import

**Verdict: FIXED**

`importedColors` previously converted a missing or non-object `colors` value into an empty object after checking only the format marker. With `replace:true`, this could clear stored overrides. It now requires `colors` to be an object and returns `InvalidArgs` otherwise.

## Evidence

| File and lines | Evidence |
| --- | --- |
| `src/gui/ControlCommandsTheme.cpp:231-237` | The file import checks `colors.isObject()` before returning colors for application. |
| `tests/src/gui/WorkspaceAccessTest.cpp:214-232` | Regression writes `{"format":"zene-theme"}`, attempts replacement while an accent override exists, and asserts rejection and preservation of the override. |

The focused test failed before the fix and also failed when the production patch was reversed. It passed after restoring the patch. The affected CTest family ran **1 test target** (`WorkspaceAccessTest`), which passed.

## Commands and results

Build and gate commands ran from the worktree root. Test commands ran from `build/tests`. Logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-63.r1/logs`. The `fff grep` tool was unavailable in this session, so targeted log searches used `rg`.

| Stage | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Initial build | `cmake --build build --target zene WorkspaceAccessTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-63.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 | Built regression against original behavior. |
| RED before fix | `./WorkspaceAccessTest aMalformedThemeFileCannotEraseOverrides > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-63.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 1 | `!imported.ok` failed because the malformed file was accepted. |
| Build after fix | `cmake --build build --target zene WorkspaceAccessTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-63.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 | Rebuilt product and test. |
| GREEN focused test | `./WorkspaceAccessTest aMalformedThemeFileCannotEraseOverrides > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-63.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 | Regression passed. |
| CTest family | `ctest -R WorkspaceAccessTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-63.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 | 1/1 CTest targets passed. |
| Reverse production fix | `git diff -- src/gui/ControlCommandsTheme.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-63.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-63.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 | Temporarily restored original behavior for sensitivity proof. |
| Rebuild with fix reversed | `cmake --build build --target zene WorkspaceAccessTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-63.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 | Rebuilt product and test. |
| RED with fix reversed | `./WorkspaceAccessTest aMalformedThemeFileCannotEraseOverrides > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-63.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 1 | The same `!imported.ok` assertion failed. |
| Restore production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-63.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 | Restored the patch used for final verification. |
| Final rebuild | `cmake --build build --target zene WorkspaceAccessTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-63.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 | Rebuilt product and test. |
| Final focused test | `./WorkspaceAccessTest aMalformedThemeFileCannotEraseOverrides > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-63.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 | Regression passed with restored fix. |
| Final CTest family | `ctest -R WorkspaceAccessTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-63.r1/logs/test-6.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 | 1/1 CTest targets passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-63.r1/logs/gate-4.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 | Passed without baseline changes. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-63.r1/logs/gate-5.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 | Passed. |

## Files committed

- `src/gui/ControlCommandsTheme.cpp`
- `tests/src/gui/WorkspaceAccessTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-63.md`
