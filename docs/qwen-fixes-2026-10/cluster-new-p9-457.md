# Cluster new-p9-457

Verdict: FIXED

## Required evidence summary

| Evidence | Exact command or files | Result |
|---|---|---|
| RED, final regression with the product patch removed | `ctest -R '^AudioBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-457.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | Exit 8; 1 CTest target and 33 Qt cases (32 passed, regression failed) |
| GREEN, same regression after reapplying the patch | `ctest -R '^AudioBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-457.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | Exit 0; 1/1 CTest target passed |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-457.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | Exit 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-457.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | Exit 0 |
| Files in fix commit `b56b05a9b` | `src/common/SharedMemory.cpp`; `tests/src/core/AudioBufferTest.cpp`; `tests/upstream-modifications.txt`; `docs/qwen-fixes-2026-10/cluster-new-p9-457.md` | All four paths committed |

## Evidence

- `src/common/SharedMemory.cpp:113-126` checks that a POSIX mapping contains a full header and rejects a declared array byte count larger than `m_size - sizeof(Header)`. It unmaps before throwing so a failed constructor does not leak the mapping.
- `src/common/SharedMemory.cpp:224-237` uses `VirtualQuery` on Windows to get the mapped region size and applies the same header and length checks before reading the header.
- `tests/src/core/AudioBufferTest.cpp:173-190` creates a one-byte shared array, overwrites its in-band length with `UINT64_MAX`, and verifies that attach throws.
- The selected CTest family contains one CTest target. The RED run reported 32 Qt test cases passed and the regression failed, for 33 cases total. The GREEN run passed the target.

## Regression evidence

The first RED run was before the production change. The final regression was then run with the product patch removed, followed by a rebuild of `zene` and `AudioBufferTest`; it failed again. Reapplying the exact product patch, rebuilding both targets, and rerunning the same CTest command passed.

| Stage | Exact command | Exit |
|---|---|---:|
| Initial test build | `cmake --build build --target AudioBufferTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-457.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Initial RED | `ctest -R '^AudioBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-457.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Fixed build | `cmake --build build --target zene AudioBufferTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-457.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Intermediate GREEN | `ctest -R '^AudioBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-457.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Reverse product patch | `git diff -- src/common/SharedMemory.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-457.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-457.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild without fix | `cmake --build build --target zene AudioBufferTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-457.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Final regression RED | `ctest -R '^AudioBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-457.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc` | 8 |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-457.r1/logs/fix.patch; rc=$?; echo EXIT=$rc` | 0 |
| Rebuild with fix | `cmake --build build --target zene AudioBufferTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-457.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Final GREEN | `ctest -R '^AudioBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-457.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-457.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-457.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc` | 0 |

The Linux build and test were run here; the Windows branch was not compiled in this environment.

## Files in the commit

- `src/common/SharedMemory.cpp`
- `tests/src/core/AudioBufferTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-457.md`
