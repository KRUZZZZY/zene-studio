ALREADY-FIXED

# Cluster 5201 — ONNX errors can escape the separator

## Decision and evidence

The finding is already fixed on this branch. `OnnxRuntimeStemSeparator::separate()` enters its `try` at `src/core/OnnxRuntimeStemSeparator.cpp:182`. It calls `initializeSession()` at `:213-214`; that helper constructs `Ort::Session` at `:124`. The segment loop calls `runSessionSegment()` at `:266-267`, where `session.Run()` is at `:141`. Both exception sites are inside the same `try`. The `Ort::Exception` handler at `:313-317` stores `what()` in `error` and returns `Status::Failed`; the `std::exception` handler at `:318-322` does the same. The ONNX Runtime header also declares `Ort::Exception` as deriving from `std::exception` (`onnxruntime_cxx_api.h:54`).

The suite already had `testPostRunOrtExceptionIsReturnedAsFailure()` to inject an `Ort::Exception` after `Run`. I added `testMalformedModelExceptionIsReturnedBySeparator()` at `tests/src/core/OnnxRuntimeStemSeparatorTest.cpp:90-115`: it gives `separate()` a present but corrupt temporary model and verifies the exception is converted to `Status::Failed` with an error. The new test is synchronous and has no timing, sleeps, or races. `initializeSession()` and `runSessionSegment()` each have one caller, both inside `separate()`.

This behavior was added before this task in ancestor commit `3db2618ef5915e056316a50a17076be07e74aab9`, titled `fix(stems): catch ONNX errors across separation (cluster 5201)`. The production catch is already in `HEAD`; this task adds only the direct session-construction regression test and this report.

## Verification

| Check | Command/result |
| --- | --- |
| RED regression | With the exception handlers temporarily removed, `testMalformedModelExceptionIsReturnedBySeparator()` fails its `!exceptionEscaped` assertion. Build EXIT=0; CTest EXIT=8, 1/1 test failed. |
| GREEN regression | After `git apply -R` restored the existing catch, build EXIT=0 and CTest EXIT=0, 1/1 test passed. |
| Feature gates | `feature-check-5.log` EXIT=0; all eight checks PASS, including complexity and file length. |
| Completion check | Initial run before adding the direct test failed only `tests added or changed` (EXIT=1). After the test commit, `submit-check-2.log` passed (EXIT=0); final clean-tree rerun follows the report update. |
| Files for this task | `tests/src/core/OnnxRuntimeStemSeparatorTest.cpp`; `docs/qwen-fixes-2026-10/cluster-5201.md`. |

Commands run (exact shell commands; CTest commands ran from `build/tests`):

```sh
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5201 5201 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/submit-check-1.log 2>&1; submit_exit=$?; echo EXIT=$submit_exit | tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/submit-check-1.log; exit "$submit_exit"
git diff -- src/core/OnnxRuntimeStemSeparator.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/fix.patch && cmake --build build --target zene OnnxRuntimeStemSeparatorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/build-red.log 2>&1; build_exit=$?; echo EXIT=$build_exit | tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/build-red.log; exit "$build_exit"
ctest -R '^OnnxRuntimeStemSeparatorTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/test-red.log 2>&1; test_exit=$?; echo EXIT=$test_exit | tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/test-red.log; exit "$test_exit"
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/fix.patch && git diff --exit-code -- src/core/OnnxRuntimeStemSeparator.cpp
cmake --build build --target zene OnnxRuntimeStemSeparatorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/build-green.log 2>&1; build_exit=$?; echo EXIT=$build_exit | tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/build-green.log; exit "$build_exit"
ctest -R '^OnnxRuntimeStemSeparatorTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/test-green.log 2>&1; test_exit=$?; echo EXIT=$test_exit | tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/test-green.log; exit "$test_exit"
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5201 5201 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/submit-check-2.log 2>&1; submit_exit=$?; echo EXIT=$submit_exit | tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/submit-check-2.log; exit "$submit_exit"
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-5201 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/feature-check-5.log 2>&1; gate_exit=$?; echo EXIT=$gate_exit | tee -a /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5201.r1/logs/feature-check-5.log; exit "$gate_exit"
```

The observed exits for the listed commands are: submit-check-1 `1`; red build/test `0`/`8`; restore `0`; green build/test `0`/`0`; submit-check-2 `0`; final feature-check `0`. The first completion-check failure was resolved by adding the direct regression test.

## Self-check

- **Reverted-fix regression:** The product fix predates this task. To prove the new test detects the reported failure, I temporarily removed the existing exception boundary, built, and ran the test family: `build-red.log` EXIT=0; `test-red.log` EXIT=8 with the new test failing because the exception escaped. I restored the exact source with `git apply -R`; `build-green.log` EXIT=0 and `test-green.log` EXIT=0 (1/1 passed). The final source has no production diff.
- **Determinism:** The new test synchronously writes a fixed corrupt model to `QTemporaryFile` and calls `separate()` directly. It uses no timing, sleep, or race. The red log shows the direct `!exceptionEscaped` assertion failed before the pre-existing asynchronous job test ran.
- **Cause and coverage:** The session constructor and `session.Run()` are called through helpers inside `separate()`'s exception boundary. Both helpers have one caller there. `Ort::Exception` is explicitly caught, assigned to `error`, and converted to `Status::Failed`.
- **Collateral behavior:** No production code changed; the only code change is a regression test. Audio-thread behavior, saved data, and other separator callers are unchanged.
- **ALREADY-FIXED evidence:** `src/core/OnnxRuntimeStemSeparator.cpp:182, 213-214, 266-267, 313-322`; `tests/src/core/OnnxRuntimeStemSeparatorTest.cpp:90-115, 138-159`; ancestor commit `3db2618ef5915e056316a50a17076be07e74aab9`.
