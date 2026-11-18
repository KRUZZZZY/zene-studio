# Cluster new-p9-540

Verdict: **FIXED**

`plugins/VstEffect/VstSubPluginFeatures.cpp:77` now excludes symbolic links from the directory candidates. The recursive call at line 82 therefore cannot follow a directory symlink back to this directory or an ancestor. The regression at `tests/src/core/VstSubPluginFeaturesTest.cpp:35` creates that layout and asserts one plugin key at line 62.

## Verification evidence

The affected ctest family contains one test (`VstSubPluginFeaturesTest`); each ctest result below ran 1 test.

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Original scanner, RED | `ctest -R VstSubPluginFeaturesTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-540.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` (cwd `build/tests`) | 8 | Failed as intended: 41 keys, expected 1. |
| Fixed scanner, GREEN | `ctest -R VstSubPluginFeaturesTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-540.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` (cwd `build/tests`) | 0 | 1/1 passed. |
| Fix reversed, RED proof | `ctest -R VstSubPluginFeaturesTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-540.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` (cwd `build/tests`; after reversing `fix.patch` and rebuilding) | 8 | Failed as intended: 41 keys, expected 1. |
| Fix restored, GREEN proof | `ctest -R VstSubPluginFeaturesTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-540.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` (cwd `build/tests`; after reapplying `fix.patch` and rebuilding) | 0 | 1/1 passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-540.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed. |
| File length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-540.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed. |

Each ctest run followed a rebuild of both `zene` and `VstSubPluginFeaturesTest`:

| Build | Exact command | Exit | Result |
|---|---|---:|---|
| Initial attempt | `cmake --build build --target zene VstSubPluginFeaturesTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-540.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 2 | Make had not yet picked up the target after CMake regenerated the test build files. |
| Retry before RED | `cmake --build build --target zene VstSubPluginFeaturesTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-540.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Fresh original scanner binaries. |
| Fixed scanner | `cmake --build build --target zene VstSubPluginFeaturesTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-540.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt after the fix. |
| Fix reversed | `cmake --build build --target zene VstSubPluginFeaturesTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-540.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt original scanner for inverse RED proof. |
| Fix restored | `cmake --build build --target zene VstSubPluginFeaturesTest -j2 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-540.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Rebuilt fixed scanner for restored GREEN proof. |

The inverse proof used these patch commands, each of which exited 0:

- `git diff -- plugins/VstEffect/VstSubPluginFeatures.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-540.r1/logs/fix.patch`
- `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-540.r1/logs/fix.patch; rc=$?; echo EXIT=$rc`
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-540.r1/logs/fix.patch; rc=$?; echo EXIT=$rc`

## Files committed

- `plugins/VstEffect/VstSubPluginFeatures.cpp`
- `tests/CMakeLists.txt`
- `tests/src/core/VstSubPluginFeaturesTest.cpp`
- `tests/fork-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-540.md`
