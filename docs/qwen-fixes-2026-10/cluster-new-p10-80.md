# Cluster new-p10-80: NaN analysis bounds

Verdict: **FIXED**

`src/core/ControlDetectSupport.cpp:157-169` now rejects a non-finite `max_seconds` with `std::isfinite` before applying the existing range check. Finite values keep the same range and result. The regression at `tests/src/core/ImportDetectionTest.cpp:296-309` constructs a numeric NaN `QJsonValue`, verifies it remains NaN, and checks for an `InvalidArgs` refusal from `readDetectBound`.

The original implementation failed this regression: `readDetectBound` returned success for NaN. Reversing only the production fix reproduced the same failure; reapplying it passed.

CTest commands below ran from `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-80/build/tests`; build and gate commands ran from the worktree root.

| Evidence | Exact command(s) and exit code(s) | Result |
| --- | --- | --- |
| Original-code RED | `cmake --build build --target zene ImportDetectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-80.r1/logs/build-1.log 2>&1; status_code=$?; echo EXIT=$status_code; exit $status_code` — **0**<br>`ctest -R ImportDetectionTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-80.r1/logs/test-1.log 2>&1; status_code=$?; echo EXIT=$status_code; exit $status_code` — **8** | Regression failed as intended; QtTest reported 7 passed, 1 failed. |
| Fixed-code GREEN | `cmake --build build --target zene ImportDetectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-80.r1/logs/build-2.log 2>&1; status_code=$?; echo EXIT=$status_code; exit $status_code` — **0**<br>`ctest -R ImportDetectionTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-80.r1/logs/test-2.log 2>&1; status_code=$?; echo EXIT=$status_code; exit $status_code` — **0** | ImportDetectionTest passed, 1/1 CTest tests. |
| Reverse-patch proof | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-80.r1/logs/fix.patch; status_code=$?; echo EXIT=$status_code; exit $status_code` — **0**<br>`cmake --build build --target zene ImportDetectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-80.r1/logs/build-3.log 2>&1; status_code=$?; echo EXIT=$status_code; exit $status_code` — **0**<br>`ctest -R ImportDetectionTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-80.r1/logs/test-3.log 2>&1; status_code=$?; echo EXIT=$status_code; exit $status_code` — **8** | Removing the production fix made the same regression fail again: 7 passed, 1 failed. |
| Reapplied-fix proof | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-80.r1/logs/fix.patch; status_code=$?; echo EXIT=$status_code; exit $status_code` — **0**<br>`cmake --build build --target zene ImportDetectionTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-80.r1/logs/build-4.log 2>&1; status_code=$?; echo EXIT=$status_code; exit $status_code` — **0**<br>`ctest -R ImportDetectionTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-80.r1/logs/test-4.log 2>&1; status_code=$?; echo EXIT=$status_code; exit $status_code` — **0** | ImportDetectionTest passed, 1/1 CTest tests. |
| Affected-family count | `ctest -R ImportDetectionTest --output-on-failure` | 1 registered CTest test; passed in both green runs. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-80.r1/logs/gate-1.log 2>&1; status_code=$?; echo EXIT=$status_code; exit $status_code` — **0** | PASS. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-80.r1/logs/gate-2.log 2>&1; status_code=$?; echo EXIT=$status_code; exit $status_code` — **0** | PASS. |

Files committed:

- `src/core/ControlDetectSupport.cpp`
- `tests/src/core/ImportDetectionTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-80.md`

The configured `fff grep` tool was unavailable in this session, so build, test, and gate logs were searched with `rg` for `error`, `FAILED`, and `Assertion`.
