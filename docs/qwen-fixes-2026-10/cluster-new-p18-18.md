new-p18-18: FIXED

Evidence — REAL: `openFor()` used to call `reload()` for the matching dialog, replacing its pending controls with saved clip edits; it now raises the existing dialog without reloading ([CrossfadeEditorDialog.cpp:204](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-18/src/gui/CrossfadeEditorDialog.cpp:204)). The test opens the pair in reverse order, stages 12 ticks / Exponential, and checks those values survive reopening ([CrossfadeEditorTest.cpp:166](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-18/tests/src/gui/CrossfadeEditorTest.cpp:166)).

Commands and results:
- `cmake --build build --target zene CrossfadeEditorTest` — EXIT 0 (logs 002, 004, 007, 010).
- `./CrossfadeEditorTest reopeningAnExistingEditorPreservesPendingControls` from `build/tests` on original code — EXIT 1; actual 48, expected 12 (log 003).
- Same test with fix — EXIT 0 (log 005).
- `git diff -- src/gui/CrossfadeEditorDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-18.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-18.r1/logs/fix.patch` — EXIT 0 (log 006).
- Rebuild with p18 fix reversed — EXIT 0 (log 007); same test with p18 fix reversed — EXIT 1, actual 48 / expected 12 (log 008).
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-18.r1/logs/fix.patch` — EXIT 0 (log 009); rebuild — EXIT 0 (log 010); same test with fix restored — EXIT 0 (log 011).
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-18` — EXIT 1 (log 012). Complexity, file-length, namespace, no-tautology, docs figures, and upstream-regression passed. `fork-sources` and `all-sources-reproduce` failed because the existing `tests/all-sources.txt` contains paths absent from its own generation recipe (including vendor Abseil paths); no source-manifest baseline was changed.
