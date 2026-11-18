# Cluster new-p9-18: comp strip keyboard focus

**Verdict: ALREADY-FIXED.** `CompSwipeStrip` calls `a11y::describeCanvas` in its constructor at `src/gui/CompSwipeStrip.cpp:93`. That helper sets `Qt::StrongFocus` whenever the widget lacks `Qt::TabFocus` at `src/gui/AccessibleCanvas.cpp:76`. `Qt::StrongFocus` enables tab and click focus. The shared helper's contract also states that a canvas is keyboard reachable at `include/AccessibleCanvas.h:44`.

The test now checks tab focus and verifies that clicking the strip gives it focus at `tests/src/gui/CanvasKeyboardTest.cpp:293-298`. It passed before any production change. There was no RED run or fix patch to reverse because the reported behavior was already handled by the shared helper.

| Evidence | Command / result |
| --- | --- |
| RED | Not applicable: the branch already grants the strip focus; no failing assertion was observed. |
| Build | `cmake --build build --target zene CanvasKeyboardTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-18.r1/logs/build-1.log' 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-18.r1/logs/build-1.log'; exit "$result"` — EXIT=0. |
| Branch verification | From `build/tests`: `ctest -R '^CanvasKeyboardTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-18.r1/logs/test-1.log' 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-18.r1/logs/test-1.log'; exit "$result"` — EXIT=0, 1/1 CTest tests passed. |
| Fix rollback replay | Not applicable: no product fix was made. |
| Complexity gate | `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-18.r1/logs/gate-5.log' 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-18.r1/logs/gate-5.log'; exit "$result"` — EXIT=0. |
| File length gate | `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-18.r1/logs/gate-6.log' 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-18.r1/logs/gate-6.log'; exit "$result"` — EXIT=0. |

Files committed:

- `tests/src/gui/CanvasKeyboardTest.cpp` — checks tab eligibility and click focus on the comp strip.
- `docs/qwen-fixes-2026-10/cluster-new-p9-18.md` — this finding report.
