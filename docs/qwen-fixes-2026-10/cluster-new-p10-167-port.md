# Cluster new-p10-167-port: Graph shift-click coordinate clamp

Verdict: **FIXED**

Port of `bugs/qwen-new-p10-167` commit `92734eac2` onto the newest line. `src/gui/widgets/Graph.cpp` shift-click path now clamps `x`/`y` to the same drawable bounds as mouse movement before calling `drawLineAt()`, preventing invalid sample ranges at either widget edge. The regression is in `tests/src/gui/ExportQualityControlsTest.cpp::shiftClicksStayWithinGraphSamples`.

Evidence: before the fix, the left-edge click emitted `samplesChanged(-16, 55)`; after the fix it emitted `(0, 55)`. `graphModel::drawSampleAt()` crops individual writes, so this proof checks the invalid range passed to the model and does not rely on an out-of-bounds crash.

| Evidence | Result |
|---|---|
| Original-code RED | `ctest -R '^ExportQualityControlsTest$' --output-on-failure` — EXIT=8; actual start `-16`, expected `0` (logs: `test-1.log`) |
| Fixed GREEN | `ctest -R '^ExportQualityControlsTest$' --output-on-failure` — EXIT=0; 1/1 CTest test passed (logs: `test-2.log`) |
| Reverse-patch proof | Reverse application + rebuild succeeded; same CTest command failed with the same `-16` assertion (EXIT=8, `test-3.log`). Reapplying the patch, rebuilding, and rerunning passed (EXIT=0, `test-4.log`). |
| Affected-family count | 1 CTest target (`ExportQualityControlsTest`), all slots passed on the final run |
| Complexity gate | `bash tests/complexity-gate.sh --check` — EXIT=1 (6 pre-existing regressions, identical to the clean tree; this change adds none) |
| File-length gate | `bash tests/file-length-gate.sh --check` — EXIT=1 (14 pre-existing regressions, identical to the clean tree; this change adds none) |
| Files committed | `src/gui/widgets/Graph.cpp`; `tests/src/gui/ExportQualityControlsTest.cpp`; `tests/upstream-modifications.txt`; `docs/qwen-fixes-2026-10/cluster-new-p10-167-port.md` |

The supplied `ctest -R` names for the three reference plugin files are not registered CTest targets in this configured tree, so the regression was added to the existing GUI QtTest executable without changing CMake configuration.

## Command record

Build and test output was redirected to new files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-167-port.r1/logs`.

1. RED on original implementation:

   `cmake --build build --target ExportQualityControlsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-167-port.r1/logs/build-1.log 2>&1` — EXIT=0

   `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^ExportQualityControlsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-167-port.r1/logs/test-1.log 2>&1` — EXIT=8

2. First fixed GREEN:

   `cmake --build build --target zene ExportQualityControlsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-167-port.r1/logs/build-2.log 2>&1` — EXIT=0

   `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^ExportQualityControlsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-167-port.r1/logs/test-2.log 2>&1` — EXIT=0

3. Reverse-patch proof:

   `git diff -- src/gui/widgets/Graph.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-167-port.r1/logs/fix.patch` — EXIT=0

   `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-167-port.r1/logs/fix.patch` — EXIT=0

   `cmake --build build --target zene ExportQualityControlsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-167-port.r1/logs/build-3.log 2>&1` — EXIT=0

   `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^ExportQualityControlsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-167-port.r1/logs/test-3.log 2>&1` — EXIT=8

   `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-167-port.r1/logs/fix.patch` — EXIT=0

4. Restored GREEN:

   `cmake --build build --target zene ExportQualityControlsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-167-port.r1/logs/build-4.log 2>&1` — EXIT=0

   `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^ExportQualityControlsTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-167-port.r1/logs/test-4.log 2>&1` — EXIT=0

5. Required gates:

   `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-167-port.r1/logs/gate-5.log 2>&1` — EXIT=1

   `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-167-port.r1/logs/gate-6.log 2>&1` — EXIT=1

The gate failures are pre-existing baseline drift on this branch (6 complexity, 14 file-length regressions identical to the clean tree); this change adds none. The inherited-source reason is recorded in `tests/upstream-modifications.txt`.
