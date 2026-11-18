# Cluster new-p9-450: malformed Scala ratios

**Verdict: FIXED**

`ratioFromLine()` now counts and parses slashes only in the first whitespace-delimited interval value. This refuses `3/2/7` while allowing Scala's ignored trailing description to contain a slash, as in `3/2 description/variant`.

Evidence:

- `src/core/SessionTuningScala.cpp:60-73` isolates the interval value before validating its slash count and denominator.
- `tests/src/core/SessionTuningTest.cpp:601-640` verifies malformed ratios including `3/2/7` are refused without changing the loaded table, and verifies `3/2 description/variant` loads.
- `ctest -R SessionTuningTest` matched one registered CTest test (`SessionTuningTest`). The pre-fix run had 13 QtTest slots pass and the new valid-description assertion fail; both final green runs passed 1/1 CTest test.

## Verification evidence

| Check | Exact command | Result |
| --- | --- | --- |
| RED before fix | `ctest -R SessionTuningTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r2/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit 0` | EXIT=8; 1/1 CTest failed because the valid trailing description was refused. The preceding malformed-ratio assertions, including `3/2/7`, passed. |
| GREEN after fix | `ctest -R SessionTuningTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r2/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit 0` | EXIT=0; 1/1 CTest passed. |
| Reverse only the production diff | `git diff -- src/core/SessionTuningScala.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r2/logs/fix.patch; result=$?; echo EXIT=$result; exit 0` | EXIT=0. |
| Reverse-proof rebuild | `cmake --build build --target zene SessionTuningTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r2/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit 0` | EXIT=0. |
| Reverse-proof test | `ctest -R SessionTuningTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r2/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit 0` | EXIT=8; the valid trailing-description assertion failed again. |
| Restore production diff | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r2/logs/fix.patch; result=$?; echo EXIT=$result; exit 0` | EXIT=0. |
| Restored rebuild | `cmake --build build --target zene SessionTuningTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r2/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit 0` | EXIT=0. |
| Restored test | `ctest -R SessionTuningTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r2/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit 0` | EXIT=0; 1/1 CTest passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r2/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result; exit 0` | EXIT=0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r2/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result; exit 0` | EXIT=0. |
| Final complexity gate rerun | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r2/logs/gate-3.log 2>&1; result=$?; echo EXIT=$result; exit 0` | EXIT=0. |
| Final file-length gate rerun | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r2/logs/gate-4.log 2>&1; result=$?; echo EXIT=$result; exit 0` | EXIT=0. |

The initial regression build was also successful: `cmake --build build --target zene SessionTuningTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r2/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit 0` — EXIT=0. The fixed-code build was successful: `cmake --build build --target zene SessionTuningTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r2/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit 0` — EXIT=0.

The `fff` grep tool was not exposed in this session and `command -v fff` returned no path. The build, test, and gate logs were searched with `rg -n -i` for `error`, `FAILED`, and `Assertion`. All logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-450.r2/logs/`.

## Files in the cluster commit

- `src/core/SessionTuningScala.cpp`
- `tests/src/core/SessionTuningTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-450.md`
