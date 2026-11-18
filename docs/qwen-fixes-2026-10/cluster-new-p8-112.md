# Cluster new-p8-112: invalid DAWproject booleans

Verdict: **FIXED**

## Finding and change

`flagAttribute` in [src/core/DawProjectReadShared.h:163](../../src/core/DawProjectReadShared.h#L163) normalized values with `QString::trimmed()`, which also removes Unicode whitespace outside XML Schema's whitespace set. `collapseXmlWhitespace` at [src/core/DawProjectReadShared.h:99](../../src/core/DawProjectReadShared.h#L99) now applies XML Schema whitespace collapse using only space, tab, CR, and LF. Other Unicode whitespace remains part of the value and invalid boolean values are refused.

The regression at [DawProjectInterchangeRoundTripTest.cpp:972](../../tests/src/core/DawProjectInterchangeRoundTripTest.cpp#L972) asserts that `solo="&#xA0;true"` is refused. The test passed for the four valid forms and retained coverage for ordinary malformed and empty values.

## Evidence

| Check | Exact command (cwd noted where relevant) | Exit |
|---|---|---:|
| First RED before the production edit | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r2/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (cwd: `build/tests`) | 8 |
| GREEN after the production edit | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r2/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (cwd: `build/tests`) | 0 |
| Sensitivity RED with only the production patch reversed, after rebuilding `zene` and the test target | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r2/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (cwd: `build/tests`) | 8 |
| Final GREEN after restoring and rebuilding | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r2/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (cwd: `build/tests`) | 0 |
| Affected-family count | `DawProjectInterchangeRoundTripTest`: 1/1 CTest test ran | — |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r2/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r2/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 |
| `git diff --check` | `git diff --check; rc=$?; echo EXIT=$rc; exit $rc` | 0 |

The first RED and reversed-patch RED both report `invalid solo boolean was accepted`. The final GREEN log reports `100% tests passed, 0 tests failed out of 1`.

## Build, patch-reversal, and restore commands

All build and test output was written to new files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r2/logs`.

```sh
cmake --build build --target DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r2/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc
# EXIT=0

cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r2/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc
# EXIT=0

git diff -- src/core/DawProjectReadShared.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r2/logs/fix.patch
# EXIT=0

cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r2/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc
# EXIT=0

git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r2/logs/fix.patch; rc=$?; echo EXIT=$rc; exit $rc
# EXIT=0

cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-112.r2/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc
# EXIT=0
```

## Files committed

- `src/core/DawProjectReadShared.h`
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-112.md`
