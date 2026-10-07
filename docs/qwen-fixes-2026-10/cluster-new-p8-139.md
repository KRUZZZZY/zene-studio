# Cluster new-p8-139

Verdict: **FIXED**

The finding is real. `CompSource::remove()` in [ItemListSurfaceSources.cpp](../../src/gui/ItemListSurfaceSources.cpp#L167) used only `rows.first()`, so Delete changed one comp segment even when the table selected several. The fix snapshots every selected segment's original range and its original neighbour lane before invoking any `comp.select` command. Later edits therefore keep their original ranges when earlier edits merge rows.

The regression in [TakeLanesPanelTest.cpp](../../tests/src/gui/TakeLanesPanelTest.cpp#L147) selects rows 0 and 2, presses Delete, and checks that both original ranges end up on lane 1. The first edit merges with the middle row, so processing row 2 by its now-stale index would miss the final range. The product source is already registered in `tests/fork-sources.txt:1668`; the inherited test change is recorded in `tests/upstream-modifications.txt:1054`.

## Evidence

| Phase | Exact test command | Exit | Result |
|---|---|---:|---|
| Original implementation, RED | `cd build/tests && ctest -R '^TakeLanesPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/test-1.log; exit "$rc"` | 8 | 1 CTest test ran; failed at `segments.size()`, actual 2, expected 1. |
| Fixed implementation, GREEN | `cd build/tests && ctest -R '^TakeLanesPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/test-2.log; exit "$rc"` | 0 | 1 CTest test ran and passed. |
| Production patch reversed, RED | `cd build/tests && ctest -R '^TakeLanesPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/test-3.log; exit "$rc"` | 8 | Same assertion failed: actual 2, expected 1. |
| Patch restored, GREEN | `cd build/tests && ctest -R '^TakeLanesPanelTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/test-4.log; exit "$rc"` | 0 | 1 CTest test ran and passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/gate-1.log; exit "$rc"` | 0 | Passed. |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/gate-2.log; exit "$rc"` | 0 | Passed. |

Each test run was preceded by a successful build of `zene` and `TakeLanesPanelTest`:

| Build phase | Exact command | Exit |
|---|---|---:|
| Original RED | `cmake --build build --target zene TakeLanesPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/build-1.log; exit "$rc"` | 0 |
| Fix GREEN | `cmake --build build --target zene TakeLanesPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/build-2.log; exit "$rc"` | 0 |
| Patch reversed | `cmake --build build --target zene TakeLanesPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/build-3.log; exit "$rc"` | 0 |
| Patch restored | `cmake --build build --target zene TakeLanesPanelTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/build-4.log; exit "$rc"` | 0 |

The reversal proof used these commands (both exit 0):

```sh
git diff -- src/gui/ItemListSurfaceSources.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/prove-1.log; exit "$rc"
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-139.r1/logs/prove-2.log; exit "$rc"
```

## Files

- `src/gui/ItemListSurfaceSources.cpp` — snapshots and processes all selected comp segments.
- `tests/src/gui/TakeLanesPanelTest.cpp` — regression for multi-row Delete across a merge.
- `tests/upstream-modifications.txt` — reason for the inherited test change.
- `docs/qwen-fixes-2026-10/cluster-new-p8-139.md` — this report.
