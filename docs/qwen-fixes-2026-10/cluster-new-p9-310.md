# Cluster new-p9-310: NAM multi-block prewarm

Verdict: **FIXED**

`plugins/NeuralAmp/nam/NamModel.cpp:101` now zeroes each chunk of the aliased prewarm buffer before `processChunk()` writes its output there. The regression in `tests/src/core/NamModelPrewarmTest.cpp:39` builds an 8193-sample prewarm model and compares its later output with an instance reset and explicitly fed silence; the comparison is at line 77.

The test failed before the fix with `prewarmed=0.197056219` versus `silence reference=0.0996679962`. Reversing only the production patch reproduced the same failure. After restoring the patch, it passed.

## Evidence

All build and test output was captured in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-310.r1/logs`. The initial test-target build had no generated rule (exit 2); `cmake --build build --target zene` refreshed the existing build tree without an explicit configure command.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Initial target lookup | `cmake --build build --target NamModelPrewarmTest` | 2 | Target rule not generated yet; `build-1.log` |
| Refresh configured tree | `cmake --build build --target zene` | 0 | `build-3.log` |
| Build regression target before RED | `cmake --build build --target NamModelPrewarmTest` | 0 | `build-4.log` |
| RED, original implementation | `cd build/tests && ctest -R NamModelPrewarmTest --output-on-failure` | 8 | 1/1 failed at the intended comparison; `test-1.log` |
| Build after fix | `cmake --build build --target zene neuralamp NamModelPrewarmTest` | 0 | `build-5.log` |
| GREEN | `cd build/tests && ctest -R NamModelPrewarmTest --output-on-failure` | 0 | 1/1 passed; `test-2.log` |
| Save and reverse only production fix | `git diff -- plugins/NeuralAmp/nam/NamModel.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-310.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-310.r1/logs/fix.patch` | 0 | Reversal proof |
| Build with fix reversed | `cmake --build build --target zene neuralamp NamModelPrewarmTest` | 0 | `build-6.log` |
| RED, reversal proof | `cd build/tests && ctest -R NamModelPrewarmTest --output-on-failure` | 8 | 1/1 failed with the same values; `test-3.log` |
| Restore production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-310.r1/logs/fix.patch` | 0 | Fix restored |
| Final build | `cmake --build build --target zene neuralamp NamModelPrewarmTest` | 0 | `build-7.log` |
| Final GREEN | `cd build/tests && ctest -R NamModelPrewarmTest --output-on-failure` | 0 | 1/1 passed; `test-4.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |

Affected-family test count: **1 test executed** (`NamModelPrewarmTest`; zero tests would be an error).

## Files committed

- `plugins/NeuralAmp/nam/NamModel.cpp`
- `tests/src/core/NamModelPrewarmTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-310.md`
