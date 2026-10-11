# Cluster new-p9-304

Verdict: **FIXED**

## Finding and change

`NoteCanvas::noteAt()` in `src/gui/noteeditor/NoteCanvas.cpp:198-217` scans from the last visible note to the first and returns on its first rectangle hit. `NoteCanvas::strokeAlong()` previously passed each sampled point to that single-note lookup, so coincident note rectangles hid all but the topmost note.

`src/gui/noteeditor/NoteCanvasGestures.cpp:80-98` now checks every visible note rectangle at each existing 2-pixel path sample, in reverse draw order. This marks every overlapped note while retaining which note sets the mute direction first. Regression slots in `tests/src/gui/NoteEditorTest.cpp:192-210` and `:425-448` drive erase and mute drags across coincident notes.

## Evidence

All ctest commands below ran from `build/tests`; build, test, and gate output was redirected to the named new log file under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r1/logs`. The `EXIT` value is the command status printed by the exact `echo EXIT=$?` wrapper.

| Stage | Exact command | Recorded exit |
| --- | --- | ---: |
| RED build | `cmake --build build --target NoteEditorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| RED test on original code | `ctest -R '^NoteEditorTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r1/logs/test-red-1.log 2>&1; echo EXIT=$?` | 8 |
| GREEN build | `cmake --build build --target zene NoteEditorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| GREEN test | `ctest -R '^NoteEditorTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r1/logs/test-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Reverse production patch | `git diff -- src/gui/noteeditor/NoteCanvasGestures.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Reversal build | `cmake --build build --target zene NoteEditorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| Reversal RED test | `ctest -R '^NoteEditorTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r1/logs/test-proof-red-1.log 2>&1; echo EXIT=$?` | 8 |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Restored build | `cmake --build build --target zene NoteEditorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| Reversal GREEN test | `ctest -R '^NoteEditorTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r1/logs/test-proof-green-1.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-304.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

The affected ctest family ran **1/1** registered test (`NoteEditorTest`). Both new regression slots failed on the original code and after reversing the production patch; the final restored run passed. The original-code RED log records 24 passed and 2 failed Qt test cases. Both required gates passed.

## Files committed

- `src/gui/noteeditor/NoteCanvasGestures.cpp`
- `tests/src/gui/NoteEditorTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-304.md`
