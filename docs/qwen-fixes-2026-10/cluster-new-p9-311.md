# Cluster new-p9-311

Verdict: **FIXED**.

The NAM loader now rejects nonpositive model dimensions and dilations before they can produce invalid Eigen dimensions or history indices. It also checks per-layer and accumulated receptive-field arithmetic against the supported `int` range.

## Evidence

- `plugins/NeuralAmp/nam/NamModelLoader.cpp:269-297` validates dilation and receptive-field bounds, then rejects nonpositive input, head, channel, and bottleneck dimensions.
- `plugins/NeuralAmp/tests/nam_harness.cpp:307-405` creates weight-complete `.nam` fixtures for a valid control and seven malformed configurations.
- `plugins/NeuralAmp/tests/CMakeLists.txt:51-67` registers all eight cases as `NamLoaderTest_*` CTest tests beside the harness.
- `tests/CMakeLists.txt:4260-4269` mirrors those registrations into the main `build/tests` CTest tree used by the required test command.
- `tests/upstream-modifications.txt` records the inherited main test CMake registration change.

## Regression results

The affected-family count is **8 CTest tests**. With the loader patch removed, CTest failed 7 of 8 cases; `NamLoaderTest_head-size-zero` segfaulted, while the valid control passed. With the loader restored, all 8 passed.

The CTest commands below ran with `build/tests` as the working directory; other commands ran from the worktree root. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r2/logs`.

```text
cmake --build build --target zene neuralamp nam_harness -j12 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r2/logs/build-1.log' 2>&1; echo EXIT=$?
EXIT=0

ctest -R NamLoaderTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r2/logs/test-1.log' 2>&1; echo EXIT=$?
EXIT=0; 8 tests passed

git show HEAD -- plugins/NeuralAmp/nam/NamModelLoader.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r2/logs/fix.patch' && git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r2/logs/fix.patch'; echo EXIT=$?
EXIT=0; patch contains only the committed loader change

cmake --build build --target zene neuralamp nam_harness -j12 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r2/logs/build-2.log' 2>&1; echo EXIT=$?
EXIT=0; loader patch removed

ctest -R NamLoaderTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r2/logs/test-2.log' 2>&1; echo EXIT=$?
EXIT=8; 7 tests failed of 8, including the zero-head-size segfault

git checkout -- plugins/NeuralAmp/nam/NamModelLoader.cpp; echo EXIT=$?
EXIT=0

cmake --build build --target zene neuralamp nam_harness -j12 > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r2/logs/build-3.log' 2>&1; echo EXIT=$?
EXIT=0; loader fix restored

ctest -R NamLoaderTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r2/logs/test-3.log' 2>&1; echo EXIT=$?
EXIT=0; 8 tests passed

bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r2/logs/gate-7.log' 2>&1; echo EXIT=$?
EXIT=0

bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-311.r2/logs/gate-8.log' 2>&1; echo EXIT=$?
EXIT=0
```

The session did not expose the `fff grep` tool, so the logs were searched with `rg` for `error`, `FAILED`, and `Assertion`. The build and gate logs, plus the two green test logs, had no matches. `test-2.log` contains the expected `tests FAILED` summary and head-size segfault; it contains no `error` or `Assertion` matches.

## Files committed

The production fix and harness were committed earlier in `95dff2a47`:

- `plugins/NeuralAmp/nam/NamModelLoader.cpp`
- `plugins/NeuralAmp/tests/nam_harness.cpp`

This CTest-registration follow-up commits:

- `plugins/NeuralAmp/tests/CMakeLists.txt`
- `tests/CMakeLists.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-311.md`
