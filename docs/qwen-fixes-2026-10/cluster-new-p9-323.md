# Cluster new-p9-323

**Verdict: FIXED**

## Finding and change

`nam_profile` accepted zero and negative measured block counts. Its report then divided timing values by that count and returned success. It also accepted negative or malformed warmup values and non-positive or non-finite sample rates.

The profiler now parses integer and floating-point arguments strictly, requires a positive block count, keeps block size within its existing range, requires non-negative warmup, and accepts only positive finite sample rates. It reports invalid arguments and exits with code 2 before loading the model.

File evidence:

- `plugins/NeuralAmp/tests/nam_profile.cpp:115` strictly parses integer arguments; `:144` rejects malformed, non-finite, and non-positive sample rates.
- `plugins/NeuralAmp/tests/nam_profile.cpp:157` validates the optional CLI arguments; `:211` handles validation before model loading; `:264` and `:270-278` contain the profiler divisions that require a positive block count.
- `tests/src/plugins/NeuralAmpProfileArgumentsTest.cpp:119-166` covers zero and negative blocks, invalid warmup, invalid sample rates, and a valid invocation using a minimal real `.nam` model.
- `tests/CMakeLists.txt:1967-1975` registers the test when `nam_profile` is available and builds the profiler as its dependency.

## Verification evidence

Build commands ran from the worktree root. CTest commands ran from `build/tests`. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-323.r1/logs`.

| Phase | Command | EXIT | Log / evidence |
|---|---|---:|---|
| Initial target lookup | `cmake --build build --target NeuralAmpProfileArgumentsTest -j2` | 2 | `build-1.log`; generated Makefiles did not yet contain the newly registered target. |
| Build refresh | `cmake --build build --target nam_profile -j2` | 0 | `build-2.log`; the configured build refreshed its generated files. |
| Baseline test build | `cmake --build build --target NeuralAmpProfileArgumentsTest -j2` | 0 | `build-3.log` |
| RED on original code | `ctest -R NeuralAmpProfileArgumentsTest --output-on-failure` | 8 | `test-1.log`; 7 invalid-input cases failed, including zero blocks with actual exit code 0, expected 2. |
| Fixed build | `cmake --build build --target NeuralAmpProfileArgumentsTest -j2` | 0 | `build-4.log` |
| GREEN after fix | `ctest -R NeuralAmpProfileArgumentsTest --output-on-failure` | 0 | `test-2.log` |
| Remove production patch | `git diff -- plugins/NeuralAmp/tests/nam_profile.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-323.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-323.r1/logs/fix.patch` | 0 | The patch contains only the production file. |
| Rebuild without fix | `cmake --build build --target NeuralAmpProfileArgumentsTest -j2` | 0 | `build-5.log` |
| Canary RED without fix | `ctest -R NeuralAmpProfileArgumentsTest --output-on-failure` | 8 | `test-3.log`; zero blocks again returned 0 instead of 2. |
| Restore production patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-323.r1/logs/fix.patch` | 0 | Restored the same patch. |
| Rebuild with fix | `cmake --build build --target NeuralAmpProfileArgumentsTest -j2` | 0 | `build-6.log` |
| Canary GREEN with fix | `ctest -R NeuralAmpProfileArgumentsTest --output-on-failure` | 0 | `test-4.log` |
| Final build | `cmake --build build --target NeuralAmpProfileArgumentsTest -j2` | 0 | `build-7.log` |
| Final CTest | `ctest -R NeuralAmpProfileArgumentsTest --output-on-failure` | 0 | `test-5.log`; CTest ran 1 registered test, 1/1 passed. The executable covers 8 argument scenarios plus setup and cleanup. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |

## Files committed

- `plugins/NeuralAmp/tests/nam_profile.cpp`
- `tests/src/plugins/NeuralAmpProfileArgumentsTest.cpp`
- `tests/CMakeLists.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-323.md`
