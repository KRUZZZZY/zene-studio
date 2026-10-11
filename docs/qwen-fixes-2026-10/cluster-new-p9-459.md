# Cluster new-p9-459: shared-memory array size overflow

**Verdict: FIXED**

The finding is real. Both `SharedMemory<T[]>::create` overloads multiplied the element count by `sizeof(T)` without checking for overflow. The wrapped byte count could create a mapping smaller than the requested array. `checkedSizeBytes` now checks the count against `SIZE_MAX / sizeof(T)` and throws `std::length_error` before either overload creates the mapping.

## File evidence

| File and lines | Evidence |
|---|---|
| `include/SharedMemory.h:215-224` | Both `create` overloads call the checked conversion before constructing shared memory. |
| `include/SharedMemory.h:245-252` | Rejects an overflowing count and returns the byte count otherwise. |
| `tests/src/core/AudioBufferTest.cpp:169-189` | Separate keyed and unkeyed regression cases expect `std::length_error`. |
| `tests/upstream-modifications.txt:1054-1055` | Records the inherited production and test changes. |

## RED/GREEN and affected-family evidence

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Initial RED (unkeyed slot) | `ctest -R '^AudioBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-459.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 | One CTest case ran; the expected exception was absent. |
| RED (both slots) | `ctest -R '^AudioBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-459.r1/logs/test-2.log 2>&1; echo EXIT=$?` | 8 | One CTest case ran; QtTest reported 32 passed and both overflow cases failed. |
| GREEN | `ctest -R '^AudioBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-459.r1/logs/test-3.log 2>&1; echo EXIT=$?` | 0 | AudioBufferTest passed, 1/1 CTest cases. |
| Related family | `ctest -R '^RemotePluginClientE2ETest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-459.r1/logs/test-4.log 2>&1; echo EXIT=$?` | 0 | RemotePluginClientE2ETest passed, 1/1 CTest cases. |
| Reverse-patch RED | `ctest -R '^AudioBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-459.r1/logs/test-5.log 2>&1; echo EXIT=$?` | 8 | After removing the product fix and rebuilding, both overflow cases failed again (32 passed, 2 failed). |
| Restored-patch GREEN | `ctest -R '^AudioBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-459.r1/logs/test-6.log 2>&1; echo EXIT=$?` | 0 | After restoring the product fix and rebuilding, AudioBufferTest passed, 1/1 CTest cases. |

## Build and proof commands

All commands below used the stated log folder and captured the shell exit code:

| Exact command | Exit |
|---|---:|
| `cmake --build build --target AudioBufferTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-459.r1/logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| `cmake --build build --target AudioBufferTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-459.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| `cmake --build build --target zene AudioBufferTest RemotePluginClientE2ETest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-459.r1/logs/build-3.log 2>&1; echo EXIT=$?` | 0 |
| `git diff -- include/SharedMemory.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-459.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-459.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| `cmake --build build --target zene AudioBufferTest RemotePluginClientE2ETest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-459.r1/logs/build-4.log 2>&1; echo EXIT=$?` | 0 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-459.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| `cmake --build build --target zene AudioBufferTest RemotePluginClientE2ETest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-459.r1/logs/build-5.log 2>&1; echo EXIT=$?` | 0 |

## Ratchet gates

| Exact command | Exit |
|---|---:|
| `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-459.r1/logs/gate-1.log 2>&1; echo EXIT=$?` | 0 |
| `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-459.r1/logs/gate-2.log 2>&1; echo EXIT=$?` | 0 |

## Files in the commit

- `include/SharedMemory.h`
- `tests/src/core/AudioBufferTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-459.md`
