# Cluster new-p9-107: delay template constructors

**Verdict: FIXED**

## Evidence

`include/Delay.h:69-74`, `140-145`, `211-218`, and `294-299` contained constructor initializers that named members the templates do not declare. The dual tap constructor also initialized nonexistent `m_delay` and `m_fraction` members instead of its two delay and fraction pairs. Each constructor now initializes its declared `m_gain`; the dual tap constructor initializes both delay taps and both fractions to zero.

`tests/src/core/DeviceDspTest.cpp:180-183` explicitly instantiates all four templates. Its `delayLineTemplatesInstantiate()` case at lines 213-224 constructs and exercises the public feedback and allpass delay lines. The test file is product-new and remains registered in `tests/fork-sources.txt`; it does not belong in `tests/upstream-modifications.txt`.

## Build and test evidence

All build and test output was redirected to fresh numbered files in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-107.r1/logs`. The original-code regression and the inverse-patch proof both failed at compilation with the missing-member diagnostics. The test family ran one CTest case (`DeviceDspTest`), which passed after the fix and after restoring it in the inverse-patch proof.

| Phase | Exact command | Working directory | Log | Exit |
| --- | --- | --- | --- | ---: |
| RED, before fix | `cmake --build build --target zene DeviceDspTest --parallel 4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-107.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result` | worktree | `build-1.log` | 2 |
| GREEN | `cmake --build build --target zene DeviceDspTest --parallel 4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-107.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result` | worktree | `build-2.log` | 0 |
| GREEN ctest, 1/1 passed | `ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-107.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result` | `build/tests` | `test-1.log` | 0 |
| Create and reverse product patch | `git diff -- include/Delay.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-107.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-107.r1/logs/fix.patch; result=$?; echo EXIT=$result` | worktree | `fix.patch` | 0 |
| RED, inverse patch | `cmake --build build --target zene DeviceDspTest --parallel 4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-107.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result` | worktree | `build-3.log` | 2 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-107.r1/logs/fix.patch; result=$?; echo EXIT=$result` | worktree | `fix.patch` | 0 |
| GREEN, restored patch | `cmake --build build --target zene DeviceDspTest --parallel 4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-107.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result` | worktree | `build-4.log` | 0 |
| GREEN ctest after proof, 1/1 passed | `ctest -R '^DeviceDspTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-107.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result` | `build/tests` | `test-2.log` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-107.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result` | worktree | `gate-1.log` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-107.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result` | worktree | `gate-2.log` | 0 |

The `fff grep` tool was not available in the active tool list; each log was searched with single-pattern `rg` queries instead of being read in full.

## Commit history and review correction

The original fix commit included `include/Delay.h`, `tests/src/core/DeviceDspTest.cpp`, `tests/upstream-modifications.txt`, and this report. Luna's round 1 review correctly identified the test-file entry in `tests/upstream-modifications.txt` as a misclassification. This follow-up removes that entry; the `tests/fork-sources.txt` registration remains.

## Review correction verification

The correction changes only the source-classification manifest and this report. The original regression and inverse-patch evidence above remains applicable to the unchanged constructor fix and test. The required ratchet gates were rerun after the correction:

| Gate | Exact command | Working directory | Log | Exit |
| --- | --- | --- | --- | ---: |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-107.r2/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-107.r2/logs/gate-1.log; echo EXIT=$result` | worktree | `gate-1.log` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-107.r2/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-107.r2/logs/gate-2.log; echo EXIT=$result` | worktree | `gate-2.log` | 0 |

The `fff grep` tool was not available in the active tool list. I searched the new logs with separate `rg` patterns for `error`, `FAILED`, and `Assertion`; no matches were found.

## Files in the review-correction commit

- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-107.md`
