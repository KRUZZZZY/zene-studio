# Cluster new-p9-293: MonoDelay array deallocation

**Verdict: FIXED**

`MonoDelay` allocates its sample buffer with `new sample_t[]` at
`plugins/Flanger/MonoDelay.cpp:78`. The destructor and `setSampleRate()` now
release that buffer with `delete[]` at lines 51 and 74. The regression at
`tests/src/core/MonoDelayTest.cpp:29-37` checks both release sites and fails on
the original scalar deletes.

The supplied CTest names were not registered in this build: the listing command
below found zero tests. I registered `MonoDelayTest`; its affected-family run
executes one test. `fff` was not available in this session, so build and test
logs were searched with targeted `rg` matches.

## Regression and gates

| Check | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Initial test, before fix | `ctest -R '^MonoDelayTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-293.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | RED; 1 test failed at the array-delete assertion |
| Test after fix | `ctest -R '^MonoDelayTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-293.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | GREEN; 1/1 passed |
| Test after reversing product patch | `ctest -R '^MonoDelayTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-293.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | 8 | RED; the same assertion failed |
| Test after reapplying product patch | `ctest -R '^MonoDelayTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-293.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | GREEN; 1/1 passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-293.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-293.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 | Passed |

## Build and proof commands

| Command | Exit |
| --- | ---: |
| `cmake --build build --target MonoDelayTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-293.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 2 (the generated build files did not yet contain this new target) |
| `cmake --build build --target AudioBufferTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-293.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `cmake --build build --target MonoDelayTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-293.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-293.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `cmake --build build --target flanger MonoDelayTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-293.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `git diff -- plugins/Flanger/MonoDelay.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-293.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-293.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| `cmake --build build --target flanger MonoDelayTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-293.r1/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-293.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| `cmake --build build --target flanger MonoDelayTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-293.r1/logs/build-7.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

CTest discovery for the provided names was a listing only, not a test run:

`ctest -N -R '^(MonoDelay|FlangerEffect)$' > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-293.r1/logs/test-list-1.log 2>&1; rc=$?; echo EXIT=$rc` — exit 0, `Total Tests: 0`.

## Files in the commit

- `plugins/Flanger/MonoDelay.cpp`
- `tests/src/core/MonoDelayTest.cpp`
- `tests/CMakeLists.txt`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-293.md`
