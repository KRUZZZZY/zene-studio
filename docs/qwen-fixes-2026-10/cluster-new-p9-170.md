# cluster new-p9-170

Verdict: **FIXED**.

`HardwareInsert::setSettings()` previously emptied the return ring only on a disabled-to-enabled transition. When an enabled insert changed its return destination, queued samples selected by the old destination remained in the ring. The reset condition at `src/core/HardwareInsert.cpp:85-89` now also empties the ring when `settings.ret` changes.

The regression at `tests/src/core/HardwareIoTest.cpp:285-337` queues two periods on the old stereo route, consumes one, changes to a mono route, then checks that the next block contains the new route's samples. The exact final inverse-patch run failed at `newRouteMatches` with the production change removed; the same test passed after restoring it.

## Evidence

The ctest family is `HardwareIoTest` (one registered ctest). Final GREEN: 1/1 ctest passed. Final RED: the regression failed while the other 11 Qt test cases passed.

The commands below ran from the worktree root unless noted. Build/test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/`. Ctest commands ran from `build/tests`.

| Run | Exact command | EXIT | Result |
|---|---|---:|---|
| Build draft 1 | `cmake --build build --target HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 2 | First test draft attempted to modify the engine's const input view. |
| Build draft 2 | `cmake --build build --target HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Built the next draft. |
| Test draft 1 | `ctest -R HardwareIoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | Discarded: the test inspected a channel buffer that was not being processed. |
| Build draft 3 | `cmake --build build --target HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Built after the first production edit. |
| Test draft 2 | `ctest -R HardwareIoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | Discarded for the same invalid channel-buffer assertion. |
| Build draft 4 | `cmake --build build --target HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Built the engine-capture regression. |
| Test draft 3 | `ctest -R HardwareIoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed with the fix; later repeated after the inverse-patch proof. |
| Inverse patch | `git diff -- src/core/HardwareInsert.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 | Removed the production change for RED proof. |
| Build 5 | `cmake --build build --target zene HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt product and affected test with the fix removed. |
| Test 4 | `ctest -R HardwareIoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | RED: old-route samples appeared after the route change. |
| Restore patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 | Restored the production change. |
| Build 6 | `cmake --build build --target zene HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt product and affected test with the fix restored. |
| Test 5 | `ctest -R HardwareIoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed with the fix restored. |
| Final inverse patch | `git diff -- src/core/HardwareInsert.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 | Repeated proof after test cleanup changes. |
| Build 7 | `cmake --build build --target zene HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/build-7.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt product and final test with the fix removed. |
| **Final RED** | `ctest -R HardwareIoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/test-6.log 2>&1; rc=$?; echo EXIT=$rc` | **8** | `changingReturnRouteDropsQueuedFrames` failed at `newRouteMatches`; 11 other Qt test cases passed. |
| Final restore | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 | Restored the production change. |
| Build 8 | `cmake --build build --target zene HardwareIoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/build-8.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt product and final test with the fix restored. |
| **Final GREEN** | `ctest -R HardwareIoTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/test-7.log 2>&1; rc=$?; echo EXIT=$rc` | **0** | 1/1 ctest passed. |
| Earlier complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed before the final test cleanup change; final rerun below. |
| Earlier file length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed before the final test cleanup change; final rerun below. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc` | **0** | Passed. |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-170.r1/logs/gate-4.log 2>&1; rc=$?; echo EXIT=$rc` | **0** | Passed. |

The `fff grep` tool was not exposed in this session and `command -v fff` returned no path. I searched the log folder with `rg -n` for `error`, `FAILED`, and `Assertion` instead.

## Files committed

- `src/core/HardwareInsert.cpp`
- `tests/src/core/HardwareIoTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-170.md`
