# Cluster new-p10-133

Verdict: **FIXED**

`readStemFile()` now computes the expected byte count with checked `qint64` arithmetic, compares it with `QFile::size()` before reading, and reads at most that many bytes. `readStems()` passes the input frame count into the reader. The regression uses a named pipe that supplies one valid frame but reports size zero; the old reader accepts it, while the fixed reader rejects it before consuming the contents.

## File evidence

- `src/core/ExternalProcessStemSeparator.cpp:81-101`: expected-frame parameter, overflow guard, size check before the bounded read.
- `src/core/ExternalProcessStemSeparator.cpp:125`: passes the mix frame count.
- `tests/src/core/StemSplitPipelineTest.cpp:152-200`: the zero-reported-size regression and its failed-status assertion.

## Build and test evidence

Build commands ran from the worktree root. Test commands ran from `build/tests`. All build and test output was written to the listed log under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-133.r1/logs`; each log records its unpiped exit code.

| Check | Exact command | Exit and result |
|---|---|---|
| Baseline RED (`test-4.log`) | `ctest -R '^StemSplitPipelineTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-133.r1/logs/test-4.log 2>&1` | 8; 8 passed, 1 failed. Regression observed `Success` where it expects `Failed`. |
| Fixed GREEN (`test-5.log`) | `ctest -R '^StemSplitPipelineTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-133.r1/logs/test-5.log 2>&1` | 0; 9 passed, 0 failed, 0 skipped. |
| Patch-reversal RED (`test-6.log`) | `ctest -R '^StemSplitPipelineTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-133.r1/logs/test-6.log 2>&1` | 8; after reversing the production patch, regression again observed `Success` where it expects `Failed`. |
| Restored GREEN (`test-7.log`) | `ctest -R '^StemSplitPipelineTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-133.r1/logs/test-7.log 2>&1` | 0; 9 passed, 0 failed, 0 skipped. |
| Complexity gate (`gate-1.log`) | `bash tests/complexity-gate.sh --check` | 0; PASS. |
| File length gate (`gate-2.log`) | `bash tests/file-length-gate.sh --check` | 0; PASS. |

The affected family therefore has 9 passing QtTest cases in each green run. Before each ctest run, `cmake --build build --target zene StemSplitPipelineTest` completed with exit 0; build logs are `build-4.log` through `build-7.log` for the authoritative RED, GREEN, reversal, and restored GREEN runs.

Patch-reversal proof commands, each with exit 0:

```sh
git diff -- src/core/ExternalProcessStemSeparator.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-133.r1/logs/fix.patch
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-133.r1/logs/fix.patch
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-133.r1/logs/fix.patch
```

The initial exploratory runs are not used as regression proof: `test-1.log` passed because the harness had an invalid model path; `test-2.log` failed on that invalid path; `test-3.log` failed an early diagnostic assertion while the original reader returned success. The valid baseline RED is `test-4.log`.

`fff` was unavailable in the environment (`command -v fff` exited 1); log searches used `rg`, one term per search.

## Files committed

- `src/core/ExternalProcessStemSeparator.cpp`
- `tests/src/core/StemSplitPipelineTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-133.md`
