# Cluster new-p9-226: removing a layout item leaks it

Verdict: **FIXED**

`ControlLayout::takeAt()` removes a `QLayoutItem` from its map and returns the detached item (`src/gui/ControlLayout.cpp:176`, `src/gui/ControlLayout.cpp:178`). `LinkedModelGroupView::removeControl()` removed that item and deleted its widget, but did not release the item (`src/gui/LinkedModelGroupViews.cpp:135`, `src/gui/LinkedModelGroupViews.cpp:137`). It now deletes the detached item after deleting the widget (`src/gui/LinkedModelGroupViews.cpp:139`).

The regression uses the real `LinkedModelGroupView::removeControl()` path and records deallocation of the exact layout-item address (`tests/src/gui/LinkedModelGroupViewLifetimeTest.cpp:164`, `tests/src/gui/LinkedModelGroupViewLifetimeTest.cpp:178`). It failed on the original implementation with `wasFreed(itemAddress) returned FALSE`, passed with the fix, failed again when the product patch was reversed, and passed after the patch was restored.

## Verification evidence

The affected ctest family is `LinkedModelGroupViewLifetimeTest`; each run executed **1 test**. Build, test, and gate output was redirected to new numbered files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-226.r1/logs`; each log has its captured `EXIT` code appended.

| Phase | Command and working directory | Exit | Log/evidence |
|---|---|---:|---|
| Original rebuild | `cmake --build build --target zene` (worktree root) | 0 | `build-1.log` |
| Regression target build | `cmake --build build --target LinkedModelGroupViewLifetimeTest` (worktree root) | 0 | `build-2.log` |
| RED on original code | `ctest -R LinkedModelGroupViewLifetimeTest --output-on-failure` (`build/tests`) | 8 | `test-1.log`, exact assertion at test line 178 |
| Fixed build | `cmake --build build --target zene LinkedModelGroupViewLifetimeTest` (worktree root) | 0 | `build-3.log` |
| GREEN with fix | `ctest -R LinkedModelGroupViewLifetimeTest --output-on-failure` (`build/tests`) | 0 | `test-2.log`, 1/1 passed |
| Reverse-patch proof | `git diff -- src/gui/LinkedModelGroupViews.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-226.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-226.r1/logs/fix.patch` | 0 | `fix.patch` |
| Rebuild without fix | `cmake --build build --target zene LinkedModelGroupViewLifetimeTest` (worktree root) | 0 | `build-4.log` |
| RED after reversing patch | `ctest -R LinkedModelGroupViewLifetimeTest --output-on-failure` (`build/tests`) | 8 | `test-3.log`, same assertion at test line 178 |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-226.r1/logs/fix.patch` | 0 | Patch applied |
| Final rebuild | `cmake --build build --target zene LinkedModelGroupViewLifetimeTest` (worktree root) | 0 | `build-5.log` |
| GREEN after restoring fix | `ctest -R LinkedModelGroupViewLifetimeTest --output-on-failure` (`build/tests`) | 0 | `test-4.log`, 1/1 passed |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` (worktree root) | 0 | `gate-1.log` |
| File-length ratchet | `bash tests/file-length-gate.sh --check` (worktree root) | 0 | `gate-2.log` |

Build, test, and gate output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-226.r1/logs`. The requested `fff` grep tool was not exposed in the available tools and was not installed on `PATH`; I searched the logs with `rg` for `error`, `FAILED`, and `Assertion` instead.

## Files in the change

- `src/gui/LinkedModelGroupViews.cpp`
- `tests/src/gui/LinkedModelGroupViewLifetimeTest.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-226.md`
