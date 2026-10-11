# Cluster new-p9-253

Verdict: **FIXED**.

The request formats now stop at the program byte in `thirdparty/mts-esp/libMTSClient.cpp:703`, before the parser can clear the tuning name or parse nonexistent tuning payload. The parse epilogue at line 809 also leaves map metadata alone for requests. The new `SessionTuningTest` slots at `tests/src/core/SessionTuningTest.cpp:980` and `:993` prove that a request preserves the name, map size/start key, and a seeded local note frequency.

## Regression evidence

The affected ctest family contains one registered CTest target and 14 ordinary Qt test slots (16 QtTest totals when `initTestCase` and `cleanupTestCase` are counted). The final RED run reported 14 passed and the two new slots failed. The GREEN run reports 1/1 CTest targets passed.

All ctest commands below ran from `build/tests`.

| State | Exact command | Exit | Evidence |
|---|---|---:|---|
| RED, fix reversed | `ctest -R SessionTuningTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/test-red-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/test-red-4.log; exit "$rc"` | 8 | 14 passed, 2 failed: both request-preservation slots. |
| GREEN, patch restored | `ctest -R SessionTuningTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/test-green-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/test-green-3.log; exit "$rc"` | 0 | 100% tests passed, 0 failed of 1. |

The same test was rerun with the production diff removed and restored:

| Step | Exact command | Exit |
|---|---|---:|
| Save and reverse only the parser fix | `git diff -- thirdparty/mts-esp/libMTSClient.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/fix.patch` | 0 |
| Rebuild without the fix | `cmake --build build --target zene SessionTuningTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/build-7.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/build-7.log; exit "$rc"` | 0 |
| RED run without the fix | The RED command above; log `test-red-4.log`. | 8 |
| Restore the parser fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/fix.patch` | 0 |
| Rebuild with the fix | `cmake --build build --target zene SessionTuningTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/build-8.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/build-8.log; exit "$rc"` | 0 |
| GREEN run with the fix | The GREEN command above; log `test-green-3.log`. | 0 |

The build tree had no target for a new test executable: `cmake --build build --target MtsTuningRequestTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/build-1.log; exit "$rc"` (exit 2). To honor the no-configure constraint, the regression was added as two slots to the already registered `SessionTuningTest` target. The first full-family attempt used the RED command above with log `test-red-1.log` (exit 8) and exposed an unrelated missing test plugin. After `cmake --build build --target zene tripleoscillator SessionTuningTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/build-3.log; exit "$rc"` (exit 0), the original-code RED run had only the two intended failures (`test-red-2.log`: 14 passed, 2 failed).

## Gates

| Gate | Exact command | Exit |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/gate-3.log; exit "$rc"` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/gate-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-253.r1/logs/gate-4.log; exit "$rc"` | 0 |

## Files committed

- `thirdparty/mts-esp/libMTSClient.cpp`
- `tests/src/core/SessionTuningTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-253.md`
