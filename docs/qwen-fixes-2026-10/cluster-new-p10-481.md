# Cluster new-p10-481

Verdict: **FIXED**

`f_cnt_t` is 64-bit, while the audio vectors take `std::size_t` element counts. The old expressions could wrap in the frame/channel products, the interleaved frame product, or the byte-total sum. `AudioBuffer.cpp` now converts frame counts with a checked conversion and uses checked `std::size_t` multiplication/addition. Overflow throws `std::length_error` before buffer resizing; `addGroup()` computes both required buffer lengths before clearing a shared-memory resource.

Evidence in code:

- `src/core/AudioBuffer.cpp:40` checks frame-count conversion; `:53` and `:63` check multiplication and addition.
- `src/core/AudioBuffer.cpp:138` checks interleaved allocation; `:143` and `:148` check `allocationSize()`; `:176`, `:184`, and `:200` check and apply the source/interleaved lengths in `addGroup()`.
- `tests/src/core/AudioBufferTest.cpp:153`, `:160`, `:167`, and `:175` cover multiplication overflow in size reporting, addition overflow in the byte total, source-buffer sizing, and interleaved-buffer sizing.

## Verification evidence

| Check | Exact command | Exit | Result |
|---|---|---:|---|
| Original-code RED, before the fix (3 regression cases) | `ctest -R '^AudioBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/test-1.log; exit "$rc"` (cwd: `build/tests`) | 8 | The size-report, `addGroup`, and interleaved overflow assertions each failed because no exception was thrown. |
| Intermediate GREEN after the fix (3 regression cases) | `ctest -R '^AudioBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/test-2.log; exit "$rc"` (cwd: `build/tests`) | 0 | The three initial overflow cases passed. |
| Full regression GREEN after the fix (4 cases) | `ctest -R '^AudioBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/test-3.log; exit "$rc"` (cwd: `build/tests`) | 0 | All four overflow cases passed. |
| Negative control with the production patch removed | `ctest -R '^AudioBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/test-4.log; exit "$rc"` (cwd: `build/tests`) | 8 | All four cases failed again, including the addition-overflow case. |
| Same regression after restoring the production patch | `ctest -R '^AudioBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/test-5.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/test-5.log; exit "$rc"` (cwd: `build/tests`) | 0 | All four cases passed again. |
| Affected-family run | `ctest -R '^(AudioBufferTest|SidechainInputsTest|RoutingGraphLiveTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/test-6.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/test-6.log; exit "$rc"` (cwd: `build/tests`) | 0 | 3 ctest targets passed; 0 failed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/gate-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/gate-1.log; exit "$rc"` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/gate-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/gate-2.log; exit "$rc"` | 0 | Passed. |

Build commands for the regression runs all exited 0:

- `cmake --build build --target zene AudioBufferTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/build-1.log; exit "$rc"`
- `cmake --build build --target zene AudioBufferTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/build-2.log; exit "$rc"`
- `cmake --build build --target zene AudioBufferTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/build-3.log; exit "$rc"`
- `cmake --build build --target zene AudioBufferTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/build-4.log; exit "$rc"`
- `cmake --build build --target zene AudioBufferTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/build-5.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/build-5.log; exit "$rc"`
- `cmake --build build --target zene AudioBufferTest SidechainInputsTest RoutingGraphLiveTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/build-6.log 2>&1; rc=$?; echo EXIT=$rc >> /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/build-6.log; exit "$rc"`

Each command above exited 0. The other recorded test commands were the same `AudioBufferTest` ctest invocation redirected to `test-2.log` (exit 0) and `test-6.log` as listed in the table.

Negative-control patch commands, both exit 0:

- `git diff -- src/core/AudioBuffer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/fix.patch`
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-481.r1/logs/fix.patch`

## Files committed

- `src/core/AudioBuffer.cpp`
- `tests/src/core/AudioBufferTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p10-481.md`
