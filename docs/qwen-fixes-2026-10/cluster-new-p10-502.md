# Cluster new-p10-502: FLAC open failure is treated as success

**Verdict: FIXED**

`src/core/audio/AudioFileFlac.cpp:82` now returns `false` when `sf_open()` returns a null handle. The constructor therefore reports failure, and the later FLAC commands are skipped. `tests/src/core/ExportWavDitherTest.cpp:125` exercises the real constructor using nine channels, which libsndfile rejects for FLAC.

## Regression evidence

Commands below ran from the worktree root unless noted. Build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-502.r1/logs`.

| Stage | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Original build | `cmake --build build --target ExportWavDitherTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-502.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Test target built |
| RED, original code (`build/tests`) | `ctest -R ExportWavDitherTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-502.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | `aFlacOpenFailureIsReported()` failed: `!successful` was false |
| GREEN build | `cmake --build build --target zene ExportWavDitherTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-502.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | App and test target built |
| GREEN test (`build/tests`) | `ctest -R ExportWavDitherTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-502.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | 1/1 CTest tests passed |

The required reverse-patch proof also passed: `git diff -- src/core/audio/AudioFileFlac.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-502.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-502.r1/logs/fix.patch` exited 0. With the fix removed, `cmake --build build --target zene ExportWavDitherTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-502.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` exited 0, then `ctest -R ExportWavDitherTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-502.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` from `build/tests` exited 8 on the same assertion. Reapplying the patch with `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-502.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` exited 0; the rebuild in `build-5.log` exited 0 and the test in `test-4.log` exited 0.

After the final diff, `cmake --build build --target zene ExportWavDitherTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-502.r1/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc` exited 0. The final `ctest -R ExportWavDitherTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-502.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc` from `build/tests` exited 0 (1/1 CTest tests passed).

The affected family ran one CTest test, `ExportWavDitherTest` (CTest test #62). Before the fix, its QtTest run had six passes and the new regression failure; after the fix, the CTest target passed.

## Gates

- `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-502.r1/logs/gate-3.log 2>&1; rc=$?; echo EXIT=$rc` — EXIT=0.
- `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-502.r1/logs/gate-4.log 2>&1; rc=$?; echo EXIT=$rc` — EXIT=0.

## Files in the commit

- `src/core/audio/AudioFileFlac.cpp`
- `tests/src/core/ExportWavDitherTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-502.md`
