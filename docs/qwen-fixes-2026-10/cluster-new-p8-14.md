# Cluster new-p8-14

**Verdict: FIXED**

`chain.copy` accepts a list of destinations. `copyToEach` stopped after the first incomplete copy and built `to` from only the destinations processed so far. The array form now returns the validated input list from `args.to`; `copies` continues to report actual progress. A single destination still returns a string.

## Evidence

| File and lines | Evidence |
| --- | --- |
| `src/core/ControlCommandsDeviceCopy.cpp:144-145` | Array-form `to` now uses the original destination list. |
| `tests/src/core/DeviceCopyTest.cpp:147-177,548-569` | An unregistered test effect makes `chain.copy` stop short deterministically; the regression checks three requested destinations and one processed `copies` entry. |

Both changed C++ files were already listed in `tests/fork-sources.txt`; no upstream-modifications entry was needed.

## RED/GREEN and gates

The formal RED is the build with the product patch reversed. The test reported actual `to` as `['ch-42']` and expected `['ch-42','ch-43','ch-44']`. The RED suite had 17 QtTest passes and this one failure. After the patch was restored, the same suite had 18 passes, no failures, and no skips. One CTest case (`DeviceCopyTest`) ran in each suite.

| Check | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Reverse patch | `git diff -- src/core/ControlCommandsDeviceCopy.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-14.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-14.r1/logs/fix.patch` | 0 | Product patch removed for RED proof. |
| RED build | `cmake --build build --target zene DeviceCopyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-14.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 | Rebuilt `zene` and `DeviceCopyTest`. |
| RED test (run from `build/tests`) | `ctest -R DeviceCopyTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-14.r1/logs/test-5.log 2>&1; echo EXIT=$?` | 8 | Regression failed at the expected `to` comparison; 17 passed, 1 failed, 0 skipped. |
| Restore patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-14.r1/logs/fix.patch` | 0 | Product fix restored. |
| GREEN build | `cmake --build build --target zene DeviceCopyTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-14.r1/logs/build-6.log 2>&1; echo EXIT=$?` | 0 | Rebuilt `zene` and `DeviceCopyTest`. |
| GREEN test (run from `build/tests`) | `ctest -R DeviceCopyTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-14.r1/logs/test-6.log 2>&1; echo EXIT=$?` | 0 | 1/1 CTest case passed. |
| GREEN test count (run from `build/tests`) | `ctest -R DeviceCopyTest --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-14.r1/logs/test-7.log 2>&1; echo EXIT=$?` | 0 | 18 QtTest passes, 0 failures, 0 skips; 1/1 CTest case passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-14.r1/logs/gate-3.log 2>&1; echo EXIT=$?` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-14.r1/logs/gate-4.log 2>&1; echo EXIT=$?` | 0 | Passed. |

The test family needs built effect modules. This command completed with exit 0 before the formal RED/GREEN pair:

```sh
cmake --build build --target zene DeviceCopyTest amplifier gate modulation > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-14.r1/logs/build-4.log 2>&1; echo EXIT=$?
```

Earlier fixture iterations were not used as RED evidence: `test-1.log` exited 8 because the first fixture required an effect module that had not been built; `test-2.log` exited 8 because the initial test effect had no controls for serialization; `test-3.log` exited 8 with the expected `to` mismatch plus the same missing-module failure. The final test uses a self-contained unregistered effect, and the required modules were built before the formal pair.

## Files committed

- `src/core/ControlCommandsDeviceCopy.cpp`
- `tests/src/core/DeviceCopyTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-14.md`
