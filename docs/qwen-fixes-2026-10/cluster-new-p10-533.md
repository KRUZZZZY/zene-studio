# Cluster new-p10-533

**Verdict: FIXED.** `sp_createn` accepted `nchan <= 0` and dereferenced unchecked allocation results. It now rejects invalid counts before allocating, returns `SP_NOT_OK` on either allocation failure, frees the partially allocated state if `calloc` fails, and returns `SP_OK` after successful initialization.

## Evidence

- [base.c](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-533/plugins/ReverbSC/base.c:9): validates the output pointer and channel count, checks both allocations, cleans up the first allocation on second-allocation failure, and initializes the output only after success.
- [ReverbSCCreateTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-533/tests/src/plugins/ReverbSCCreateTest.cpp:51): covers zero and negative counts, injected `malloc` and `calloc` failures, and successful channel creation. Allocator failures are isolated in child processes.
- [ReverbSCCreateTestAllocator.c](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-533/tests/src/plugins/ReverbSCCreateTestAllocator.c:1): injects deterministic failures while compiling the production `base.c` implementation into the test.
- The test is registered in [tests/CMakeLists.txt](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-533/tests/CMakeLists.txt:416). New sources are listed in `tests/fork-sources.txt` and `tests/all-sources.txt`; inherited changes are recorded in `tests/upstream-modifications.txt`.

The affected CTest family ran **1/1 test executable**, containing four Qt test cases.

## RED/GREEN and gates

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-533.r1/logs`. The fff grep tool was not exposed in this session; log matches were checked with `rg`.

| Evidence | Command (working directory) | Exit | Log |
|---|---|---:|---|
| RED on original code | `ctest -R ReverbSCCreateTest --output-on-failure` (`build/tests`) | 8 | `test-1.log` |
| GREEN with fix | `ctest -R ReverbSCCreateTest --output-on-failure` (`build/tests`) | 0 | `test-2.log` |
| Reverse product patch | `git diff -- plugins/ReverbSC/base.c > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-533.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-533.r1/logs/fix.patch` | 0 | `fix.patch` |
| Mutation rebuild | `cmake --build build --target zene reverbsc ReverbSCCreateTest` | 0 | `build-5.log` |
| RED with product patch reversed | `ctest -R ReverbSCCreateTest --output-on-failure` (`build/tests`) | 8 | `test-3.log` |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-533.r1/logs/fix.patch` | 0 | `fix.patch` |
| Restore rebuild | `cmake --build build --target zene reverbsc ReverbSCCreateTest` | 0 | `build-6.log` |
| GREEN after restore | `ctest -R ReverbSCCreateTest --output-on-failure` (`build/tests`) | 0 | `test-4.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-2.log` |

The original-code RED reported the invalid-count output pointer remained non-null, the forced `malloc` failure child crashed, the forced `calloc` failure retained a state object, and successful creation returned the failure status. The same four cases failed after the mutation reversal and passed after restoration.

Build setup attempts, retained in the same log folder: `cmake --build build --target zene ReverbSCCreateTest` exited 2 (`build-1.log`; `zene` completed, but the newly generated test target was not yet available to that invocation); the next `cmake --build build --target ReverbSCCreateTest` exited 2 (`build-2.log`, unresolved C/C++ linkage in the test declaration). After correcting the test's C linkage, `cmake --build build --target ReverbSCCreateTest` exited 0 (`build-3.log`). The fixed-code build `cmake --build build --target zene reverbsc ReverbSCCreateTest` exited 0 (`build-4.log`).

## Files committed

- `plugins/ReverbSC/base.c`
- `tests/CMakeLists.txt`
- `tests/src/plugins/ReverbSCCreateTest.cpp`
- `tests/src/plugins/ReverbSCCreateTestAllocator.c`
- `tests/fork-sources.txt`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-533.md`
