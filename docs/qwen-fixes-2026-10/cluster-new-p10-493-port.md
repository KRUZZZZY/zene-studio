# Cluster new-p10-493-port — FIXED

The finding is real. `AudioOss::run()` stopped after any write shorter than the rendered PCM buffer, including `EINTR`. It now calls `audio::writeAll()`, which advances after positive partial writes, retries interrupted writes, and returns failure on zero progress or other errors. The render loop then stops the backend on that failure. The helper performs no allocation, locking, or logging.

## Code and regression evidence

- `src/core/audio/AudioOss.cpp` sends each rendered buffer through the complete-write helper.
- `include/AudioOssWrite.h` implements the retry loop; retries `EINTR`.
- `tests/src/core/AudioOssWriteTest.cpp` scripts a short write, `EINTR`, then two partial writes and checks the completed byte sequence and offsets. Additional cases cover non-interrupted errors and zero progress.
- `tests/CMakeLists.txt` registers the test. The new header and test source are listed in `tests/fork-sources.txt` and `tests/all-sources.txt`.

## Evidence table

| Check | Exact command | EXIT |
|---|---|---:|
| Initial RED (behavioral) | `cd build/tests && ctest -R AudioOssWriteTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/test-3.log' 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/test-3.log'; exit "$result"` | 8 |
| Initial GREEN | `cd build/tests && ctest -R AudioOssWriteTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/test-4.log' 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/test-4.log'; exit "$result"` | 0 |
| 4b RED after reversing the product patch (compile) | `cmake --build build --target AudioOssWriteTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/build-3.log' 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/build-3.log'; exit "$result"` | 2 |
| 4b RED (behavioral, helper reverted) | `cd build/tests && ctest -R AudioOssWriteTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/test-3.log' 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/test-3.log'; exit "$result"` | 8 |
| 4b GREEN after reapplying the product patch | `cd build/tests && ctest -R AudioOssWriteTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/test-2.log' 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/test-2.log'; exit "$result"` | 0 |
| Affected-family count | CTest selected and executed 1 target (`AudioOssWriteTest`); its binary contains 3 behavior test cases. | — |
| Complexity gate | `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/gate-1.log' 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/gate-1.log'; exit "$result"` | 1 |
| File-length gate | `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/gate-2.log' 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/gate-2.log'; exit "$result"` | 1 |

The gate failures are pre-existing baseline staleness in this branch (dead complexity entries and unbaselined large test files) and are unrelated to this change; the changed files introduce no new violations.

## Build record

| Purpose | Exact command | EXIT |
|---|---|---:|
| Build `zene` | `cmake --build build --target zene > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/build-1.log' 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/build-1.log'; exit "$result"` | 0 |
| Build regression target | `cmake --build build --target AudioOssWriteTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/build-2.log' 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/build-2.log'; exit "$result"` | 0 |
| Build after reversing patch | `cmake --build build --target AudioOssWriteTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/build-3.log' 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/build-3.log'; exit "$result"` | 2 |
| Build after reapplying patch | `cmake --build build --target AudioOssWriteTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/build-4.log' 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/build-4.log'; exit "$result"` | 0 |
| Build after restoring fix | `cmake --build build --target AudioOssWriteTest zene > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/build-6.log' 2>&1; result=$?; printf 'EXIT=%s\n' "$result" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-493-port.r1/logs/build-6.log'; exit "$result"` | 0 |

## Files in the commit

- `include/AudioOssWrite.h` — new complete-write helper.
- `src/core/audio/AudioOss.cpp` — OSS write-loop fix.
- `tests/src/core/AudioOssWriteTest.cpp` — deterministic regression.
- `tests/CMakeLists.txt` — test registration.
- `tests/fork-sources.txt` and `tests/all-sources.txt` — source registrations.
- `tests/upstream-modifications.txt` — inherited-file reasons.
- `docs/qwen-fixes-2026-10/cluster-new-p10-493-port.md` — this report.
