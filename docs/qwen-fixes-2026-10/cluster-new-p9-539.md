# Cluster new-p9-539: invalid accessibility values

**Verdict: FIXED.** `ValueControl::setCurrentValue()` in `src/gui/AccessibleWidgets.cpp:165-172` now checks the success flag from `QVariant::toFloat()` before mutating the model. The regression in `tests/src/gui/CanvasKeyboardTest.cpp:221-235` verifies that a nonnumeric value leaves the model unchanged and a valid value still applies.

The regression failed on the original implementation at `CanvasKeyboardTest.cpp:233`: actual model value `0`, expected `50`. Reversing only the production patch reproduced that failure.

## Evidence

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Original build | `cmake --build build --target zene CanvasKeyboardTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-539.r1/logs/build-1.log 2>&1; code=$?; echo EXIT=$code` | 0 | `zene` and regression target rebuilt before red run |
| RED | `ctest -R '^CanvasKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-539.r1/logs/test-1.log 2>&1; code=$?; echo EXIT=$code` | 8 | 1 CTest test; QtTest totals: 11 passed, 1 failed |
| Initial GREEN build | `cmake --build build --target zene CanvasKeyboardTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-539.r1/logs/build-2.log 2>&1; code=$?; echo EXIT=$code` | 0 | Rebuilt after the fix |
| Initial GREEN | `ctest -R '^CanvasKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-539.r1/logs/test-2.log 2>&1; code=$?; echo EXIT=$code` | 0 | Regression passed |
| Proof: reverse production patch | `git diff -- src/gui/AccessibleWidgets.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-539.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-539.r1/logs/fix.patch; code=$?; echo EXIT=$code` | 0 | Removed only the product fix |
| Proof rebuild without fix | `cmake --build build --target zene CanvasKeyboardTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-539.r1/logs/build-3.log 2>&1; code=$?; echo EXIT=$code` | 0 | Rebuilt original behavior |
| Proof RED | `ctest -R '^CanvasKeyboardTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-539.r1/logs/test-3.log 2>&1; code=$?; echo EXIT=$code` | 8 | Same assertion failed: actual `0`, expected `50` |
| Proof: restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-539.r1/logs/fix.patch; code=$?; echo EXIT=$code` | 0 | Restored fix |
| Final GREEN build | `cmake --build build --target zene CanvasKeyboardTest ItemListPanelTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-539.r1/logs/build-4.log 2>&1; code=$?; echo EXIT=$code` | 0 | Rebuilt product and both affected GUI targets |
| Final GREEN | `ctest -R '^(CanvasKeyboardTest|ItemListPanelTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-539.r1/logs/test-4.log 2>&1; code=$?; echo EXIT=$code` | 0 | Affected-family count: 2 CTest tests, 2 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-539.r1/logs/gate-1.log 2>&1; code=$?; echo EXIT=$code` | 0 | Passed |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-539.r1/logs/gate-2.log 2>&1; code=$?; echo EXIT=$code` | 0 | Passed |

The `fff` grep tool was not available in this session; build and test logs were checked with `rg` for errors and failures. The changed product source is already registered in `tests/fork-sources.txt` as a fork-new file. No new source files were added.

## Files committed

- `src/gui/AccessibleWidgets.cpp`
- `tests/src/gui/CanvasKeyboardTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-539.md`
