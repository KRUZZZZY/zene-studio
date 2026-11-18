# Cluster new-p10-310

**Verdict: FIXED**

The reported integer division was a false explanation: `ClipView::pixelsPerBar()` is a `float` (`include/ClipView.h:180`). The real issue was that `PatternClipView` converted the clip offset to integer pixels before combining it with each note's position. At 4 px/bar and 192 ticks/bar, a tick-36 note and tick-36 offset each map to 0.75 px; combining 72 ticks first yields pixel 1, while truncating the offset first yields pixel 0.

`PatternClipView.cpp:119-125` now keeps a wrapped tick offset and carries the existing sub-2-pixel pattern-cycle adjustment in ticks as well as pixels. At `:182-195`, each note position is combined with that offset and rasterized once; the existing repeated-pattern pixel step remains in the loop. The offscreen regression at `tests/src/gui/ArrangerTimelineTest.cpp:211-264` uses a real `PatternClipView` at 4 px/bar. Its left border covers local x=1, so it checks the same repeated note at x=5 and asserts `x - 4 == 1`. Original code reports 0; fixed code reports 1.

## Evidence

| Check | Exact command and result |
|---|---|
| Initial standalone-target attempt (setup only) | `cmake --build build --target zene PatternClipViewTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-310.r2/logs/build-1.log 2>&1; echo EXIT=$?` — **EXIT=2** because the configured build had no generated `PatternClipViewTest` target. The regression was added to the existing `ArrangerTimelineTest` target; no configure was run. |
| RED build | `cmake --build build --target zene ArrangerTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-310.r2/logs/build-6.log 2>&1; echo EXIT=$?` — **EXIT=0**. |
| RED | From `build/tests`: `ctest -R ArrangerTimelineTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-310.r2/logs/test-5.log 2>&1; echo EXIT=$?` — **EXIT=8**. CTest ran 1 target; QtTest reported 12 passed and the regression failed with `notePixelX - 4 == 0` (expected 1). |
| GREEN build | `cmake --build build --target zene ArrangerTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-310.r2/logs/build-7.log 2>&1; echo EXIT=$?` — **EXIT=0**. |
| GREEN | From `build/tests`: `ctest -R ArrangerTimelineTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-310.r2/logs/test-6.log 2>&1; echo EXIT=$?` — **EXIT=0**, 1/1 CTest targets passed. |
| Reverse only production patch | `git diff -- src/gui/clips/PatternClipView.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-310.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-310.r2/logs/fix.patch; echo EXIT=$?` — **EXIT=0**. |
| Rollback build | `cmake --build build --target zene ArrangerTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-310.r2/logs/build-8.log 2>&1; echo EXIT=$?` — **EXIT=0**. |
| Rollback RED | From `build/tests`: `ctest -R ArrangerTimelineTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-310.r2/logs/test-7.log 2>&1; echo EXIT=$?` — **EXIT=8**, regression again reported `notePixelX - 4 == 0`. |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-310.r2/logs/fix.patch; echo EXIT=$?` — **EXIT=0**. |
| Final build | `cmake --build build --target zene ArrangerTimelineTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-310.r2/logs/build-9.log 2>&1; echo EXIT=$?` — **EXIT=0**. |
| Final GREEN | From `build/tests`: `ctest -R ArrangerTimelineTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-310.r2/logs/test-8.log 2>&1; echo EXIT=$?` — **EXIT=0**, 1/1 CTest targets passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-310.r2/logs/gate-1.log 2>&1; echo EXIT=$?` — **EXIT=0**. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-310.r2/logs/gate-2.log 2>&1; echo EXIT=$?` — **EXIT=0**. |

Files committed:

- `src/gui/clips/PatternClipView.cpp`
- `tests/src/gui/ArrangerTimelineTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-310.md`
