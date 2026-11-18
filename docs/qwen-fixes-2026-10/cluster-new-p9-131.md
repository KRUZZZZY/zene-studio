# Cluster new-p9-131 — FIXED

The finding is real. `Editor::togglePlayStop()` and `Editor::togglePause()` assign `s_lastPlayedEditor` at [Editor.cpp:73](../../src/gui/editors/Editor.cpp) and [Editor.cpp:82](../../src/gui/editors/Editor.cpp); [Editor.h:54](../../include/Editor.h) returned that raw pointer. The destructor was defaulted. It now clears the pointer when it refers to the editor being destroyed ([Editor.cpp:86-91](../../src/gui/editors/Editor.cpp)); destruction of an older editor leaves a newer last-played editor intact.

The regression is [lastPlayedEditorClearsWhenDestroyed](../../tests/src/gui/SampleEditorDialogTest.cpp:142). It failed on the original code and failed again after the product patch was removed, then passed after the patch was reapplied.

## Evidence

CTest was run from `build/tests`. The affected family contains one registered CTest item, `SampleEditorDialogTest`. The red Qt run reported 8 passed and 1 failed; the green CTest runs passed the item.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| Initial RED build | `cmake --build build --target SampleEditorDialogTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-131.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Initial RED test | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-131.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| Initial GREEN build | `cmake --build build --target SampleEditorDialogTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-131.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Initial GREEN test | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-131.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Rebuild editor-header consumers and zene | `cmake --build build --target SampleEditorDialogTest VisibilitySetMenuTest PianoRollBenchmarkTest TrackFolderMenuTest ArrangerTimelineTest AccessibilityWalkTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-131.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| GREEN test after dependent rebuild | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-131.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Remove product patch | `git diff -- include/Editor.h src/gui/editors/Editor.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-131.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-131.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| Rebuild without fix | `cmake --build build --target SampleEditorDialogTest VisibilitySetMenuTest PianoRollBenchmarkTest TrackFolderMenuTest ArrangerTimelineTest AccessibilityWalkTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-131.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Negative-control RED test | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-131.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-131.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit $result` | 0 |
| Rebuild with fix restored | `cmake --build build --target SampleEditorDialogTest VisibilitySetMenuTest PianoRollBenchmarkTest TrackFolderMenuTest ArrangerTimelineTest AccessibilityWalkTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-131.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Negative-control GREEN test | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-131.r1/logs/test-5.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-131.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-131.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |

The build and test logs were searched for `error`, `FAILED` and `Assertion`. No `fff` grep tool was available in this session, so the searches used `rg` against the specified log files without printing whole logs.

## Files in the commit

- `include/Editor.h`
- `src/gui/editors/Editor.cpp`
- `tests/src/gui/SampleEditorDialogTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-131.md`
