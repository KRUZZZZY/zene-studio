# Cluster new-p10-225

Verdict: **FIXED**

`src/core/LocklessAllocator.cpp:58` now rejects a zero element size with `std::invalid_argument` before pool allocation. Previously, `alloc()` could return the zero-size pool address and `free()` reached the modulo at `src/core/LocklessAllocator.cpp:148` with a zero divisor. The regression is `tests/src/core/ArrayVectorTest.cpp:68`.

The dedicated target was unavailable in the configured tree, and the build guard rejected a full build. To honor the no-configure constraint, the regression is a new case in the existing `ArrayVectorTest` target; no CMake files changed.

## Evidence

| Check | Command | Result |
|---|---|---|
| RED on original implementation | `ctest -R ArrayVectorTest --output-on-failure` from `build/tests` | Exit 8; 1/1 ctest target ran. `zeroElementSizeConstructorTest` failed because no `std::invalid_argument` was thrown (39 Qt test cases passed, 1 failed). |
| GREEN with fix | `ctest -R ArrayVectorTest --output-on-failure` from `build/tests` | Exit 0; 1/1 ctest target passed. |
| Patch reversal proof | Reversed the saved production patch, rebuilt `ArrayVectorTest`, and reran the same ctest command | Reverse/apply exit 0; rebuild exit 0; ctest exit 8 with the same failing assertion. Reapplied the patch, rebuilt, and reran: apply exit 0, rebuild exit 0, ctest exit 0. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | Exit 0. |
| File-length gate | `bash tests/file-length-gate.sh --check` | Exit 0. |

## Commands and logs

Commands below were run from the worktree unless marked `build/tests`. Build and test output was redirected to the named log in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-225.r1/logs`.

Target discovery before choosing the existing suite:

- `cmake --build build --target LocklessAllocatorTest` — exit 2 (`build-1.log`; target unavailable).
- `cmake --build build/tests --target help` — exit 0 (`build-2.log`).
- `cmake --build build/tests --target LocklessAllocatorTest` — exit 2 (`build-3.log`; target unavailable).
- `cmake --build build/tests` — exit 97 (`build-4.log`; `bunny-guard` requires a named target).

RED, fix, and GREEN:

- `cmake --build build/tests --target ArrayVectorTest` — exit 0 (`build-5.log`, before fix).
- `cmake --build build --target zene` — exit 0 (`build-6.log`, before RED).
- `ctest -R ArrayVectorTest --output-on-failure` from `build/tests` — exit 8 (`test-1.log`, RED).
- `cmake --build build/tests --target ArrayVectorTest` — exit 0 (`build-7.log`, with fix).
- `cmake --build build --target zene` — exit 0 (`build-8.log`, with fix).
- `ctest -R ArrayVectorTest --output-on-failure` from `build/tests` — exit 0 (`test-2.log`, GREEN).

Patch reversal proof:

- `git diff -- src/core/LocklessAllocator.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-225.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-225.r1/logs/fix.patch` — exit 0.
- `cmake --build build/tests --target ArrayVectorTest` — exit 0 (`build-9.log`, with fix reversed).
- `ctest -R ArrayVectorTest --output-on-failure` from `build/tests` — exit 8 (`test-3.log`, same RED assertion).
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-225.r1/logs/fix.patch` — exit 0.
- `cmake --build build/tests --target ArrayVectorTest` — exit 0 (`build-10.log`, fix restored).
- `cmake --build build --target zene` — exit 0 (`build-11.log`, fix restored).
- `ctest -R ArrayVectorTest --output-on-failure` from `build/tests` — exit 0 (`test-4.log`, proof GREEN).

Ratchet gates:

- `bash tests/complexity-gate.sh --check` — exit 0 (`gate-1.log`).
- `bash tests/file-length-gate.sh --check` — exit 0 (`gate-2.log`).

## Files in the commit

- `src/core/LocklessAllocator.cpp`
- `tests/src/core/ArrayVectorTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-225.md`
