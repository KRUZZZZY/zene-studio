# Cluster new-p10-400

Verdict: **FIXED**.

`src/gui/editors/StepRecorderWidget.cpp:169` now computes the ruler's right boundary with `m_marginRight`. The regression in `tests/src/gui/NoteEditorTest.cpp:124` renders a 220 px widget with a 10 px top margin and a 30 px right margin, then checks the resulting boundary at line 137. Before the fix it observed 210 instead of 190; removing the production change reproduced the same failure.

The affected family is `NoteEditorTest`: 1 registered CTest target. The red QtTest runs recorded 24 passing cases and the new regression failing; the green CTest runs passed 1/1 target.

## Validation commands

All test commands ran from `build/tests`. Build, test, and gate output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-400.r1/logs`. The fff grep tool was not available in the session; I searched the logs for the required terms with `rg`.

| Command | Exit | Result |
| --- | ---: | --- |
| `cmake --build build --target NoteEditorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-400.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 | Regression built against original code |
| `ctest -R NoteEditorTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-400.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | RED: actual 210, expected 190 |
| `cmake --build build --target NoteEditorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-400.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 | Built after fix |
| `ctest -R NoteEditorTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-400.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 | GREEN: 1/1 CTest target passed |
| `git diff -- src/gui/editors/StepRecorderWidget.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-400.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-400.r1/logs/fix.patch; echo EXIT=$?` | 0 | Removed only the production fix |
| `cmake --build build --target NoteEditorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-400.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 | Rebuilt without fix |
| `ctest -R NoteEditorTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-400.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 | Reversal RED: same 210 vs 190 failure |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-400.r1/logs/fix.patch; echo EXIT=$?` | 0 | Reapplied production fix |
| `cmake --build build --target NoteEditorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-400.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 | Rebuilt with fix restored |
| `ctest -R NoteEditorTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-400.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 | Reversal GREEN: 1/1 CTest target passed |
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-400.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 | Application target rebuilt |
| `ctest -R NoteEditorTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-400.r1/logs/test-5.log 2>&1; echo EXIT=$?` | 0 | Final rerun: 1/1 CTest target passed |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-400.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 | Gate passed |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-400.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 | Gate passed |

## Files committed

- `src/gui/editors/StepRecorderWidget.cpp`
- `tests/src/gui/NoteEditorTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-400.md`
