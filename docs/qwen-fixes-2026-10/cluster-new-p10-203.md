# Cluster new-p10-203

Verdict: **FIXED**

`CompSource::insertAfter` in `src/gui/ItemListSurfaceSources.cpp:161` now refuses to split a segment narrower than two ticks, because it has no interior tick boundary. For valid spans, the midpoint is calculated relative to the start. The refusal leaves the composite unchanged and is shown in the item list status line.

The regression in `tests/src/gui/ItemListPanelTest.cpp:189` seeds a one tick segment `[10, 11)`, creates the comp item list, and invokes its Insert action. At line 205 it requires refusal; the following checks require the segment bounds and lane to remain unchanged.

## Verification evidence

The affected CTest family is `ItemListPanelTest`. The final green run executed **1/1 CTest test** (test #232); it includes the new QtTest regression slot.

| Stage | Exact command | Exit |
| --- | --- | ---: |
| Build regression against original production code | `cmake --build build --target ItemListPanelTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-203.r1/logs/build-1.log' 2>&1; echo EXIT=$?` | 0 |
| Original-code RED | From `build/tests`: `ctest -R ItemListPanelTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-203.r1/logs/test-1.log' 2>&1; echo EXIT=$?` | 8 |
| Build fixed `zene` and test target | `cmake --build build --target zene ItemListPanelTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-203.r1/logs/build-2.log' 2>&1; echo EXIT=$?` | 0 |
| GREEN | From `build/tests`: `ctest -R ItemListPanelTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-203.r1/logs/test-2.log' 2>&1; echo EXIT=$?` | 0 |
| Save product-only patch for the required reversal proof | `git diff -- src/gui/ItemListSurfaceSources.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-203.r1/logs/fix.patch'; echo EXIT=$?` | 0 |
| Reverse production fix | `git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-203.r1/logs/fix.patch'; echo EXIT=$?` | 0 |
| Rebuild without fix | `cmake --build build --target zene ItemListPanelTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-203.r1/logs/build-3.log' 2>&1; echo EXIT=$?` | 0 |
| Reversal RED | From `build/tests`: `ctest -R ItemListPanelTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-203.r1/logs/test-3.log' 2>&1; echo EXIT=$?` | 8 |
| Restore production fix | `git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-203.r1/logs/fix.patch'; echo EXIT=$?` | 0 |
| Rebuild restored fix | `cmake --build build --target zene ItemListPanelTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-203.r1/logs/build-4.log' 2>&1; echo EXIT=$?` | 0 |
| Final GREEN | From `build/tests`: `ctest -R ItemListPanelTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-203.r1/logs/test-4.log' 2>&1; echo EXIT=$?` | 0 |
| Complexity ratchet | `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-203.r1/logs/gate-1.log' 2>&1; echo EXIT=$?` | 0 |
| File length ratchet | `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-203.r1/logs/gate-2.log' 2>&1; echo EXIT=$?` | 0 |
| Whitespace check | `git diff --check; echo EXIT=$?` | 0 |

Both RED runs fail at `ItemListPanelTest.cpp:205`: QtTest reports `!panel.insertItem()` returned FALSE, which means Insert incorrectly returned true on the original code. The final restored-fix run passes.

## Files committed

- `src/gui/ItemListSurfaceSources.cpp`
- `tests/src/gui/ItemListPanelTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-203.md`
