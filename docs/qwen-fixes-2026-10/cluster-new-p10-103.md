# Cluster new-p10-103

Verdict: **FIXED**

## Evidence

- `src/core/DawProjectRead.cpp:176-182`: the automation reader parses only `RealPoint` in tempo automation and `TimeSignaturePoint` in metre automation. It skips every other child, including `Target` and unknown elements.
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp:1026-1054`: the regression puts an attribute-free `<Unknown/>` in each automation, then checks that the valid point is the only point and retains its values.
- Before the fix, the regression failed: `model.tempoPoints.size()` was 2 instead of 1. Reversing the final production patch reproduced that failure; reapplying it passed.
- Affected family: 1 CTest test (`DawProjectInterchangeRoundTripTest`), containing 35 QtTest cases. The final CTest run passed 1/1.
- The first complexity check reported `parseAutomationPoints` at CCN 11. The redundant `<Target>` special case was removed because the expected-point guard already skips it. Final complexity and file-length checks both pass.

## Commands and exit codes

Commands below were run from the worktree root unless the working directory is stated. Build and test output was saved in the listed new log files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs`.

### Build commands

| Command | Exit |
| --- | ---: |
| `cmake --build build --target DawProjectInterchangeRoundTripTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/build-1.log' 2>&1; echo EXIT=$?` | 0 |
| `cmake --build build --target DawProjectInterchangeRoundTripTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/build-2.log' 2>&1; echo EXIT=$?` | 0 |
| `cmake --build build --target DawProjectInterchangeRoundTripTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/build-3.log' 2>&1; echo EXIT=$?` | 0 |
| `cmake --build build --target DawProjectInterchangeRoundTripTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/build-4.log' 2>&1; echo EXIT=$?` | 0 |
| `cmake --build build --target zene > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/build-5.log' 2>&1; echo EXIT=$?` | 0 |
| `cmake --build build --target DawProjectInterchangeRoundTripTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/build-6.log' 2>&1; echo EXIT=$?` | 0 |
| `cmake --build build --target DawProjectInterchangeRoundTripTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/build-7.log' 2>&1; echo EXIT=$?` | 0 |
| `cmake --build build --target DawProjectInterchangeRoundTripTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/build-8.log' 2>&1; echo EXIT=$?` | 0 |
| `cmake --build build --target zene > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/build-9.log' 2>&1; echo EXIT=$?` | 0 |

### CTest commands

Working directory for each command: `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-103/build/tests`.

| Command | Result |
| --- | ---: |
| `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/test-red-1.log' 2>&1; echo EXIT=$?` | EXIT=8, regression failed before the fix |
| `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/test-green-1.log' 2>&1; echo EXIT=$?` | EXIT=0 |
| `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/test-red-proof-1.log' 2>&1; echo EXIT=$?` | EXIT=8, regression failed with the production patch reversed |
| `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/test-green-proof-1.log' 2>&1; echo EXIT=$?` | EXIT=0 |
| `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/test-green-2.log' 2>&1; echo EXIT=$?` | EXIT=0 |
| `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/test-red-proof-2.log' 2>&1; echo EXIT=$?` | EXIT=8, regression failed with the final production patch reversed |
| `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/test-green-proof-2.log' 2>&1; echo EXIT=$?` | EXIT=0, 1/1 CTest tests passed |

### Patch proof commands

Each command below was run twice, once for the first fix and once for the final patch after the CCN adjustment. Each invocation returned EXIT=0.

```sh
git diff -- src/core/DawProjectRead.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/fix.patch' && git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/fix.patch'; echo EXIT=$?
git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/fix.patch'; echo EXIT=$?
```

### Ratchet gates

| Command | Exit |
| --- | ---: |
| `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/gate-1.log' 2>&1; echo EXIT=$?` | 1, initial run identified CCN 11 |
| `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/gate-2.log' 2>&1; echo EXIT=$?` | 0 |
| `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/gate-3.log' 2>&1; echo EXIT=$?` | 0 |
| `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-103.r1/logs/gate-4.log' 2>&1; echo EXIT=$?` | 0 |

## Files committed

- `src/core/DawProjectRead.cpp`
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-103.md`
