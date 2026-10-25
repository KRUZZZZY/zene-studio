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

new-p23-75: FIXED

Evidence — REAL: `close()` hides this `WA_DeleteOnClose` dialog and queues deletion, so it can remain in `topLevelWidgets()` until deferred deletion runs. `openFor()` now reuses a matching editor only if it is visible ([CrossfadeEditorDialog.cpp:204](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-18/src/gui/CrossfadeEditorDialog.cpp:204)). The regression closes and reopens synchronously, confirms the old `QPointer` is still live, and requires a distinct visible editor ([CrossfadeEditorTest.cpp:182](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-18/tests/src/gui/CrossfadeEditorTest.cpp:182)).

Commands and results:
- `cmake --build build --target zene CrossfadeEditorTest` — EXIT 0 (logs 013, 015, 018, 021).
- `./CrossfadeEditorTest reopeningBeforeDeferredDeleteCreatesNewEditor` from `build/tests` on original code — EXIT 1; reopened pointer equaled the closed pending-delete dialog (log 014).
- Same test with fix — EXIT 0 (log 016).
- `git diff -- src/gui/CrossfadeEditorDialog.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-18.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-18.r1/logs/fix.patch` — EXIT 0 (log 017); rebuild with p23 fix reversed — EXIT 0 (log 018); same test with p23 fix reversed — EXIT 1 (log 019).
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p18-18.r1/logs/fix.patch` — EXIT 0 (log 020); rebuild — EXIT 0 (log 021); same test with fix restored — EXIT 0 (log 022).
- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p18-18` — EXIT 1 (log 023): same pre-existing `all-sources.txt` recipe mismatch; other listed checks passed.
- `ctest -R '^CrossfadeEditorTest$' --output-on-failure` from `build/tests` — EXIT 0; 1/1 CTest family tests passed (log 024).
