# Cluster new-p9-233: loudness histogram overflow

## Verdict: FIXED

The finding is real. [LufsMeter.cpp:295] routes every LUFS value at or above the histogram ceiling into an overflow bucket. The bucket uses its accumulated mean energy for gate comparisons ([LufsMeter.cpp:300]), instead of the old +4.975 LUFS bin center. Storage remains fixed-size. The same overflow handling is used by the short-term histogram used for LRA.

The regression at [LufsMeterTest.cpp:290] feeds a 5.1 tone at +15 dBFS on all six channels. The five measured channels should read +19.6389 LUFS. Before the fix and with the saved production patch removed, the result was `-inf`; with the fix restored it passes.

## Evidence

| Check | Exact invocation | Exit | Result |
|---|---|---:|---|
| RED before fix (`test-1.log`) | `ctest -R '^LufsMeterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | 1 test executed; regression measured `-inf`, expected `19.6389 +/- 0.1`. |
| RED with fix removed (`test-4.log`) | `ctest -R '^LufsMeterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | After rebuilding, the same regression failed with the same result. |
| GREEN after fix restored (`test-5.log`) | `ctest -R '^LufsMeterTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | 1/1 passed. |
| Affected family (`test-7.log`) | `ctest -R '^(LufsMeterTest|LoudnessReportTest|MeterTapTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r1/logs/test-7.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | 3/3 passed. |
| Complexity gate (`gate-1.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed. |
| File-length gate (`gate-2.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed. |

The affected targets were rebuilt successfully. `cmake --build build --target LufsMeterTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` exited 0 before the fix. `cmake --build build --target zene LufsMeterTest LoudnessReportTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` exited 0 with the fix, and the same command logged to `build-4.log` and `build-5.log` exited 0 with the fix removed and restored. `cmake --build build --target MeterTapTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r1/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc` exited 0 after the final fix. The unqualified `cmake --build build > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` exited 97 because the workspace guard requires named targets; its log says full builds are the parent’s job.

The proof patch commands also exited 0:

```sh
git diff -- include/LufsMeter.h src/core/LufsMeter.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r1/logs/fix.patch; rc=$?; echo EXIT=$rc  # 0
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r1/logs/fix.patch; rc=$?; echo EXIT=$rc  # 0
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r1/logs/fix.patch; rc=$?; echo EXIT=$rc  # 0
```

The build, test, and gate commands redirected output to new numbered logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-233.r1/logs`; each command printed the exit code recorded above.

## Files in the commit

- `include/LufsMeter.h`
- `src/core/LufsMeter.cpp`
- `tests/src/core/LufsMeterTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-233.md`
