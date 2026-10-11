# Cluster new-p9-354: oversampling stage bounds

Verdict: **FIXED**.

The finding is real. For `MaxStages = 4`, a stage count of 5 makes each `reset()` loop write a third remaining filter into a two-element array. Negative values also leave an invalid stage count for later processing. Both samplers now reject values outside `[0, MaxStages]` before changing configuration or resetting.

## Code and regression evidence

- `include/OversamplingHelpers.h:73-76` and `:179-182` are the remaining-filter writes in `Upsampler::reset()` and `Downsampler::reset()`.
- `include/OversamplingHelpers.h:81-84` and `:187-191` guard both `setup()` methods before state changes.
- `include/OversamplingHelpers.h:125-130` and `:227-232` guard both `setStages()` methods before state changes and `reset()`.
- `tests/src/core/AudioResamplerRatioTest.cpp:356`, `:370`, `:384`, and `:406` cover upper and negative invalid values for both entry points and both sampler classes, checking that the previous settings remain intact.
- The regression failed on the restored original helper at `AudioResamplerRatioTest::upsamplerRejectsOutOfRangeStageChanges`: actual stage count 5, expected 2. The same RED result was reproduced by the explicit reverse/apply proof below.

## Verification

The affected CTest family contains one registered test, `AudioResamplerRatioTest`. The GREEN run executed 1/1 tests; that executable includes the four new regression slots.

| Run | Exact command | Exit |
|---|---|---:|
| Initial regression-target build (before adding the hiir interface dependency) | `cmake --build build --target AudioResamplerRatioTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-354.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 2 |
| Reverse product patch | `git diff -- include/OversamplingHelpers.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-354.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-354.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit "$result"` | 0 |
| RED rebuild with original helper restored | `cmake --build build --target zene AudioResamplerRatioTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-354.r1/logs/build-5.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 0 |
| RED CTest | `ctest -R AudioResamplerRatioTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-354.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 8 |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-354.r1/logs/fix.patch; result=$?; echo EXIT=$result; exit "$result"` | 0 |
| GREEN rebuild after restoring the fix | `cmake --build build --target zene AudioResamplerRatioTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-354.r1/logs/build-6.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 0 |
| GREEN CTest | `ctest -R AudioResamplerRatioTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-354.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-354.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-354.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result; exit "$result"` | 0 |

The product-only reverse/apply patch is saved at `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-354.r1/logs/fix.patch`. The `fff grep` tool was unavailable in this session, so I searched the logs with `rg`, one term per search (`error`, `FAILED`, and `Assertion`).

The first test compile attempt failed because this test target did not inherit hiir's include path; `tests/CMakeLists.txt:979-982` now links the existing `hiir` interface target for this test. Subsequent recorded builds passed.

## Files committed

- `include/OversamplingHelpers.h`
- `tests/CMakeLists.txt`
- `tests/src/core/AudioResamplerRatioTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-354.md`
