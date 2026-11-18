# Cluster new-p9-382

Verdict: **FIXED**

The old accumulator started at zero, so positive latency samples could never affect its minimum. `LatencyMinimum::observe()` now initializes from the first sample, and `value()` returns `-1` if the polling loop observed nothing. The LV2 polling loop uses this accumulator. A deterministic regression feeds 12 and 8 frames and expects 8.

## File evidence

- `tests/src/core/RealtimeSnapshotTest.cpp:134-142`: first-sample initialization and the no-observation sentinel.
- `tests/src/core/RealtimeSnapshotTest.cpp:311-317`: deterministic positive-sample regression.
- `tests/src/core/RealtimeSnapshotTest.cpp:339-343`: LV2 polling uses the tested accumulator and checks for zero.

## Build and test evidence

The affected CTest family contains one registered test, `RealtimeSnapshotTest` (CTest test #195). The RED run executed that test and showed 3 Qt cases passed, the new regression failed, and 6 skipped. The LV2 integration case skipped because this build has no installed LV2 effect; the deterministic regression itself ran.

Commands below were run from the worktree root unless marked `build/tests`.

| Phase | Exact command | Exit | Result |
|---|---|---:|---|
| RED build | `cmake --build build --target RealtimeSnapshotTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-382.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result` | 0 | Target built before the fix. |
| RED test (`build/tests`) | `ctest -R '^RealtimeSnapshotTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-382.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result` | 8 | Regression reported actual 0, expected 8. |
| GREEN build | `cmake --build build --target RealtimeSnapshotTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-382.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result` | 0 | Rebuilt after the fix. |
| GREEN test (`build/tests`) | `ctest -R '^RealtimeSnapshotTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-382.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result` | 0 | 1/1 CTest tests passed. |

## Inverse proof

The regression was kept in place while reversing only the accumulator change. It failed again, then passed after reapplying the patch.

| Exact command | Exit |
|---|---:|
| `git diff -- tests/src/core/RealtimeSnapshotTest.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-382.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-382.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| `cmake --build build --target RealtimeSnapshotTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-382.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| `ctest -R '^RealtimeSnapshotTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-382.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result` (`build/tests`) | 8 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-382.r1/logs/fix.patch; result=$?; echo EXIT=$result` | 0 |
| `cmake --build build --target RealtimeSnapshotTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-382.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| `ctest -R '^RealtimeSnapshotTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-382.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result` (`build/tests`) | 0 |

## Gates

| Exact command | Exit |
|---|---:|
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-382.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-382.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result` | 0 |

## Files committed

- `tests/src/core/RealtimeSnapshotTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-382.md`
