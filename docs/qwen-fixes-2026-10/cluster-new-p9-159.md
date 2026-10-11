# Cluster new-p9-159

**Verdict: FIXED**

`Graph::m_lastCursorX` was read before any click initialized it. The constructor now sets it to `-1` as the no-prior-position sentinel. A first Shift-click (and a first mouse move) edits only the current sample; after that, the saved X is used for line drawing. The class layout is unchanged.

## File evidence

- [`include/Graph.h:119`](../../include/Graph.h:119) declares `m_lastCursorX`.
- [`src/gui/widgets/Graph.cpp:46`](../../src/gui/widgets/Graph.cpp:46) initializes it to `-1`; line 134 handles a first mouse move, and line 184 starts the first-Shift-click decision.
- [`tests/src/gui/SampleEditorDialogTest.cpp:156`](../../tests/src/gui/SampleEditorDialogTest.cpp:156) verifies that the first Shift-click changes only its clicked sample.
- [`tests/upstream-modifications.txt:1054`](../../tests/upstream-modifications.txt:1054) records the inherited production-file change.

## Test evidence

The suggested `MultitapEchoControlDialog`, `WaveShaperControls`, and `DynamicsProcessorControls` CTest filters were not registered in this configured tree. This discovery command returned `Total Tests: 0`; it did not run tests:

`ctest -N -R '^(MultitapEchoControlDialog|WaveShaperControls|DynamicsProcessorControls)$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-159.r1/logs/discovery-1.log 2>&1; echo EXIT=$?` — EXIT=0.

The regression was added to the existing `SampleEditorDialogTest` target instead.

Affected family count: **1 registered CTest test executed** per run. Before the fix, QtTest reported 8 passing slots and the new slot failing with `Compared floats are not the same (fuzzy compare)`. After the fix, CTest reported 1/1 passed.

| Evidence | Exact command | Exit |
| --- | --- | ---: |
| Pre-fix build | `cmake --build build --target zene SampleEditorDialogTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-159.r1/logs/build-red-1.log 2>&1; echo EXIT=$?` | 0 |
| RED before fix | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-159.r1/logs/test-red-1.log 2>&1; echo EXIT=$?` | 8 |
| Reverse-patch build | `cmake --build build --target zene SampleEditorDialogTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-159.r1/logs/build-red-proof-final.log 2>&1; echo EXIT=$?` | 0 |
| RED with production patch reversed | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-159.r1/logs/test-red-proof-final.log 2>&1; echo EXIT=$?` | 8 |
| Reapplied-patch build | `cmake --build build --target zene SampleEditorDialogTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-159.r1/logs/build-green-proof-final.log 2>&1; echo EXIT=$?` | 0 |
| GREEN after reapplying fix | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-159.r1/logs/test-green-proof-final.log 2>&1; echo EXIT=$?` | 0 |
| Final dependent build | `cmake --build build --target zene SampleEditorDialogTest bitinvader dynamicsprocessor freeboy multitapecho vibedstrings watsyn waveshaper xpressive -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-159.r1/logs/build-dependent-final.log 2>&1; echo EXIT=$?` | 0 |
| Final test run | `ctest -R '^SampleEditorDialogTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-159.r1/logs/test-dependent-final.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-159.r1/logs/gate-complexity-final.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-159.r1/logs/gate-file-length-final.log 2>&1; echo EXIT=$?` | 0 |

The final proof used `git diff -- src/gui/widgets/Graph.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-159.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-159.r1/logs/fix.patch; echo EXIT=$?` (EXIT=0), rebuilt with the reverse-patch build command in the table (EXIT=0), and observed the RED result above. It then used `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-159.r1/logs/fix.patch; echo EXIT=$?` (EXIT=0), rebuilt with the reapplied-patch build command in the table (EXIT=0), and observed the GREEN result above.

An unrestricted build attempt was rejected by the runner's named-target guard: `cmake --build build -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-159.r1/logs/build-green-1.log 2>&1; echo EXIT=$?` — EXIT=97. All subsequent builds used named targets. Both gates exited 0. The complexity log lists 68 functions above CCN 10 across the fork scope, and the file-length log lists five sources above 500 lines; neither gate failed.

## Files committed

- `src/gui/widgets/Graph.cpp`
- `tests/src/gui/SampleEditorDialogTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-159.md`
