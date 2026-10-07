# Cluster new-p8-112: invalid DAWproject booleans

Verdict: **FIXED**

## Finding and change

`flagAttribute` in [src/core/DawProjectReadShared.h](../../src/core/DawProjectReadShared.h#L133) previously treated every value other than `true` and `1` as false. It now keeps the existing false default when an optional attribute is absent, accepts the XML boolean values `true`, `false`, `1`, and `0`, and records a parse error for any other present value, including an empty value. The channel reader passes its `ReadState` through both boolean call sites in [src/core/DawProjectReadTracks.cpp](../../src/core/DawProjectReadTracks.cpp#L256) (solo) and [src/core/DawProjectReadTracks.cpp](../../src/core/DawProjectReadTracks.cpp#L272) (Mute).

Regression coverage in [DawProjectInterchangeRoundTripTest.cpp](../../tests/src/core/DawProjectInterchangeRoundTripTest.cpp#L965) refuses malformed solo and Mute values and an empty solo value, while checking all four valid lexical forms.

## Evidence

| Check | Exact test command (cwd: `build/tests`) | Exit |
|---|---|---:|
| RED on original code | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 |
| GREEN after fix | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| Sensitivity RED with production patch removed, after rebuilding `zene` and the test target | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 |
| Affected CTest family | `DawProjectInterchangeRoundTripTest`: 1 CTest test ran (1/1) | — |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |

The RED output names `malformedBooleanAttributesAreRefused()` and reports that the invalid solo boolean was accepted. The final GREEN run reports `100% tests passed, 0 tests failed out of 1`.

## Exact commands and captured exit codes

All commands below ran from the repository root unless marked `cwd: build/tests`. Build and test output was redirected to new numbered files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs`.

```sh
mkdir -p /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs && cmake --build build --target DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc
# EXIT=0

ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc
# cwd: build/tests; EXIT=8

cmake --build build --target DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc
# EXIT=0

ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc
# cwd: build/tests; EXIT=0

git diff -- src/core/DawProjectReadShared.h src/core/DawProjectReadTracks.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc
# EXIT=0

cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc
# EXIT=0

ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc
# cwd: build/tests; EXIT=8

git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc
# EXIT=0

cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc
# EXIT=0

ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc
# cwd: build/tests; EXIT=0

bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc
# EXIT=0

bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc
# EXIT=0
```

## Files committed

- `src/core/DawProjectReadShared.h`
- `src/core/DawProjectReadTracks.cpp`
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-112.md`
