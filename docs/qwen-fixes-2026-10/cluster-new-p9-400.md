# Cluster new-p9-400: concurrent retrospective takes

Verdict: **FIXED**

The finding was real. `src/core/RetroAudioCapture.cpp:152` previously gave every
writer the same `<destination>.partial` file, then removed the destination
before renaming the partial at lines 183–190. Concurrent requests could share
the staging file and race during publication.

`writeWindow()` now opens a `QSaveFile` with direct-write fallback disabled,
passes its handle to libsndfile (`src/core/RetroAudioCapture.cpp:152–160`), and
calls `commit()` only after writing and closing the WAV (`:190–195`). Each call
gets independent sibling staging; Qt atomically publishes each complete file.

The regression at `tests/src/core/RetroAudioCaptureTest.cpp:236–335` starts two
writes to the same path, checks both results and frame counts, and reads back
the whole final file to ensure it contains one complete take.

## Verification evidence

The test was run from `build/tests`. The original implementation failed, the
reversed-patch check failed, and the restored implementation passed:

| Phase | Exact command | Exit | Result |
|---|---|---:|---|
| Build original test | `cmake --build build --target RetroAudioCaptureTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-400.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (worktree root) | 0 | Test target rebuilt |
| Original-code RED | `ctest -R '^RetroAudioCaptureTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-400.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 | `secondOk` false: shared partial had already moved |
| Reverse product patch | `git diff -- src/core/RetroAudioCapture.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-400.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-400.r1/logs/fix.patch` (worktree root) | 0 | Only production change reverted |
| Rebuild without fix | `cmake --build build --target zene RetroAudioCaptureTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-400.r1/logs/build-7.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (worktree root) | 0 | `zene` and affected test rebuilt |
| Reversal RED | `ctest -R '^RetroAudioCaptureTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-400.r1/logs/test-7.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 8 | `firstOk` false during publication |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-400.r1/logs/fix.patch` (worktree root) | 0 | Fix restored |
| Final rebuild | `cmake --build build --target zene RetroAudioCaptureTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-400.r1/logs/build-8.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (worktree root) | 0 | `zene` and affected test rebuilt |
| Final GREEN | `ctest -R '^RetroAudioCaptureTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-400.r1/logs/test-8.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` | 0 | 1/1 CTest case passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-400.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (worktree root) | 0 | Passed |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-400.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc; exit $rc` (worktree root) | 0 | Passed |

The affected CTest family contains one case; the final run executed and passed
that case. Test-development runs are retained as `test-2.log` through
`test-6.log`: the early versions exposed a staging-observation assumption and
an overly strict PCM tolerance; the finalized test passed in `test-6.log` and
again after the reversal check in `test-8.log`. Their paired builds are
`build-2.log` through `build-6.log` (each exit 0).

## Files in this commit

- `src/core/RetroAudioCapture.cpp`
- `tests/src/core/RetroAudioCaptureTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-400.md`
