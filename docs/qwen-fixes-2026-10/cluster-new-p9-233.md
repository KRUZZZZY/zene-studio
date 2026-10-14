# Cluster new-p9-233: loudness histogram overflow

## Verdict: FIXED

The finding is real. The previous single overflow bucket used its mean energy as the representative loudness for every over-range block. That merged distinct levels for the integrated relative gate and short-term LRA percentiles. The histogram now keeps fixed 0.25 LU bands above its existing 0.05 LU range, through +805 LUFS. Both integrated and short-term scans use the bands' center levels; accumulated energy and counts still produce the integrated mean. The bounded histograms require no per-block storage or audio-thread allocation.

Evidence locations: [LufsMeter.h:196-203](../../include/LufsMeter.h), [LufsMeter.cpp:290-338](../../src/core/LufsMeter.cpp), and [LufsMeter.cpp:385-435](../../src/core/LufsMeter.cpp). The original uniform +15 dBFS 5.1 regression remains at [LufsMeterTest.cpp:290](../../tests/src/core/LufsMeterTest.cpp); new tests at lines 313 and 327 cover mixed +5.1/+25.1 dBFS stereo passages for integrated gating and LRA.

The initial regression run measured 22.1396 LUFS instead of 25.1 LUFS and LRA 0 LU. After reversing the product patch and rebuilding, both assertions failed again. With the patch restored and rebuilt, both pass.

## Evidence

Exit values below are the captured operation codes printed by each command's `echo EXIT=$rc`.

| Check | Exact command | EXIT | Result |
|---|---|---:|---|
| Build regression (`build-1.log`) | `cmake --build build --target LufsMeterTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r2/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Built the test with the new assertions against the round-1 product code. |
| RED (`test-1.log`) | `ctest -R '^LufsMeterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r2/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | Mixed gate read 22.1396 vs 25.1 LUFS; hot-level LRA read 0 LU. |
| Save product patch | `git diff -- include/LufsMeter.h src/core/LufsMeter.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r2/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 | Saved only the product changes for the required reversal proof. |
| Reverse product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r2/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 | Removed the product change while retaining the regression tests. |
| Rebuild with product patch reversed (`build-4.log`) | `cmake --build build --target zene LufsMeterTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r2/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt `zene` and the regression target. |
| RED with product patch reversed (`test-3.log`) | `ctest -R '^LufsMeterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r2/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | Both mixed-level regressions failed again. |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r2/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 | Restored the fix. |
| Final affected-target build (`build-5.log`) | `cmake --build build --target zene LufsMeterTest LoudnessReportTest MeterTapTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r2/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt `zene` and all affected test targets. |
| GREEN (`test-4.log`) | `ctest -R '^LufsMeterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r2/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | `LufsMeterTest` passed after restoring the fix. |
| Affected family (`test-5.log`) | <code>ctest -R '^(LufsMeterTest&#124;LoudnessReportTest&#124;MeterTapTest)$' --output-on-failure &gt; /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r2/logs/test-5.log 2&gt;&amp;1; rc=$?; echo EXIT=$rc</code> | 0 | 3/3 CTest targets passed. |
| Complexity gate (`gate-7.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r2/logs/gate-7.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed. |
| File-length gate (`gate-8.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r2/logs/gate-8.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed. |

## Files in the commit

- `include/LufsMeter.h`
- `src/core/LufsMeter.cpp`
- `tests/src/core/LufsMeterTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-233.md`
