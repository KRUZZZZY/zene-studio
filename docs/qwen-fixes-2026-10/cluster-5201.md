ALREADY-FIXED

# Cluster 5201 — ONNX errors can escape the separator

## Decision and evidence

The finding is already fixed on this branch. `OnnxRuntimeStemSeparator::separate()` enters its `try` at `src/core/OnnxRuntimeStemSeparator.cpp:182`. It calls `initializeSession()` at `:213-214`; that helper constructs `Ort::Session` at `:124`. The segment loop calls `runSessionSegment()` at `:266-267`, where `session.Run()` is at `:141`. Both exception sites are inside the same `try`. The `Ort::Exception` handler at `:313-317` stores `what()` in `error` and returns `Status::Failed`; the `std::exception` handler at `:318-322` does the same. The ONNX Runtime header also declares `Ort::Exception` as deriving from `std::exception` (`onnxruntime_cxx_api.h:54`).

The existing deterministic regression `testPostRunOrtExceptionIsReturnedAsFailure()` in `tests/src/core/OnnxRuntimeStemSeparatorTest.cpp:111-132` injects an `Ort::Exception` through `m_postRunTestHook`, checks that it does not escape `separate()`, and verifies `Status::Failed` plus the reported error. The hook is called in post-run validation at `src/core/OnnxRuntimeStemSeparator.cpp:108`. `initializeSession()` and `runSessionSegment()` each have one caller, both inside `separate()`.

This behavior was added before this task in ancestor commit `3db2618ef5915e056316a50a17076be07e74aab9`, titled `fix(stems): catch ONNX errors across separation (cluster 5201)`. The existing test and production catch are already in `HEAD`; no product or test changes were needed for this cluster.

## Verification

| Check | Command/result |
| --- | --- |
| RED/reverted-fix run | N/A: this branch already contains the fix, so per procedure step 2 I skipped the regression-writing/reversion steps. No RED result is claimed. |
| Build | EXIT=0 |
| Existing ONNX test | EXIT=0; 1/1 test passed. |
| Feature gates | EXIT=0; all eight checks PASS, including complexity and file length. |
| Completion check | To run after committing this report. |
| Files committed for cluster 5201 | `docs/qwen-fixes-2026-10/cluster-5201.md` only. |

Commands run (exact shell commands):

```sh
mkdir -p /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs && cmake --build build --target zene OnnxRuntimeStemSeparatorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/build-1.log 2>&1; build_exit=$?; echo EXIT=$build_exit | tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/build-1.log; exit "$build_exit"
ctest -R '^OnnxRuntimeStemSeparatorTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/test-1.log 2>&1; test_exit=$?; echo EXIT=$test_exit | tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/test-1.log; exit "$test_exit"
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5201 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/feature-check-1.log 2>&1; gate_exit=$?; echo EXIT=$gate_exit | tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/feature-check-1.log; exit "$gate_exit"
```

## Self-check

- **Reverted-fix regression:** No product patch was made in this task. The regression was not reversed because the fix predates this branch task in commit `3db2618ef`; the current existing test passes (1/1). This follows the procedure's ALREADY-FIXED path rather than claiming an unobserved RED result.
- **Determinism:** The existing test injects `Ort::Exception` through a direct function-pointer hook. It uses no timing, sleep, or race.
- **Cause and coverage:** The session constructor and `session.Run()` are called through helpers inside `separate()`'s exception boundary. Both helpers have one caller there. `Ort::Exception` is explicitly caught, assigned to `error`, and converted to `Status::Failed`.
- **Collateral behavior:** No production or test code changed; this task does not alter audio-thread behavior, saved data, or other separator callers.
- **ALREADY-FIXED evidence:** `src/core/OnnxRuntimeStemSeparator.cpp:182, 213-214, 266-267, 313-322`; `tests/src/core/OnnxRuntimeStemSeparatorTest.cpp:111-132`; ancestor commit `3db2618ef5915e056316a50a17076be07e74aab9`.
