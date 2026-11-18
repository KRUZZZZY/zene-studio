# Cluster new-p9-176: `normalize()` zero divisor

**Verdict: FIXED**

`normalize()` now rejects a block size below two before integer-halving it. The regression checks that a one-sample block returns `-1` without changing the output buffer.

## Evidence

- `src/core/fft_helpers.cpp:68` rejects `block_size < 2`; line 70 halves only accepted sizes.
- `tests/src/core/AudioAnalysisTest.cpp:136-142` contains the regression. The original implementation returned `0` where the assertion expected `-1`.
- `tests/upstream-modifications.txt:1054` records the inherited source change.
- Affected family: `AudioAnalysisTest`, 1 registered CTest test executed per run (8 QtTest items including init and cleanup).

| Stage | Exact command | Exit |
|---|---|---:|
| Initial regression build | `cmake --build build --target AudioAnalysisTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-176.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| Initial RED | `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-176.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 |
| Rebuild `zene` before trusting RED | `cmake --build build --target zene -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-176.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| Confirmed original RED | `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-176.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 8 |
| Fixed build | `cmake --build build --target zene AudioAnalysisTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-176.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| Initial GREEN | `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-176.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 0 |
| Reverse helper patch | `git diff -- src/core/fft_helpers.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-176.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-176.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Reverse-patch build | `cmake --build build --target zene AudioAnalysisTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-176.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| Reverse-patch RED | `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-176.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 8 |
| Restore helper patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-176.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Restored build | `cmake --build build --target zene AudioAnalysisTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-176.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |
| Restored GREEN | `cd build/tests && ctest -R '^AudioAnalysisTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-176.r1/logs/test-5.log 2>&1; echo EXIT=$?` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-176.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-176.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

The reverse-patch RED failed at `normalizationRejectsAOneSampleBlock`: actual return `0`, expected `-1`. Both ratchet gates passed without baseline changes.

## Files committed

- `src/core/fft_helpers.cpp`
- `tests/src/core/AudioAnalysisTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-176.md`
