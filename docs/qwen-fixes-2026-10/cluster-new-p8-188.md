# Cluster new-p8-188

**Verdict: FIXED**

`src/gui/PinConnector.cpp:512` and `:515` accepted `relPos == cellSize() - s_lineThickness`, the separator pixel after a cell. Both hit-test bounds now reject that coordinate with `>=`. The regression at `tests/src/gui/AccessibilityWalkTest.cpp:262` sends mouse-press events to both real matrix widgets, at vertical and horizontal separator pixels, and verifies that the input and output pins stay unchanged.

The existing GUI test target was used so the test exercises `PinConnector::MatrixView::mousePressEvent` under the real `GuiApplication`; no CMake reconfiguration was needed. Each CTest run executed one test (`AccessibilityWalkTest`, test #241). RED runs reported `3 passed, 1 failed` inside QtTest; GREEN runs passed the one CTest target.

## Evidence

| Check | Exact command | Result |
|---|---|---|
| RED on original code, after rebuilding `zene` | `ctest -R AccessibilityWalkTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=8`; separator click toggled input pin `(0, 0)` |
| GREEN with fix | `ctest -R AccessibilityWalkTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0`; `1/1` passed |
| Reverse-patch RED | `ctest -R AccessibilityWalkTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=8`; same assertion failed without production fix |
| Restored-patch GREEN | `ctest -R AccessibilityWalkTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0`; `1/1` passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/gate-4.log 2>&1; rc=$?; echo EXIT=$rc` | `EXIT=0` |

## Build and proof commands

All build and test output went to new files in the requested log folder.

| Exact command | Exit |
|---|---:|
| `cmake --build build --target AccessibilityWalkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 2 (initial test compile attempt used the private nested `MatrixView` type; the test was corrected to identify the widgets by meta-object name) |
| `cmake --build build --target AccessibilityWalkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `ctest -R AccessibilityWalkTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 (original-code RED, repeated after rebuilding `zene`) |
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `cmake --build build --target AccessibilityWalkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `git diff -- src/gui/PinConnector.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| `cmake --build build --target zene AccessibilityWalkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| `cmake --build build --target zene AccessibilityWalkTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/build-7.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-188.r1/logs/gate-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

The `fff grep` tool was unavailable in this session; build/test logs were searched with `rg` for `error`, `FAILED`, `Assertion`, `FAIL!` and test totals.

## Files committed

- `src/gui/PinConnector.cpp`
- `tests/src/gui/AccessibilityWalkTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-188.md`
