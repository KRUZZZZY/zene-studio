# Cluster new-p9-95

**Verdict: FIXED**

The finding was real. `ConvolutionKernel` now owns FFTW buffers and plans with RAII, checks all four allocations and both plans before transforming, and throws `std::bad_alloc` if any resource cannot be created. `ConvolutionReverbEffect::loadImpulse()` catches that failure and returns `false`, preserving the currently installed kernel and impulse path. Processing also tolerates the initial generated-hall load failing, so a missing kernel is not dereferenced.

## Evidence

| Location | Evidence |
| --- | --- |
| `plugins/ConvolutionReverb/ConvolutionKernel.h:69-97` | RAII deleters and owners for all FFTW buffers and plans. |
| `plugins/ConvolutionReverb/ConvolutionKernel.cpp:37-47` | Every allocation and plan is checked before partition transforms begin. |
| `plugins/ConvolutionReverb/ConvolutionReverb.cpp:198-209` | Kernel allocation failure becomes a `false` load result; installation and path update happen only on success. |
| `plugins/ConvolutionReverb/ConvolutionReverb.cpp:267-269` | The audio path safely handles the initial-load failure case. |
| `tests/src/core/DeviceDspTest.cpp:377-389` | Deterministic allocation and inverse-plan failure regression, including checks that acquired resources are released. |

The no-fix proof is in `test-4.log`: `DeviceDspTest::theConvolutionKernelRejectsFftwResourceFailures()` failed because no `std::bad_alloc` was thrown (10 other QtTest cases passed; none skipped). Reapplying the production patch and running the same ctest command passed `DeviceDspTest` (1/1 CTest target).

## Commands and exit codes

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-95.r1/logs`. Build commands ran from the worktree; ctest commands ran from `build/tests`.

| Log | Exact command | Exit |
| --- | --- | ---: |
| `build-1.log` | `cmake --build build --target DeviceDspTest --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-95.r1/logs/build-1.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | 0 |
| `test-1.log` | `ctest -R DeviceDspTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-95.r1/logs/test-1.log 2>&1; test_exit=$?; echo EXIT=$test_exit` | 8 (regression assertion failed; optional modules had not yet been built) |
| `build-2.log` | `cmake --build build --target DeviceDspTest convolutionreverb zene --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-95.r1/logs/build-2.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | 0 |
| `test-2.log` | `ctest -R DeviceDspTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-95.r1/logs/test-2.log 2>&1; test_exit=$?; echo EXIT=$test_exit` | 8 (existing console assertion; console module not built yet) |
| `build-3.log` | `cmake --build build --target deesser transientshaper vocoder dynamiceq convolutionreverb insertrecorder console --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-95.r1/logs/build-3.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | 0 |
| `test-3.log` | `ctest -R DeviceDspTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-95.r1/logs/test-3.log 2>&1; test_exit=$?; echo EXIT=$test_exit` | 0 |
| `fix.patch` | `git diff -- plugins/ConvolutionReverb/ConvolutionKernel.cpp plugins/ConvolutionReverb/ConvolutionKernel.h plugins/ConvolutionReverb/ConvolutionReverb.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-95.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-95.r1/logs/fix.patch; action_exit=$?; echo EXIT=$action_exit` | 0 |
| `build-4.log` | `cmake --build build --target DeviceDspTest convolutionreverb zene --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-95.r1/logs/build-4.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | 0 |
| `test-4.log` | `ctest -R DeviceDspTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-95.r1/logs/test-4.log 2>&1; test_exit=$?; echo EXIT=$test_exit` | 8 (expected regression assertion; 10 passed, 1 failed, 0 skipped) |
| `fix.patch` | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-95.r1/logs/fix.patch; action_exit=$?; echo EXIT=$action_exit` | 0 |
| `build-5.log` | `cmake --build build --target DeviceDspTest convolutionreverb zene --parallel 12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-95.r1/logs/build-5.log 2>&1; build_exit=$?; echo EXIT=$build_exit` | 0 |
| `test-5.log` | `ctest -R DeviceDspTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-95.r1/logs/test-5.log 2>&1; test_exit=$?; echo EXIT=$test_exit` | 0 (1/1 CTest target passed) |
| `gate-1.log` | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-95.r1/logs/gate-1.log 2>&1; gate_exit=$?; echo EXIT=$gate_exit` | 0 |
| `gate-2.log` | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-95.r1/logs/gate-2.log 2>&1; gate_exit=$?; echo EXIT=$gate_exit` | 0 |

## Files committed

- `plugins/ConvolutionReverb/ConvolutionKernel.cpp`
- `plugins/ConvolutionReverb/ConvolutionKernel.h`
- `plugins/ConvolutionReverb/ConvolutionReverb.cpp`
- `tests/src/core/DeviceDspTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-95.md`
