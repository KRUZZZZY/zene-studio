# Cluster new-p8-97: DAWproject tick-range export

Verdict: **FIXED**

## Evidence

| File:line | Evidence |
|---|---|
| `src/core/DawProjectWrite.cpp:125` | `isSupportedTime()` now checks the source beat and the six-decimal value emitted to XML with `dawProjectBeatRepresentable()`. This matches the reader's rounded tick conversion and signed `tick_t` limit. |
| `src/core/DawProjectModel.cpp:84` | `dawProjectBeatRepresentable()` rounds beat values to the 48-tick grid and checks the result against `tick_t` bounds. |
| `src/core/DawProjectRead.cpp:116` | The reader refuses a point before converting an unrepresentable beat. |
| `tests/src/core/DawProjectInterchangeRoundTripTest.cpp:1335` | Regression calls `writeDawProject()` and rejects clip and note times at 50,000,000 beats, plus a value whose six-decimal serialization rounds over the tick limit. |
| `tests/src/core/DawProjectInterchangeRoundTripTest.cpp:638` | The importer refusal test packages its deliberately invalid `1e300` tempo point as raw XML in a ZIP and asserts the parser rejects it specifically for the 48-tick grid. |

The test family contains one CTest case. The final QtTest run reported 35 passed, 0 failed.

## Verification command ledger

Build and gate commands ran from the worktree root. The CTest and direct QtTest commands ran from `build/tests`. Each build/test output was saved under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r1/logs`.

| Purpose | Exact command | Exit | Evidence |
|---|---|---:|---|
| Initial regression build | `cmake --build build --target DawProjectInterchangeRoundTripTest` | 0 | `build-1.log` |
| Initial RED | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure` | 8 | `test-1.log`: new clip-time assertion failed; 34 passed, 1 failed. |
| Fixed build before adapting the import fixture | `cmake --build build --target zene DawProjectInterchangeRoundTripTest` | 0 | `build-2.log` |
| Intermediate run | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure` | 8 | `test-2.log`: the existing import test still tried to create its invalid input through the writer. The fixture was changed to raw XML in a ZIP. |
| Build with the adjusted fixture | `cmake --build build --target zene DawProjectInterchangeRoundTripTest` | 0 | `build-3.log` |
| GREEN | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure` | 0 | `test-3.log`: 1/1 CTest case passed. |
| Rechecked raw import fixture | `cmake --build build --target zene DawProjectInterchangeRoundTripTest` | 0 | `build-6.log` |
| Rechecked import fixture | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure` | 0 | `test-7.log`: 1/1 CTest case passed. |
| Save and remove product fix for proof | `git diff -- src/core/DawProjectWrite.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r1/logs/fix.patch` | 0 | Regression proof setup. |
| Build with product fix removed | `cmake --build build --target zene DawProjectInterchangeRoundTripTest` | 0 | `build-4.log` |
| RED with product fix removed | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure` | 8 | `test-4.log`: the new clip-time assertion failed again; 34 passed, 1 failed. |
| Restore product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r1/logs/fix.patch` | 0 | Regression proof restoration. |
| Rebuild restored fix | `cmake --build build --target zene DawProjectInterchangeRoundTripTest` | 0 | `build-5.log` |
| GREEN after restoration | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure` | 0 | `test-5.log`: 1/1 CTest case passed. |
| Final save and remove product fix | `git diff -- src/core/DawProjectWrite.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r1/logs/fix.patch` | 0 | Regression proof against the final test source. |
| Build with product fix removed | `cmake --build build --target zene DawProjectInterchangeRoundTripTest` | 0 | `build-7.log` |
| Final RED with product fix removed | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure` | 8 | `test-8.log`: the new clip-time assertion failed again; 34 passed, 1 failed. |
| Final restore product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r1/logs/fix.patch` | 0 | Regression proof restoration. |
| Final rebuild | `cmake --build build --target zene DawProjectInterchangeRoundTripTest` | 0 | `build-8.log` |
| Final GREEN | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure` | 0 | `test-9.log`: 1/1 CTest case passed. |
| Build using the actual export API in the regression | `cmake --build build --target zene DawProjectInterchangeRoundTripTest` | 0 | `build-9.log` |
| GREEN using the actual export API | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure` | 0 | `test-11.log`: 1/1 CTest case passed. |
| Save and remove fix for actual-export proof | `git diff -- src/core/DawProjectWrite.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r1/logs/fix.patch` | 0 | Regression proof setup. |
| Final fix-removal build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest` | 0 | `build-10.log` |
| Final RED using the actual export API | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure` | 8 | `test-12.log`: `writeDawProject()` accepted the 50,000,000-beat clip when the fix was removed; 34 passed, 1 failed. |
| Restore fix for actual-export proof | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-97.r1/logs/fix.patch` | 0 | Regression proof restoration. |
| Final restored build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest` | 0 | `build-11.log` |
| Final GREEN using the actual export API | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure` | 0 | `test-13.log`: 1/1 CTest case passed. |
| Final QtTest count | `./DawProjectInterchangeRoundTripTest` | 0 | `test-14.log`: 35 passed, 0 failed. |
| Complexity ratchet | `bash tests/complexity-gate.sh --check` | 0 | `gate-7.log` |
| File-length ratchet | `bash tests/file-length-gate.sh --check` | 0 | `gate-8.log` |

## Files committed

- `src/core/DawProjectWrite.cpp`
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-97.md`
