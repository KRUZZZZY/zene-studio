# Cluster new-p9-352: Patman partial patch loading

Verdict: **FIXED**

`PatmanInstrument::loadPatch()` now accumulates samples in a temporary `QVector` and swaps it into the instrument only after the whole file parses successfully. An error while reading a later sample leaves the prior active patch intact. The production change is at [Patman.cpp](../../plugins/Patman/Patman.cpp#L221), with the successful replacement at [Patman.cpp](../../plugins/Patman/Patman.cpp#L368).

The regression at [ControlDeviceCatalogueTest.cpp](../../tests/src/core/ControlDeviceCatalogueTest.cpp#L139) loads a valid two-sample patch, then tries a patch with a valid first sample and a truncated second. It renders before and after and checks that the active sound remains the same at line 166.

## Evidence

| Check | Exact command | Result |
| --- | --- | --- |
| RED, with the product fix reverted | `ctest -R ControlDeviceCatalogueTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-352.r1/logs/test-3.log 2>&1; echo EXIT=$?` from `build/tests` | `EXIT=8`; the new sound comparison failed because the truncated patch replaced the previous sample. |
| GREEN, with the product fix restored | `ctest -R ControlDeviceCatalogueTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-352.r1/logs/test-4.log 2>&1; echo EXIT=$?` from `build/tests` | `EXIT=0`; 1/1 CTest entry passed. |
| Verbose affected-family count | `ctest -R ControlDeviceCatalogueTest --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-352.r1/logs/test-5.log 2>&1; echo EXIT=$?` from `build/tests` | `EXIT=0`; QtTest reported 9 passed, 0 failed, 2 skipped. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-352.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | `EXIT=0`. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-352.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | `EXIT=0`. |

The revert proof used these exact commands and results:

```text
git diff -- plugins/Patman/Patman.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-352.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-352.r1/logs/fix.patch; echo EXIT=$?
EXIT=0

cmake --build build --target zene patman ControlDeviceCatalogueTest --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-352.r1/logs/build-3.log 2>&1; echo EXIT=$?
EXIT=0

ctest -R ControlDeviceCatalogueTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-352.r1/logs/test-3.log 2>&1; echo EXIT=$?
EXIT=8

git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-352.r1/logs/fix.patch; echo EXIT=$?
EXIT=0

cmake --build build --target zene patman ControlDeviceCatalogueTest --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-352.r1/logs/build-4.log 2>&1; echo EXIT=$?
EXIT=0

ctest -R ControlDeviceCatalogueTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-352.r1/logs/test-4.log 2>&1; echo EXIT=$?
EXIT=0
```

Both red and green build commands targeted `zene`, the `patman` module, and `ControlDeviceCatalogueTest`. The fff grep tool was unavailable in this session; I searched log files with individual `rg` terms and did not read logs wholesale.

## Files in the commit

- `plugins/Patman/Patman.cpp`
- `tests/src/core/ControlDeviceCatalogueTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-352.md`
