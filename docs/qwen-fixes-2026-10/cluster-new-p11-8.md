# Cluster new-p11-8

**Verdict: FIXED**

`ClipView::mousePressEvent` returned for every non-right click on a locked clip at
`src/gui/clips/ClipView.cpp:610-615`, preventing the existing Ctrl-middle mute handler from running.
The guard now lets Ctrl-middle through. Other locked middle clicks still return before deletion.
The regression is `tests/src/gui/ArrangerTimelineTest.cpp:255-269`;
`tests/upstream-modifications.txt` records the inherited production-file change.

## Evidence

All build commands ran from the worktree root. All CTest commands ran from `build/tests`.
The affected family is one CTest test (`ArrangerTimelineTest`); the original RED run reported
12 passing Qt cases and the new mute case failing.

| Stage | Exact command | Exit |
|---|---|---:|
| Original regression build | `cmake --build build --target ArrangerTimelineTest zene -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-8.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| RED on original code | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-8.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 |
| Build with fix | `cmake --build build --target ArrangerTimelineTest zene -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-8.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| Initial GREEN | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-8.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 |
| Revert only the product fix | `git diff -- src/gui/clips/ClipView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-8.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-8.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild without fix | `cmake --build build --target ArrangerTimelineTest zene -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-8.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| Sensitivity RED | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-8.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 |
| Restore product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-8.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild with fix restored | `cmake --build build --target ArrangerTimelineTest zene -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-8.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| Final GREEN | `ctest -R '^ArrangerTimelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-8.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-8.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p11-8.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

The RED assertion was `clip->isMuted()` false with message “Ctrl-middle-click did not mute the
locked clip”; the same failure occurred in the sensitivity run without the product patch. The
restored patch passed the family (`1/1` CTest test). The `fff grep` tool was not exposed in this
session, so captured logs were searched with `rg -n`.

## Files committed

- `src/gui/clips/ClipView.cpp`
- `tests/src/gui/ArrangerTimelineTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p11-8.md`
