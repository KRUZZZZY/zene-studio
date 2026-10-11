# Cluster new-p9-316 — FIXED

## Evidence

The finding is real. `plugins/NeuralAmp/tests/nam_harness.cpp:158-160` now
checks both float32 WAV writes and `fclose`; lines `175-195` check each int16
header/sample write and `fclose`. The legacy `--wav` caller at lines `652-654`
returns exit code 2 when the writer reports failure. The new
`plugins/NeuralAmp/tests/wav_write_failure_test.py:88-109` exercises all three
CLI output paths against Linux `/dev/full` using a generated minimal NAM model.

The initial regression failed on the original harness: signal sanity and
colouring passed, but `--wav /dev/full` returned success. After the fix all
three paths return a non-zero exit code. Reversing only the production patch
made the regression fail again; reapplying it made the same test pass.

## Evidence table

Commands below ran in the stated working directory. Build and test output is
saved in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs`.

| Check | Exact command | Result |
| --- | --- | --- |
| Baseline harness build (repo root) | `cmake --build build --target nam_harness > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/build-1.log; exit "$rc"` | EXIT=0 |
| RED on original binary (`build/tests`) | `python3 ../../plugins/NeuralAmp/tests/wav_write_failure_test.py ../plugins/nam_harness > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/test-1-red.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/test-1-red.log; exit "$rc"` | EXIT=1; assertion: legacy `--wav` returned success after a failing write |
| Fixed harness build (repo root) | `cmake --build build --target nam_harness > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/build-2.log; exit "$rc"` | EXIT=0 |
| GREEN (`build/tests`) | `python3 ../../plugins/NeuralAmp/tests/wav_write_failure_test.py ../plugins/nam_harness > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/test-2-green.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/test-2-green.log; exit "$rc"` | EXIT=0; all 3 CLI cases passed |
| Create and reverse production patch (repo root) | `git diff -- plugins/NeuralAmp/tests/nam_harness.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/fix.patch` | EXIT=0 |
| Reverted harness build (repo root) | `cmake --build build --target nam_harness > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/build-3.log; exit "$rc"` | EXIT=0 |
| RED with fix reversed (`build/tests`) | `python3 ../../plugins/NeuralAmp/tests/wav_write_failure_test.py ../plugins/nam_harness > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/test-3-revert-red.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/test-3-revert-red.log; exit "$rc"` | EXIT=1; original failure reproduced |
| Reapply production patch (repo root) | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/fix.patch` | EXIT=0 |
| Reapplied harness build (repo root) | `cmake --build build --target nam_harness > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/build-4.log; exit "$rc"` | EXIT=0 |
| GREEN after reapplying (`build/tests`) | `python3 ../../plugins/NeuralAmp/tests/wav_write_failure_test.py ../plugins/nam_harness > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/test-4-reapply-green.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/test-4-reapply-green.log; exit "$rc"` | EXIT=0; all 3 CLI cases passed |
| Affected-family test count | NAM harness: 3 CLI scenarios in the standalone regression script (`--wav`, `--write-input-wav`, `--wav-run --wav-out`) | 3 cases executed |
| Complexity gate (repo root) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/gate-3-complexity-final.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/gate-3-complexity-final.log; exit "$rc"` | EXIT=0 |
| File-length gate (repo root) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/gate-4-file-length-final.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc" >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-316.r1/logs/gate-4-file-length-final.log; exit "$rc"` | EXIT=0 |

## Files committed

- `plugins/NeuralAmp/tests/nam_harness.cpp`
- `plugins/NeuralAmp/tests/wav_write_failure_test.py`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-316.md`
