# Cluster new-p10-447

Verdict: **FIXED**

The finding is real. `src/core/ValueBuffer.cpp:25-26` now checks the signed length before converting it to `std::size_t` for the vector base constructor. Negative values throw `std::invalid_argument`. Regression coverage is in `tests/src/core/ModulationRateTest.cpp:125-127`.

## Evidence

The regression was added first. Against the original constructor, `ValueBuffer(-1)` raised the vector size error instead of `std::invalid_argument`; with the fix it passed. Removing only the production patch reproduced the same failure, and restoring it passed again.

The affected CTest family contains **1 registered test** (`ModulationRateTest`, CTest #155), executed on each run below.

## Commands and results

All build and test output was saved under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-447.r1/logs`.

| Run | Exact command | Exit |
|---|---|---:|
| Build regression target before fix (`build-1.log`) | `LOGDIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-447.r1/logs; cmake --build build --target zene ModulationRateTest -- -j12 > "$LOGDIR/build-1.log" 2>&1; echo EXIT=$?` | 0 |
| RED (`test-1.log`) | `cd build/tests && ctest -R '^ModulationRateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-447.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 |
| Build fixed target (`build-2.log`) | `LOGDIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-447.r1/logs; cmake --build build --target zene ModulationRateTest -- -j12 > "$LOGDIR/build-2.log" 2>&1; echo EXIT=$?` | 0 |
| GREEN (`test-2.log`) | `cd build/tests && ctest -R '^ModulationRateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-447.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 0 |
| Save production-only proof patch (`fix.patch`) | `git diff -- src/core/ValueBuffer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-447.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Remove fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-447.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Build without fix (`build-3.log`) | `LOGDIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-447.r1/logs; cmake --build build --target zene ModulationRateTest -- -j12 > "$LOGDIR/build-3.log" 2>&1; echo EXIT=$?` | 0 |
| Inverse RED (`test-3.log`) | `cd build/tests && ctest -R '^ModulationRateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-447.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 8 |
| Reapply fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-447.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild fixed target (`build-4.log`) | `LOGDIR=/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-447.r1/logs; cmake --build build --target zene ModulationRateTest -- -j12 > "$LOGDIR/build-4.log" 2>&1; echo EXIT=$?` | 0 |
| Restored GREEN (`test-4.log`) | `cd build/tests && ctest -R '^ModulationRateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-447.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate (`gate-1.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-447.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate (`gate-2.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-447.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

## Files committed

- `src/core/ValueBuffer.cpp`
- `tests/src/core/ModulationRateTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-447.md`
