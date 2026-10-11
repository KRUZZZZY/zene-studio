# Cluster new-p9-172: FloatModelEditorBase after model destruction

**Verdict: FIXED**

`ModelView` stores the binding in a `QPointer`. `FloatModelEditorBase` now watches the initial model and each model passed to `setModel()`. When the bound model is destroyed and the pointer clears, the editor is disabled, preventing user input paths from operating on a missing model. The existing `paintEvent()` null guard remains in place for pending paints.

## Evidence

- [include/FloatModelEditorBase.h](../../include/FloatModelEditorBase.h#L56): declares the `setModel()` override; line 237 declares its lifetime watcher.
- [src/gui/widgets/FloatModelEditorBase.cpp](../../src/gui/widgets/FloatModelEditorBase.cpp#L66): the constructor watches its initial model; lines 76-88 watch later bindings and disable the editor after the current `QPointer` clears.
- [src/gui/widgets/FloatModelEditorBase.cpp](../../src/gui/widgets/FloatModelEditorBase.cpp#L324): `paintEvent()` still returns when the model is null.
- [tests/src/gui/VcaStripLifetimeTest.cpp](../../tests/src/gui/VcaStripLifetimeTest.cpp#L102): the regression deletes the bound model, verifies the pointer cleared and editor disabled, then forces a paint with `grab()`.
- [tests/upstream-modifications.txt](../../tests/upstream-modifications.txt#L794): records the inherited production changes; [tests/fork-sources.txt](../../tests/fork-sources.txt#L2093) registers the product regression file.

The regression failed before the production change and failed again after removing the production patch and rebuilding: `!editor.isEnabled()` returned false. With the patch restored, the test passed. The affected CTest family executed one test (`1/1`).

## Commands and captured exit codes

Build commands ran from the worktree root. CTest commands ran from `build/tests`. The commands below include the shell redirection and `EXIT` marker used for the saved logs.

| Phase | Exact command | Exit |
|---|---|---:|
| Build regression before production change | `cmake --build build --target VcaStripLifetimeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r2/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| RED before production change | `ctest -R '^VcaStripLifetimeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r2/logs/test-1.log 2>&1; echo EXIT=$?` | 8 (1/1 failed on `!editor.isEnabled()`) |
| Build with production change | `cmake --build build --target zene VcaStripLifetimeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r2/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| GREEN with production change | `ctest -R '^VcaStripLifetimeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r2/logs/test-2.log 2>&1; echo EXIT=$?` | 0 (1/1 passed) |
| Save patch and remove production change | `git diff -- include/FloatModelEditorBase.h src/gui/widgets/FloatModelEditorBase.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r2/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild without production change | `cmake --build build --target zene VcaStripLifetimeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r2/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| RED after removing production change | `ctest -R '^VcaStripLifetimeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r2/logs/test-3.log 2>&1; echo EXIT=$?` | 8 (1/1 failed on `!editor.isEnabled()`) |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r2/logs/fix.patch; echo EXIT=$?` | 0 |
| Final build of app and regression | `cmake --build build --target zene VcaStripLifetimeTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r2/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| Final GREEN regression | `ctest -R '^VcaStripLifetimeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r2/logs/test-4.log 2>&1; echo EXIT=$?` | 0 (1/1 passed) |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r2/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r2/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |
| Final complexity gate (after report write) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r2/logs/gate-3.log 2>&1; echo EXIT=$?` | 0 |
| Final file-length gate (after report write) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r2/logs/gate-4.log 2>&1; echo EXIT=$?` | 0 |

An initial identical post-change build wrote `build-2.log` but the tool wait returned without an `EXIT` marker; I reran it and recorded `build-3.log` with exit 0. No exit result is claimed for that first invocation.

The fff grep tool was not available in this session (`command -v fff` returned no path and it was absent from the exposed tools). I searched the saved logs with targeted `rg` queries instead of dumping whole logs.

## Files committed

- `include/FloatModelEditorBase.h`
- `src/gui/widgets/FloatModelEditorBase.cpp`
- `tests/src/gui/VcaStripLifetimeTest.cpp`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-172.md`
