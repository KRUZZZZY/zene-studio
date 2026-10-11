# Cluster new-p9-317

Verdict: **FIXED**

`plugins/NeuralAmp/tests/nam_harness.cpp:439` now rejects a reference WAV whose sample rate differs from the input WAV before checking the sample count or comparing samples. It exits with status 2 and reports both rates. `plugins/NeuralAmp/tests/compare_rate_test.py:22` exercises the real harness CLI: an equal-rate reference passes, while an equal-length 44.1 kHz reference for 48 kHz input must be rejected.

The harness test is standalone because the configured `build/tests` tree has no NeuralAmp CTest entry. The user instruction prohibited reconfiguration, so the regression was run directly from `build/tests`. The nearby registered plugin test was also run through CTest.

## Evidence

Commands below ran in the stated working directory. Build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-317.r1/logs`.

| Check | Exact command | Exit | Evidence |
|---|---|---:|---|
| Original-code RED | `python3 ../../plugins/NeuralAmp/tests/compare_rate_test.py ../plugins/nam_harness` (`build/tests`) | 1 | `test-2.log`: harness returned 0 and printed `reference match: PASS` for the 48 kHz / 44.1 kHz pair; the regression failed as intended. |
| Fixed-code GREEN | `python3 ../../plugins/NeuralAmp/tests/compare_rate_test.py ../plugins/nam_harness` (`build/tests`) | 0 | `test-3.log`: same-rate control passed and different-rate reference was rejected. Repeated after the negative control in `test-6.log`. |
| Negative control, remove fix | `git diff -- plugins/NeuralAmp/tests/nam_harness.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-317.r1/logs/fix.patch` | 0 | Saved only the production-file patch. |
| Negative control, reverse patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-317.r1/logs/fix.patch` | 0 | Production check temporarily removed. |
| Rebuild without fix | `cmake --build build --target nam_harness` (repository root) | 0 | `build-4.log`. |
| Regression without fix | `python3 ../../plugins/NeuralAmp/tests/compare_rate_test.py ../plugins/nam_harness` (`build/tests`) | 1 | `test-5.log`: again printed `reference match: PASS`; proves the regression catches the original bug. |
| Negative control, restore patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-317.r1/logs/fix.patch` | 0 | Restored the production check. |
| Final rebuild | `cmake --build build --target nam_harness` (repository root) | 0 | `build-5.log`. The original-code build in `build-1.log` and first fixed build in `build-2.log` also exited 0. |
| Final regression | `python3 ../../plugins/NeuralAmp/tests/compare_rate_test.py ../plugins/nam_harness` (`build/tests`) | 0 | `test-6.log`. |
| Plugin test build | `cmake --build build --target AudioPluginTest` (repository root) | 0 | `build-3.log`. |
| Registered plugin test | `ctest -R AudioPluginTest --output-on-failure` (`build/tests`) | 0 | `test-4.log`: 1/1 test passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` (repository root) | 0 | `gate-3.log`. |
| File-length gate | `bash tests/file-length-gate.sh --check` (repository root) | 0 | `gate-4.log`. |

Affected-family count: one direct NeuralAmp harness regression script, covering the same-rate and mismatched-rate cases. The closest registered CTest family ran one test (`AudioPluginTest`), with 1/1 passing. An initial runner attempt used the nonexistent path `../plugins/NeuralAmp/tests/nam_harness` and exited 1 (`test-1.log`); the corrected binary path is `../plugins/nam_harness`.

The `fff` log search tool was not available in this session; logs were searched with one-term `rg` queries instead.

## Files in the fix commit

- `plugins/NeuralAmp/tests/nam_harness.cpp`
- `plugins/NeuralAmp/tests/compare_rate_test.py`
- `tests/fork-sources.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-317.md`
