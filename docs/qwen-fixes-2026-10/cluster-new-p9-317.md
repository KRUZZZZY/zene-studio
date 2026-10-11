# Cluster new-p9-317

Verdict: **FIXED**

`plugins/NeuralAmp/tests/nam_harness.cpp:439-445` rejects a reference WAV whose sample rate differs from the input before comparing sample counts or audio. `plugins/NeuralAmp/tests/compare_rate_test.py` covers same-rate acceptance and rejection of an equal-length 44.1 kHz reference for 48 kHz input. The round-one QA rework is addressed by registering this script as `NeuralAmpCompareRateTest` in `tests/CMakeLists.txt:2593-2599`, guarded by Python and the `nam_harness` target. The configured build regenerated automatically during the build; no configure command was run.

For the negative control, I removed only the production rate-check block, saved that one-file diff, rebuilt the harness and ran the same registered CTest test. It failed as expected. I then applied the patch in reverse, rebuilt and reran CTest successfully.

## Evidence

Commands ran in this worktree. Build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-317.r2/logs`.

| Check | Exact command | Exit | Evidence |
|---|---|---:|---|
| Build before first registered run (repository root) | `cmake --build build --target nam_harness > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-317.r2/logs/build-2.log 2>&1; echo EXIT=$?` | 0 | Regenerated the configured build and built `nam_harness`. An initial invocation logged to `build-1.log` yielded before its shell exit line was captured; this fully captured repeat is the build used before CTest. |
| Registered regression with fix (`build/tests`) | `ctest -R NeuralAmpCompareRateTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-317.r2/logs/test-1.log 2>&1; echo EXIT=$?` | 0 | 1/1 test passed. |
| Save negative-control diff (repository root) | `git diff -- plugins/NeuralAmp/tests/nam_harness.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-317.r2/logs/fix.patch; echo EXIT=$?` | 0 | Diff contains only deletion of the production rate check. |
| Rebuild with only the production check removed (repository root) | `cmake --build build --target nam_harness > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-317.r2/logs/build-3.log 2>&1; echo EXIT=$?` | 0 | Rebuilt the negative-control harness. |
| Registered negative control (`build/tests`) | `ctest -R NeuralAmpCompareRateTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-317.r2/logs/test-2.log 2>&1; echo EXIT=$?` | 8 | 1/1 test failed as intended: `different-rate reference was not rejected`. |
| Restore production check (repository root) | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-317.r2/logs/fix.patch; echo EXIT=$?` | 0 | Restored the committed check. |
| Final rebuild (repository root) | `cmake --build build --target nam_harness > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-317.r2/logs/build-4.log 2>&1; echo EXIT=$?` | 0 | Rebuilt after restoring the source. |
| Final registered regression (`build/tests`) | `ctest -R NeuralAmpCompareRateTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-317.r2/logs/test-3.log 2>&1; echo EXIT=$?` | 0 | 1/1 test passed. |
| Complexity gate (repository root) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-317.r2/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 | Passed. |
| File-length gate (repository root) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-317.r2/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 | Passed. |

Affected-family count: one registered CTest test. It ran 1/1 on both fixed-code executions and 1/1 in the negative control. The script checks both same-rate success and mismatched-rate rejection.

The `fff` grep tool was not exposed in this session; I searched the log folder with one-term `rg` searches for `error`, `FAILED`, and `Assertion`.

## Files in this follow-up commit

- `tests/CMakeLists.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-317.md`

The original production fix, regression script, and `tests/fork-sources.txt` entry are in the preceding fix commit.
