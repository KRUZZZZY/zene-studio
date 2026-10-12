# Cluster new-p10-134: zero-sized FifoBuffer

Verdict: **FIXED**

## Evidence

- `include/FifoBuffer.h:42` validates the capacity in the first member initializer. `include/FifoBuffer.h:88` throws `std::invalid_argument` when the capacity is zero or negative, before the remaining semaphore or buffer initialization.
- `tests/src/core/ArrayVectorTest.cpp:68` checks both zero and negative capacity. It is part of the already configured `ArrayVectorTest` target, so no CMake reconfiguration was needed.
- Before the fix, `ArrayVectorTest::fifoBufferRejectsNonPositiveCapacity()` failed with: `Expected exception of type std::invalid_argument to be thrown but no exception caught`.

## Regression evidence

The ctest command was run from `build/tests`:

| Run | Source state | Exit | Result |
| --- | --- | ---: | --- |
| `test-1.log` | Original implementation | 8 | RED; the zero-capacity check failed. The QtTest summary was 39 passed, 1 failed. |
| `test-2.log` | Fix applied | 0 | GREEN; ctest reported 1/1 targets passed. |
| `test-3.log` | Product patch reversed | 8 | RED again with the same missing-exception assertion. |
| `test-4.log` | Product patch restored | 0 | GREEN again; ctest reported 1/1 targets passed. |

Affected-family count: **1/1 ctest targets** (`ArrayVectorTest`); its baseline QtTest run contained 40 cases (39 passed and 1 failed before the fix).

## Exact commands and exit codes

Builds were run from the worktree root. Each occurrence returned exit 0:

- `cmake --build build --target ArrayVectorTest` — logs `build-1.log`, `build-3.log`, `build-5.log`, `build-7.log`.
- `cmake --build build --target zene` — logs `build-2.log`, `build-4.log`, `build-6.log`, `build-8.log`.

Tests were run from `build/tests` with `QT_QPA_PLATFORM=offscreen`:

- `QT_QPA_PLATFORM=offscreen ctest -R '^ArrayVectorTest$' --output-on-failure` — exit 8 (`test-1.log`, RED).
- `QT_QPA_PLATFORM=offscreen ctest -R '^ArrayVectorTest$' --output-on-failure` — exit 0 (`test-2.log`, GREEN).
- `QT_QPA_PLATFORM=offscreen ctest -R '^ArrayVectorTest$' --output-on-failure` — exit 8 (`test-3.log`, reversed-patch RED).
- `QT_QPA_PLATFORM=offscreen ctest -R '^ArrayVectorTest$' --output-on-failure` — exit 0 (`test-4.log`, restored-patch GREEN).

Reverse-patch proof:

- `git diff -- include/FifoBuffer.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-134.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-134.r1/logs/fix.patch` — exit 0.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-134.r1/logs/fix.patch` — exit 0.

Ratchet gates (run after this report was written):

- `bash tests/complexity-gate.sh --check` — exit 0.
- `bash tests/file-length-gate.sh --check` — exit 0.

Files in the cluster commit:

- `include/FifoBuffer.h`
- `tests/src/core/ArrayVectorTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-134.md`
