# Cluster new-p9-175: `precomputeWindow()` ignores `normalized`

Verdict: **FIXED**. `precomputeWindow()` now applies mean-gain amplitude correction only when `normalized` is true. The regression checks that the peak stays at unity when normalization is disabled for Blackman-Harris, Hamming, and Hanning windows.

## File and line evidence

- `include/fft_helpers.h:82`: documents that `normalized=false` disables amplitude correction.
- `src/core/fft_helpers.cpp:105,154-159`: the function now guards the correction with `if (normalized)`.
- `tests/src/core/AudioAnalysisTest.cpp:96-104`: regression assertion for all three cosine-sum windows.

## Evidence

| Check | Exact command | Exit |
|---|---|---:|
| Initial build, original implementation (worktree root) | `cmake --build build --target zene AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-175.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| Initial RED (`build/tests`) | `ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-175.r1/logs/red-1.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 8 |
| Build with fix (worktree root) | `cmake --build build --target zene AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-175.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| GREEN (`build/tests`) | `ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-175.r1/logs/green-1.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| Reverse-patch command | `git diff -- src/core/fft_helpers.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-175.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-175.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| Build with fix reverted (worktree root) | `cmake --build build --target zene AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-175.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| Reverse-patch RED (`build/tests`) | `ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-175.r1/logs/proof-red-1.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 8 |
| Reapply-patch command | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-175.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| Rebuild with fix restored (worktree root) | `cmake --build build --target zene AudioAnalysisTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-175.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| Reverse-patch GREEN (`build/tests`) | `ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-175.r1/logs/proof-green-1.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| Complexity gate (worktree root) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-175.r1/logs/gate-complexity-2.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| File-length gate (worktree root) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-175.r1/logs/gate-file-length-2.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |

The affected ctest family executed 1/1 test target (`AudioAnalysisTest`). Its Qt test binary has 8 cases including setup and cleanup; the new assertion failed before the fix and passed with the fix, and the full target passed after each fixed build.

Both required gates passed. The reverse-patch proof failed on the same assertion without the production change and passed after reapplication.

## Files committed

- `src/core/fft_helpers.cpp`
- `tests/src/core/AudioAnalysisTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-175.md`
