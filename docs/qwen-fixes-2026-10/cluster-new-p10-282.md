# Cluster new-p10-282: oversampling stage bounds

**Verdict: FIXED**

`Upsampler` and `Downsampler` now reject stage counts outside `[0, MaxStages]` in both `setup()` and `setStages()`. An invalid request leaves the prior stages, sample rate, and passband unchanged. The guard returns before `reset()`, so invalid values cannot index the filter arrays or create a negative shift in later processing.

## File and line evidence

| Location | Evidence |
| --- | --- |
| `include/OversamplingHelpers.h:81-88` | `Upsampler::setup()` checks both bounds before changing state. |
| `include/OversamplingHelpers.h:125-131` | `Upsampler::setStages()` checks both bounds before changing state or resetting. |
| `include/OversamplingHelpers.h:187-194` | `Downsampler::setup()` has the same guard. |
| `include/OversamplingHelpers.h:227-233` | `Downsampler::setStages()` has the same guard. |
| `tests/src/core/DeviceDspTest.cpp:208-284` | Regression cases cover negative and above-maximum values for both classes and entry points, plus valid endpoints. |
| `tests/CMakeLists.txt:978-981` | `DeviceDspTest` links the existing `hiir` interface target so it can include the helper directly. |

## Required evidence

| Evidence | Exact command | Exit / result |
| --- | --- | --- |
| RED, with the product fix reversed | `ctest -R DeviceDspTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs/test-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs/test-5.log; exit "$rc"` (run from `build/tests`) | `EXIT=8`; invalid negative values fail and the above-maximum setter run aborts from the unsafe path. |
| GREEN, after reapplying the fix and rebuilding | `ctest -R DeviceDspTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs/test-6.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs/test-6.log; exit "$rc"` (run from `build/tests`) | `EXIT=0`; `1/1` affected CTest target executed and passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs/gate-1.log; exit "$rc"` | `EXIT=0` |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs/gate-2.log; exit "$rc"` | `EXIT=0` |

The successful affected-target build command, run before the GREEN test and again for the reverse-patch proof, was:

```sh
cmake --build build --target zene DeviceDspTest console slewdistortion partc_ref_slewdistortion > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs/build-7.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs/build-7.log; exit "$rc"
```

`EXIT=0`. The same command with `build-5.log` and `build-6.log` also exited `0`; `build-6.log` is the rebuild with the fix reversed, before RED.

## Reverse-patch proof

The product-only patch was created and reversed with:

```sh
git diff -- include/OversamplingHelpers.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs/fix.patch
```

`EXIT=0`. After RED, it was reapplied with:

```sh
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs/fix.patch
```

`EXIT=0`; the subsequent affected-target rebuild and GREEN run are recorded as `build-7.log` and `test-6.log`.

## Other recorded attempts

The first test-target build, before adding the target's `hiir` dependency, exited `2` because the compiler could not find `hiir/PolyphaseIir2Designer.h`. Its exact command was:

```sh
cmake --build build --target DeviceDspTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs/build-1.log; exit "$rc"
```

After adding the dependency, test builds in `build-2.log`, `build-3.log`, and `build-4.log` exited `0`. The pre-fix regression failed in `test-1.log`, `test-2.log`, and `test-3.log` (each CTest exit `8`); its dedicated upper-bound setter run was:

```sh
ctest -R DeviceDspTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs/test-3.log; exit "$rc"
```

`EXIT=8`. The fixed test passed in `test-4.log` before the required reverse-patch proof. All build, test, and gate logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-282.r1/logs`.

The requested `fff grep` tool was not available in the tool set or on `PATH`; logs were searched with `rg` for the required failure/error terms instead.

## Files committed

- `include/OversamplingHelpers.h`
- `tests/CMakeLists.txt`
- `tests/src/core/DeviceDspTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-282.md`
