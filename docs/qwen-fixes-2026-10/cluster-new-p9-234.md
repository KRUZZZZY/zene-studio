# Cluster new-p9-234

**Verdict: FALSE-POSITIVE**

The reported expressions are at [LocklessAllocator.cpp:130](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-234/src/core/LocklessAllocator.cpp:130) and [LocklessAllocator.cpp:158](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-234/src/core/LocklessAllocator.cpp:158). The finding assumes that shifting signed `1` into the sign bit is undefined. This project builds as C++20 (`-std=gnu++20`); C++20 specifies left shift as the unique result congruent modulo `2^N`, with undefined behavior only when the shift count is negative or at least the result width ([WG21 draft N4878, §7.6.7](https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/n4878.pdf)). The allocator's shift count is 31 for a 32-bit `int`, so both operations are defined.

A temporary test compiled `LocklessAllocator.cpp` itself with `-fsanitize=shift -fno-sanitize-recover=shift` and exercised allocating all 32 slots, freeing the final slot, and allocating it again. The original implementation passed 1/1 tests with no sanitizer report. The temporary test and its CMake wiring were removed; no production behavior was changed.

## Evidence

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| RED | N/A for a false-positive finding. | N/A | The original implementation did not trigger the alleged undefined behavior. |
| GREEN | N/A; no production fix was made. | N/A | — |
| Temporary regression build | `cmake --build build --target zene LocklessAllocatorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-234.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 | Built `zene` and the instrumented regression target. |
| Original-code boundary test (run from `build/tests`) | `ctest -R LocklessAllocatorTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-234.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 | 1/1 passed; no shift-sanitizer report. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-234.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-234.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 | Passed. |

Additional build attempts: `cmake --build build --target zene LocklessAllocatorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-234.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` exited 2 because the new target had not yet been generated. `cmake --build build --target zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-234.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` exited 0 and regenerated the build files. The first test-source build command, `cmake --build build --target LocklessAllocatorTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"`, exited 2 on Qt's `slots` macro collision; that captured log was moved to the requested log folder as `build-3.log`. After renaming the local variable, the successful build above and the 1/1 CTest run completed.

**Committed file:** `docs/qwen-fixes-2026-10/cluster-new-p9-234.md`.
