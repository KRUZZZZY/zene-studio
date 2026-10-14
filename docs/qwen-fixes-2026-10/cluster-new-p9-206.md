# Cluster new-p9-206

**Verdict: FIXED**

`ControllerSource` previously passed duplicate ticks to `clip.controllers_set`, which keeps only one point per tick. Position edits now refuse a tick occupied by another point at [ItemListNoteSources.cpp:238](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-206/src/gui/noteeditor/ItemListNoteSources.cpp:238), and inserts refuse an occupied next-grid tick at [ItemListNoteSources.cpp:262](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-206/src/gui/noteeditor/ItemListNoteSources.cpp:262). The GUI regression cases are at [ItemListPanelTest.cpp:224](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-206/tests/src/gui/ItemListPanelTest.cpp:224) and [ItemListPanelTest.cpp:253](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-206/tests/src/gui/ItemListPanelTest.cpp:253).

## Evidence

All build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs`.

| Stage | Working directory | Exact command | Exit | Result |
|---|---|---|---:|---|
| RED: remove production fix | Worktree | `git diff -- src/gui/noteeditor/ItemListNoteSources.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs/fix.patch; result=$?; echo EXIT=$result > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs/patch-reverse.log; exit $result` | 0 | Inverse patch applied. |
| RED: rebuild | Worktree | `cmake --build build --target zene ItemListPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs/build-6.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs/build-6.log; exit $result` | 0 | Rebuilt app and affected test. |
| RED: regression | `build/tests` | `ctest -R '^ItemListPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs/test-6.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs/test-6.log; exit $result` | 8 | 1/1 registered CTest target ran; both collision regression slots failed (11 passed, 2 failed inside QtTest). |
| GREEN: reapply production fix | Worktree | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs/fix.patch; result=$?; echo EXIT=$result > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs/patch-apply.log; exit $result` | 0 | Production fix restored. |
| GREEN: rebuild | Worktree | `cmake --build build --target zene ItemListPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs/build-7.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs/build-7.log; exit $result` | 0 | Rebuilt app and affected test. |
| GREEN: regression | `build/tests` | `ctest -R '^ItemListPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs/test-7.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs/test-7.log; exit $result` | 0 | 1/1 registered CTest target passed. |
| Complexity gate | Worktree | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs/gate-1.log; exit $result` | 0 | Passed. |
| File-length gate | Worktree | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-206.r1/logs/gate-2.log; exit $result` | 0 | Passed. |

## Files committed

- `src/gui/noteeditor/ItemListNoteSources.cpp`
- `tests/src/gui/ItemListPanelTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-206.md`
