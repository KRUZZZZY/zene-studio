# Cluster new-p9-316 — FIXED

## Evidence

The product fix from commit `239512914` checks all WAV writes and close results in
`plugins/NeuralAmp/tests/nam_harness.cpp:135-198`, returns failure from the
`--write-input-wav` and `--wav-run --wav-out` callers at lines 426-427 and
491-495, and propagates legacy `--wav` failure at lines 652-654.

This QA follow-up registers three independent Linux CTests in
`tests/CMakeLists.txt:2590-2601`. The regression in
`plugins/NeuralAmp/tests/wav_write_failure_test.py:13-44, 74-130` requires
exit code 2 and checks each CLI path's expected failure output. The legacy case
also verifies all signal checks pass. With the product patch reversed, all
three tests independently failed because the harness returned 0. Restoring the
patch made all three pass.

## Evidence table

Build and test output is saved under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs`.

| Check | Working directory | Exact command | Result |
| --- | --- | --- | --- |
| Build and CMake test registration | worktree root | `cmake --build build --target nam_harness -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/build-1.log; exit "$rc"` | EXIT 0 |
| CTest green before reversal | `build/tests` | `ctest -R NAMHarnessWavWriteFailure --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/test-1-green.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/test-1-green.log; exit "$rc"` | EXIT 0; 3/3 passed |
| Reverse committed product patch | worktree root | `git diff HEAD^ HEAD -- plugins/NeuralAmp/tests/nam_harness.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc" > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/proof-1-reverse.log; exit "$rc"` | EXIT 0 |
| Build with product patch reversed | worktree root | `cmake --build build --target nam_harness -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/build-2-reverted.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/build-2-reverted.log; exit "$rc"` | EXIT 0 |
| RED: legacy `--wav` | `build/tests` | `ctest -R '^NAMHarnessWavWriteFailure_legacy-wav$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/test-2-red-legacy-wav.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/test-2-red-legacy-wav.log; exit "$rc"` | EXIT 8; assertion: expected exit 2, got 0 |
| RED: `--write-input-wav` | `build/tests` | `ctest -R '^NAMHarnessWavWriteFailure_write-input-wav$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/test-3-red-write-input-wav.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/test-3-red-write-input-wav.log; exit "$rc"` | EXIT 8; assertion: expected exit 2, got 0 |
| RED: `--wav-run --wav-out` | `build/tests` | `ctest -R '^NAMHarnessWavWriteFailure_wav-run$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/test-4-red-wav-run.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/test-4-red-wav-run.log; exit "$rc"` | EXIT 8; assertion: expected exit 2, got 0 |
| Reapply product patch | worktree root | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc" > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/proof-2-reapply.log; exit "$rc"` | EXIT 0 |
| Final restored build | worktree root | `cmake --build build --target nam_harness -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/build-3-restored.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/build-3-restored.log; exit "$rc"` | EXIT 0 |
| Final affected-family CTest | `build/tests` | `ctest -R NAMHarnessWavWriteFailure --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/test-5-green-restored.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/test-5-green-restored.log; exit "$rc"` | EXIT 0; 3/3 passed |
| Complexity gate | worktree root | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/gate-3-complexity-final.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/gate-3-complexity-final.log; exit "$rc"` | EXIT 0 |
| File-length gate | worktree root | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/gate-4-file-length-final.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r2/logs/gate-4-file-length-final.log; exit "$rc"` | EXIT 0 |

Affected-family count: 3 registered tests, all 3 independently RED with the
product patch reversed and all 3 GREEN with it restored.

## Files in this QA follow-up commit

- `plugins/NeuralAmp/tests/wav_write_failure_test.py`
- `tests/CMakeLists.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-316.md`
