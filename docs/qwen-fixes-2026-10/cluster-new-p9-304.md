# Cluster new-p9-304

Verdict: **FIXED**

## Finding and change

The overlapping-note fix checks every exact note rectangle, but it skipped `NoteCanvas::noteAt()`'s expanded target for narrow notes. `strokeAlong()` now marks all notes whose actual rectangles contain a sample and uses `noteAt()` only when no actual rectangle contains that sample. This retains overlap handling and the existing short-note target behavior. The fallback in `noteAt()` is at `src/gui/noteeditor/NoteCanvas.cpp:207-216`; the stroke logic is at `src/gui/noteeditor/NoteCanvasGestures.cpp:92-103`.

The erase regression at `tests/src/gui/NoteEditorTest.cpp:192-212` drags into a one-tick note's expanded target from outside it. Its path stays outside the note's actual rectangle. It fails before the product change and when the product patch is reversed, then passes after restoration.

## Evidence

Builds ran from the worktree root. Every ctest command ran from `build/tests`. Build, ctest, and gate output was saved under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r2/logs`.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| Initial RED build | `cmake --build build --target NoteEditorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r2/logs/build-1.log 2>&1` | 0 |
| RED test before this product change | `ctest -R NoteEditorTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r2/logs/test-1.log 2>&1` | 8 |
| Fixed build | `cmake --build build --target zene NoteEditorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r2/logs/build-2.log 2>&1` | 0 |
| Reverse product patch | `git diff -- src/gui/noteeditor/NoteCanvasGestures.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r2/logs/fix.patch` | 0 |
| Reversed build | `cmake --build build --target zene NoteEditorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r2/logs/build-3.log 2>&1` | 0 |
| RED test with product patch reversed | `ctest -R NoteEditorTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r2/logs/test-2.log 2>&1` | 8 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r2/logs/fix.patch` | 0 |
| Restored build | `cmake --build build --target zene NoteEditorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r2/logs/build-4.log 2>&1` | 0 |
| GREEN test | `ctest -R NoteEditorTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r2/logs/test-3.log 2>&1` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r2/logs/gate-1.log 2>&1` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r2/logs/gate-2.log 2>&1` | 0 |

The affected ctest family ran **1/1** registered test (`NoteEditorTest`). The initial and reversed RED runs each report 26 passed and 1 failed Qt test case; the restored run passed. Both required gates passed.

## Files committed

- `src/gui/noteeditor/NoteCanvasGestures.cpp`
- `tests/src/gui/NoteEditorTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-304.md`
