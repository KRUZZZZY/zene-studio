# Cluster new-p10-156

Verdict: **FIXED**

## Evidence

| File | Evidence |
| --- | --- |
| `src/core/ImportDetectionDsp.cpp:263` | The first spectral frame now uses a zero-valued preceding spectrum when computing positive spectral flux. |
| `src/core/ImportDetectionDsp.cpp:295` and `:312` | An isolated frame-zero onset can clear the RMS floor even when later, better-centred clicks set a higher relative threshold. A sustained opening tone remains excluded. |
| `tests/src/core/ImportDetectionTest.cpp:237` | A 128 BPM click track beginning at sample zero checks that the first reported onset is within 30 ms of the file start. |

Before the fix, and again with the final production patch removed, the regression reported its first onset 20,480 samples after the start (the later click) and failed. With the fix restored, the same test family passed. The steady-tone refusal in that family also continued to pass.

## Commands and results

Commands below were run from the stated working directory. Build and test output was captured in the named log under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-156.r1/logs`; each log records the unpiped exit code.

| Stage | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Build with fix removed (from worktree) | `cmake --build build --target zene ImportDetectionTest` | 0 | `build-8-no-fix.log` |
| RED (from `build/tests`) | `ctest -R '^ImportDetectionTest$' --output-on-failure` | 8 | `test-8-no-fix.log`; 1/1 ctest failed at `aTransientAtTheStartOfTheFileIsFound`, with first onset at sample 20,480 |
| Build with fix restored (from worktree) | `cmake --build build --target zene ImportDetectionTest` | 0 | `build-9-green-proof.log` |
| GREEN (from `build/tests`) | `ctest -R '^ImportDetectionTest$' --output-on-failure` | 0 | `test-9-green-proof.log`; 1/1 ctest passed |
| Complexity gate (from worktree) | `bash tests/complexity-gate.sh --check` | 0 | `gate-7-complexity.log` |
| File-length gate (from worktree) | `bash tests/file-length-gate.sh --check` | 0 | `gate-8-file-length.log` |

Regression-proof commands:

| Exact command | Exit |
| --- | ---: |
| `git diff -- src/core/ImportDetectionDsp.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-156.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-156.r1/logs/fix.patch` | 0 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-156.r1/logs/fix.patch` | 0 |

The RED and GREEN commands above were run after those patch-removal and patch-restoration steps, with a rebuild of `zene` and `ImportDetectionTest` before each run.

Log-search note: the fff grep tool was not exposed in this session, and `command -v fff` exited 1. I searched the log folder with `rg -n` for each requested term: `error`, `FAILED`, and `Assertion`.

## Files committed

- `src/core/ImportDetectionDsp.cpp`
- `tests/src/core/ImportDetectionTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-156.md`

The test file was already registered in `tests/fork-sources.txt`.
