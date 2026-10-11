# Cluster new-p9-172: FloatModelEditorBase null model paint

**Verdict: FIXED**

`ModelView` holds its model in a `QPointer`, so destroying the model clears the pointer while the editor may remain alive. Before the fix, `FloatModelEditorBase::paintEvent()` dereferenced that null pointer. It now returns before painting when there is no model, following the existing fader behavior.

## Evidence

- [src/gui/widgets/FloatModelEditorBase.cpp](../../src/gui/widgets/FloatModelEditorBase.cpp#L307): `paintEvent()` reads the model at line 309 and returns at line 310 before accessing its values.
- [tests/src/gui/VcaStripLifetimeTest.cpp](../../tests/src/gui/VcaStripLifetimeTest.cpp#L102): the regression destroys a model bound to a live editor, verifies the model pointer cleared, then forces a paint with `grab()`.
- [tests/upstream-modifications.txt](../../tests/upstream-modifications.txt#L794): records the inherited production-file change and regression coverage.

The regression failed with a segmentation fault on the original code and failed again when the production patch was removed. It passed with the patch restored. The affected CTest family contains one test (`VcaStripLifetimeTest`); the test passed after the fix.

## Commands and captured exit codes

Build commands ran from the worktree root. Each CTest command ran from `build/tests`. `rc` captures the command's actual status before the wrapper prints it as `EXIT=...`.

| Phase | Exact command | Captured exit |
|---|---|---:|
| Build baseline regression | `cmake --build build --target zene VcaStripLifetimeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` | 0 |
| RED on original code | `ctest -R VcaStripLifetimeTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` | 8 (1/1 failed, SegFault) |
| Build with fix | `cmake --build build --target zene VcaStripLifetimeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` | 0 |
| GREEN with fix | `ctest -R VcaStripLifetimeTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` | 0 (1/1 passed) |
| Save patch and remove fix | `git diff -- src/gui/widgets/FloatModelEditorBase.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit 0` | 0 |
| Rebuild without fix | `cmake --build build --target zene VcaStripLifetimeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` | 0 |
| RED without fix | `ctest -R VcaStripLifetimeTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` | 8 (1/1 failed, SegFault) |
| Restore fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit 0` | 0 |
| Rebuild with restored fix | `cmake --build build --target zene VcaStripLifetimeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` | 0 |
| GREEN proof | `ctest -R VcaStripLifetimeTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` | 0 (1/1 passed) |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc; exit 0` | 0 |

The fff grep tool was not exposed in this session. I checked the saved logs with targeted, one-term `rg -n` searches and did not dump whole logs.

## Files committed

- `src/gui/widgets/FloatModelEditorBase.cpp`
- `tests/src/gui/VcaStripLifetimeTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-172.md`
