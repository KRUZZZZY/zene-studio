# Cluster new-p9-160

Verdict: **FIXED**

## Evidence

`Graph::drawLineAt` now returns for `height() <= 5` at [Graph.cpp:190](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-160/src/gui/widgets/Graph.cpp:190), and `Graph::changeSampleAt` applies the same guard at [Graph.cpp:256](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-160/src/gui/widgets/Graph.cpp:256). Both checks run before the `height() - 5` division. The click and line-draw regression cases are in [SampleEditorDialogTest.cpp:133](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-160/tests/src/gui/SampleEditorDialogTest.cpp:133) and [SampleEditorDialogTest.cpp:146](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-160/tests/src/gui/SampleEditorDialogTest.cpp:146).

The finding is real: before the fix, both event paths stored a non-finite sample at five pixels high. The GREEN run passed. Removing only the Graph.cpp fix made both regression cases fail again; restoring it made the family pass. The affected ctest family executed **1 CTest target**; the two new QTest slots both fail in the RED runs and pass in the GREEN runs.

The three suggested ctest names are not registered in this configured tree (`ctest -N` found zero matching tests), so the regression uses the existing offscreen `SampleEditorDialogTest` target. No configure step was run.

## Commands and results

All build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-160.r1/logs`.

| Check | Exact command | Exit code and observed result |
|---|---|---|
| Candidate test discovery | `ctest -N -R '^(MultitapEchoControlDialog|WaveShaperControls|DynamicsProcessorControls)$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-160.r1/logs/discover-1.log 2>&1; rc=$?; echo EXIT=$rc` | `0`; `Total Tests: 0`. |
| Initial build | `cmake --build build --target zene SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-160.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | `0`. |
| Initial regression attempt | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-160.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | `8`; click case failed; the first line-draw stimulus did not engage mouse tracking, so it was corrected before the final RED run. |
| Final RED build | `cmake --build build --target zene SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-160.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | `0`. |
| RED test | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-160.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | `8`; both new cases failed; QTest reported `8 passed, 2 failed`. |
| GREEN build | `cmake --build build --target zene SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-160.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | `0`. |
| GREEN test | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-160.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | `0`; `100% tests passed, 0 tests failed out of 1`. |
| Save product patch | `git diff -- src/gui/widgets/Graph.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-160.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | `0`. |
| Remove product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-160.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | `0`. |
| Reversal build | `cmake --build build --target zene SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-160.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | `0`. |
| Reversal RED test | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-160.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | `8`; both regression cases failed again; QTest reported `8 passed, 2 failed`. |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-160.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | `0`. |
| Restored build | `cmake --build build --target zene SampleEditorDialogTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-160.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` | `0`. |
| Restored GREEN test | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-160.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc` | `0`; `100% tests passed, 0 tests failed out of 1`. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-160.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | `0`. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-160.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | `0`. |

## Files committed

- `src/gui/widgets/Graph.cpp`
- `tests/src/gui/SampleEditorDialogTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-160.md`
