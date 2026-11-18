# Cluster new-p10-519

Verdict: **FIXED**

## Evidence

- `include/BasicFilters.h:734` replaces a non-finite formant frequency with `minFreq()` before clamping; `include/BasicFilters.h:750` then converts the resulting bounded finite frequency to an integer table index.
- `tests/src/core/DeviceDspTest.cpp:209` covers both `Formantfilter` and `FastFormant`, comparing NaN-frequency output with the documented minimum-frequency output and checking finiteness.
- `tests/upstream-modifications.txt:1054` records the inherited header change.

## Regression evidence

Commands ran from the worktree root unless noted. Each command captured output in a new file under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-519.r1/logs` and printed the unpiped exit code.

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Original code build | `cmake --build build --target DeviceDspTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-519.r1/logs/build-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 | Built the new regression against the original implementation. |
| Original code RED (run from `build/tests`) | `ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-519.r1/logs/test-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 8 | Regression reproduced SIGSEGV in `BasicFilters<2>::calcFilterCoeffs`. |
| Fixed code build | `cmake --build build --target zene DeviceDspTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-519.r1/logs/build-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 | Rebuilt `zene` and the affected test. |
| Fixed code GREEN (run from `build/tests`) | `ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-519.r1/logs/test-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 | Passed, 1/1 CTest targets. |

## Reverse-patch proof

| Exact command | Exit | Result |
|---|---:|---|
| `git diff -- include/BasicFilters.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-519.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-519.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 | Removed only the production fix. |
| `cmake --build build --target zene DeviceDspTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-519.r1/logs/build-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 | Rebuilt before testing the reverted code. |
| `ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-519.r1/logs/test-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` (run from `build/tests`) | 8 | Same regression reproduced SIGSEGV. |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-519.r1/logs/fix.patch; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 | Restored the production fix. |
| `cmake --build build --target zene DeviceDspTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-519.r1/logs/build-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 | Rebuilt the restored code. |
| `ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-519.r1/logs/test-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` (run from `build/tests`) | 0 | Passed, 1/1 CTest targets. |

## Ratchet gates

| Exact command | Exit |
|---|---:|
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-519.r1/logs/gate-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-519.r1/logs/gate-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |

The `fff` grep tool was not available in this environment; log searches used one-term `rg -n` searches instead. No `error` or `Assertion` matches were found. `FAILED` matched only the two intentional RED runs (`test-1.log` and `test-3.log`).

## Files committed

- `include/BasicFilters.h`
- `tests/src/core/DeviceDspTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-519.md`
