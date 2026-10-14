# Cluster new-p10-269: non-positive quantize grid

Verdict: **FIXED**.

`Note::quantized()` now returns the original time for `qGrid <= 0`, before dividing or converting to `int`. `quantizePos()` and `quantizeLength()` also return early, so invalid grids do not call setters or let the zero-length fallback create a negative length. Positive-grid quantization is unchanged.

Evidence:

- [src/core/Note.cpp:301](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-269/src/core/Note.cpp:301): shared helper guard; the mutator guards are at lines 318 and 334.
- [tests/src/core/NoteTransformTest.cpp:363](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-269/tests/src/core/NoteTransformTest.cpp:363): regression covers zero and negative grids on the helper and mutators.
- The original implementation failed the regression with actual `0`, expected `123`. Removing the production patch reproduced that failure; reapplying it passed.

## Evidence table

| Evidence | Exact command / result |
|---|---|
| RED on original code | `ctest -R '^NoteTransformTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/test-1.log; exit $rc` — EXIT=8; 13 passed, regression failed (actual 0, expected 123). |
| GREEN with fix | `ctest -R '^NoteTransformTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/test-2.log; exit $rc` — EXIT=0; 1/1 CTest test passed. |
| Affected family | `ctest -R '^(NoteTransformTest|NoteEditRangeTest|NoteTransformActionsTest|SlideNotesTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/test-5.log; exit $rc` — EXIT=0; 4/4 CTest tests passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/gate-1.log; exit $rc` — EXIT=0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/gate-2.log; exit $rc` — EXIT=0. |
| Files committed | `src/core/Note.cpp`; `tests/src/core/NoteTransformTest.cpp`; `tests/upstream-modifications.txt`; `docs/qwen-fixes-2026-10/cluster-new-p10-269.md`. |

## Evidence commands

All build and test output was saved in new numbered files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs`. The `fff grep` tool was not available in this session; I searched the logs with `rg` for `error`, `FAILED`, and `Assertion`.

Build commands and exit codes:

```sh
cmake --build build --target NoteTransformTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/build-1.log; exit $rc
# EXIT=0
cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/build-2.log; exit $rc
# EXIT=0
cmake --build build --target zene NoteTransformTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/build-3.log; exit $rc
# EXIT=0
cmake --build build --target zene NoteTransformTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/build-4.log; exit $rc
# EXIT=0
cmake --build build --target zene NoteTransformTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/build-5.log; exit $rc
# EXIT=0
cmake --build build --target zene NoteTransformTest NoteEditRangeTest NoteTransformActionsTest SlideNotesTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/build-6.log; exit $rc
# EXIT=0
```

RED/GREEN and sensitivity commands:

```sh
ctest -R '^NoteTransformTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/test-1.log; exit $rc
# RED on original implementation: EXIT=8; 13 QtTest cases passed and the new assertion failed (actual 0, expected 123).
ctest -R '^NoteTransformTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/test-2.log; exit $rc
# GREEN with fix: EXIT=0; 1/1 CTest test passed.
git diff -- src/core/Note.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/fix.patch
# EXIT=0
ctest -R '^NoteTransformTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/test-3.log; exit $rc
# RED with production patch removed: EXIT=8; the same assertion failed (actual 0, expected 123).
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/fix.patch
# EXIT=0
ctest -R '^NoteTransformTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/test-4.log; exit $rc
# GREEN after reapplying fix: EXIT=0; 1/1 CTest test passed.
ctest -R '^(NoteTransformTest|NoteEditRangeTest|NoteTransformActionsTest|SlideNotesTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/test-5.log; exit $rc
# EXIT=0; affected family count 4/4 CTest tests passed.
```

Ratchet gates:

```sh
bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/gate-1.log; exit $rc
# EXIT=0
bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-269.r1/logs/gate-2.log; exit $rc
# EXIT=0
```

Files in this commit:

- `src/core/Note.cpp`
- `tests/src/core/NoteTransformTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-269.md`
