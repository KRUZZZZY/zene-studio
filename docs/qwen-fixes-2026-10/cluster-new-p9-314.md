# Cluster new-p9-314

Verdict: **FIXED**

`plugins/NeuralAmp/tests/nam_harness.cpp:359-363` now rejects a negative
`warmupBlocks` value with exit code 2 before loading the model or allocating
the generated input buffers. The regression at
`tests/src/plugins/AudioPluginTest.cpp:443-469` launches the harness with
`--warmup -1` and verifies the validation diagnostic appears before any model
load failure.

## Regression evidence

The affected CTest family is `AudioPluginTest`: one CTest case ran on each
invocation. With the original harness, the regression failed because the
harness attempted to load the deliberately missing model. With the fix, it
passed. Removing only the product patch reproduced the same failure after
rebuilding `zene` and `nam_harness`; restoring the patch passed again.

| Run | Command | Exit | Evidence log |
| --- | --- | ---: | --- |
| Original RED | `ctest -R AudioPluginTest --output-on-failure` (cwd `build/tests`) | 8 | `test-1.log`, 1/1 failed at `namHarnessRejectsNegativeWarmup` |
| Initial GREEN | `ctest -R AudioPluginTest --output-on-failure` (cwd `build/tests`) | 0 | `test-2.log`, 1/1 passed |
| Patch removed RED | `ctest -R AudioPluginTest --output-on-failure` (cwd `build/tests`) | 8 | `test-3.log`, assertion saw `LOAD FAILED` instead of the warmup diagnostic |
| Rebuilt `zene`, patch removed RED | `ctest -R AudioPluginTest --output-on-failure` (cwd `build/tests`) | 8 | `test-4.log`, same assertion; build was `cmake --build build --target zene nam_harness AudioPluginTest -j2` |
| Restored GREEN | `ctest -R AudioPluginTest --output-on-failure` (cwd `build/tests`) | 0 | `test-5.log`, 1/1 passed |

Build commands and exit codes:

| Command | Exit | Log |
| --- | ---: | --- |
| `cmake --build build --target nam_harness AudioPluginTest -j2` | 0 | `build-1.log` (original), `build-2.log` (fixed), `build-3.log` (patch removed) |
| `cmake --build build --target zene nam_harness AudioPluginTest -j2` | 0 | `build-4.log` (patch removed), `build-5.log` (restored) |

4b patch proof commands:

- `git diff -- plugins/NeuralAmp/tests/nam_harness.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-314.r1/logs/fix.patch` — exit 0
- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-314.r1/logs/fix.patch` — exit 0
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-314.r1/logs/fix.patch` — exit 0

Both required gates passed with exit code 0:

- `bash tests/complexity-gate.sh --check` — `gate-1.log`
- `bash tests/file-length-gate.sh --check` — `gate-2.log`

## Files committed

- `plugins/NeuralAmp/tests/nam_harness.cpp`
- `tests/src/plugins/AudioPluginTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-314.md`

All build and test logs are under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-314.r1/logs`.
