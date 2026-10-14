# Cluster new-p9-172: FloatModelEditorBase after model destruction

**Verdict: FIXED**

FloatModelEditorBase no longer watches its constructor-created placeholder model. ModelView::setModel() destroys that placeholder before binding the real model, so watching it treated the intentional replacement as loss of the editor's model. The editor starts watching after setModel() binds a model and disables itself if that bound model is later destroyed. The existing paintEvent() null guard remains in place.

## Evidence

- [FloatModelEditorBase.cpp](../../src/gui/widgets/FloatModelEditorBase.cpp#L66): the constructor initializes the placeholder without installing a lifetime watcher; [lines 75–87](../../src/gui/widgets/FloatModelEditorBase.cpp#L75) install the watcher for the model bound by setModel() and disable the editor after its QPointer clears.
- [FloatModelEditorBase.cpp](../../src/gui/widgets/FloatModelEditorBase.cpp#L323): paintEvent() returns when model() is null.
- [VcaStripLifetimeTest.cpp](../../tests/src/gui/VcaStripLifetimeTest.cpp#L102): the regression asserts the editor is enabled immediately after binding, then disabled after deleting the bound model, and paints after deletion.

The regression failed before the production edit at the immediate enabled-state assertion (CTest exit 8). It failed again after reversing only the production patch and rebuilding (CTest exit 8). With the patch restored, the final run passed. The affected CTest family executed one test target (1/1 passed).

## Commands and captured exit codes

Build commands ran from the worktree root. CTest commands ran from build/tests. Every output was saved in the round 3 log folder.

| Phase | Exact command | Exit |
|---|---|---:|
| Build regression before production edit | cmake --build build --target VcaStripLifetimeTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r3/logs/build-1.log 2>&1; echo EXIT=$? | 0 |
| RED before production edit | ctest -R '^VcaStripLifetimeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r3/logs/red-1.log 2>&1; echo EXIT=$? | 8 (the new editor.isEnabled() assertion failed) |
| Build app and test with fix | cmake --build build --target zene VcaStripLifetimeTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r3/logs/build-2.log 2>&1; echo EXIT=$? | 0 |
| GREEN with fix | ctest -R '^VcaStripLifetimeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r3/logs/green-1.log 2>&1; echo EXIT=$? | 0 (1/1 passed) |
| Save production patch and reverse it | git diff -- src/gui/widgets/FloatModelEditorBase.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r3/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r3/logs/fix.patch; echo EXIT=$? | 0 |
| Rebuild with production patch reversed | cmake --build build --target zene VcaStripLifetimeTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r3/logs/build-3.log 2>&1; echo EXIT=$? | 0 |
| RED with production patch reversed | ctest -R '^VcaStripLifetimeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r3/logs/red-2.log 2>&1; echo EXIT=$? | 8 (the same editor.isEnabled() assertion failed) |
| Restore production patch | git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r3/logs/fix.patch; echo EXIT=$? | 0 |
| Final rebuild of app and test | cmake --build build --target zene VcaStripLifetimeTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r3/logs/build-4.log 2>&1; echo EXIT=$? | 0 |
| Final GREEN regression | ctest -R '^VcaStripLifetimeTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r3/logs/green-2.log 2>&1; echo EXIT=$? | 0 (1/1 passed) |
| Complexity gate after report write | bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r3/logs/gate-3.log 2>&1; echo EXIT=$? | 0 |
| File-length gate after report write | bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-172.r3/logs/gate-4.log 2>&1; echo EXIT=$? | 0 |

The fff grep tool was unavailable in this session (command -v fff returned no path, and it was absent from the exposed tools), so I searched the saved logs with targeted rg queries. The expected RED logs identify the immediate enabled-state assertion; the final CTest log reports 100% tests passed, 0 tests failed out of 1.

## Files committed

- src/gui/widgets/FloatModelEditorBase.cpp
- tests/src/gui/VcaStripLifetimeTest.cpp
- docs/qwen-fixes-2026-10/cluster-new-p9-172.md
