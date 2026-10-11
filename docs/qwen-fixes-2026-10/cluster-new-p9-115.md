# Cluster new-p9-115

**Verdict: FIXED**

`sp_dcblock_create` in [plugins/ReverbSC/dcblock.c:18](../../plugins/ReverbSC/dcblock.c#L18) now returns `SP_NOT_OK` when `malloc` returns null and retains `SP_OK` on success. The deterministic regression test substitutes a failing allocator for this test target only and checks both the status and null output pointer in [ReverbScAllocationTest.cpp:44](../../tests/src/plugins/ReverbScAllocationTest.cpp#L44). CTest registration is in [tests/CMakeLists.txt:1156](../../tests/CMakeLists.txt#L1156).

## Regression evidence

The affected family is `ReverbScAllocationTest`; each CTest invocation matched and executed **1 test**.

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| Original code, RED (`test-1.log`) | `QT_QPA_PLATFORM=offscreen ctest -R ReverbScAllocationTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/test-1.log 2>&1` (run from `build/tests`) | 8 | Failed as required: actual status `1`, expected `SP_NOT_OK` (`0`). |
| Fixed code, GREEN (`test-2.log`) | `QT_QPA_PLATFORM=offscreen ctest -R ReverbScAllocationTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/test-2.log 2>&1` (run from `build/tests`) | 0 | 1/1 test passed. |
| Negative control: reverse only the production patch | `git diff -- plugins/ReverbSC/dcblock.c > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/fix.patch` | 0 | Reversed successfully. |
| Rebuilt without the fix (`build-5.log`) | `cmake --build build --target zene reverbsc ReverbScAllocationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/build-5.log 2>&1` | 0 | Build completed. |
| Negative control, RED (`test-3.log`) | `QT_QPA_PLATFORM=offscreen ctest -R ReverbScAllocationTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/test-3.log 2>&1` (run from `build/tests`) | 8 | The same regression failed again. |
| Restore the production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/fix.patch` | 0 | Restored successfully. |
| Rebuilt with the fix (`build-6.log`) | `cmake --build build --target zene reverbsc ReverbScAllocationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/build-6.log 2>&1` | 0 | Build completed. |
| Restored code, GREEN (`test-4.log`) | `QT_QPA_PLATFORM=offscreen ctest -R ReverbScAllocationTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/test-4.log 2>&1` (run from `build/tests`) | 0 | 1/1 test passed. |
| Final build after test source formatting (`build-7.log`) | `cmake --build build --target zene reverbsc ReverbScAllocationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/build-7.log 2>&1` | 0 | Build completed. |
| Final CTest (`test-5.log`) | `QT_QPA_PLATFORM=offscreen ctest -R ReverbScAllocationTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/test-5.log 2>&1` (run from `build/tests`) | 0 | 1/1 test passed. |

Two initial build attempts exposed test setup issues and were corrected before relying on the RED/GREEN results:

| Exact command | Exit | Result |
|---|---:|---|
| `cmake --build build --target zene ReverbScAllocationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/build-1.log 2>&1` (`build-1.log`) | 2 | CMake regenerated the target list during the first Make invocation; that invocation still reported no rule for the new target. |
| `cmake --build build --target zene ReverbScAllocationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/build-2.log 2>&1` (`build-2.log`) | 2 | Test source declarations lacked C linkage for the C implementation; corrected before the successful pre-fix build. |
| `cmake --build build --target zene ReverbScAllocationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/build-3.log 2>&1` (`build-3.log`) | 0 | `zene` and the regression target built against the original production code. |
| `cmake --build build --target zene reverbsc ReverbScAllocationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/build-4.log 2>&1` (`build-4.log`) | 0 | `zene`, the affected plug-in, and the regression target built after the fix. |

## Gates

| Exact command | Exit | Result |
|---|---:|---|
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/gate-1.log 2>&1` | 0 | PASS. |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/gate-2.log 2>&1` | 0 | PASS. |
| `bash tests/fork-sources-gate.sh > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-115.r1/logs/manifest-2.log 2>&1` | 0 | PASS; both source manifests reproduce and all tracked sources are registered. |

## Files in the commit

- `plugins/ReverbSC/dcblock.c`
- `tests/src/plugins/ReverbScAllocationTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-115.md`
