# Cluster new-p9-389

Verdict: FIXED

`RingBuffer` stored frame offsets in unsigned `f_cnt_t`, so negative reads and
movement wrapped before modulo. Its millisecond offset conversion also returned
an unsigned frame count. Frame-based `movePosition()` and both frame-based
`read()` overloads now accept signed offsets and normalize them into the ring
range. Millisecond movement and reads convert through a signed frame offset.
Write offsets remain unchanged because their API documents nonnegative values.

## File and line evidence

- `include/RingBuffer.h:96,118,131` declares signed frame offsets;
  `include/RingBuffer.h:217` converts millisecond offsets to signed frames.
- `src/core/RingBuffer.cpp:37` normalizes signed offsets, including the minimum
  signed value without negating it directly. `src/core/RingBuffer.cpp:128,163,189`
  use that normalization for movement and both read forms;
  `src/core/RingBuffer.cpp:136,185,211` route millisecond offsets through the
  signed conversion.
- `tests/src/core/RecordRingBufferTest.cpp:123` covers negative frame reads,
  length-limited reads, frame movement, and millisecond reads and movement.
- `tests/upstream-modifications.txt:1054,1055` records the inherited production
  file changes and their regression test.

## RED/GREEN evidence

The original production code was built before the RED run. The test failed in
`RingBuffer_NegativeOffsetsMoveBackward()`; after the fix, the same ctest family
passed. The reverse-patch proof repeated the failure with the final regression
test, then restored the patch and passed again.

| Stage | Exact command | Exit |
|---|---|---:|
| Original-code build | `cmake --build build --target zene RecordRingBufferTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-389.r1/logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| Original-code RED | `ctest -R '^RecordRingBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-389.r1/logs/test-1.log 2>&1; echo EXIT=$?` | 8 |
| Reverse-patch build | `cmake --build build --target zene RecordRingBufferTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-389.r1/logs/build-7-reverted.log 2>&1; echo EXIT=$?` | 0 |
| Reverse-patch RED | `ctest -R '^RecordRingBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-389.r1/logs/test-6-reverted.log 2>&1; echo EXIT=$?` | 8 |
| Restored build | `cmake --build build --target zene RecordRingBufferTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-389.r1/logs/build-8-restored.log 2>&1; echo EXIT=$?` | 0 |
| Final GREEN | `ctest -R '^RecordRingBufferTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-389.r1/logs/test-7-restored.log 2>&1; echo EXIT=$?` | 0 |
| Affected ctest family | One test ran; `100% tests passed, 0 tests failed out of 1`. | 1/1 |

The reverse patch was created and applied with
`git diff -- include/RingBuffer.h src/core/RingBuffer.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-389.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-389.r1/logs/fix.patch`
(exit 0). It was restored with
`git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-389.r1/logs/fix.patch; echo EXIT=$?`
(exit 0).

## Gates

| Gate | Exact command | Exit |
|---|---|---:|
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-389.r1/logs/gate-1-complexity.log 2>&1; echo EXIT=$?` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-389.r1/logs/gate-2-file-length.log 2>&1; echo EXIT=$?` | 0 |

## Files committed

- `include/RingBuffer.h`
- `src/core/RingBuffer.cpp`
- `tests/src/core/RecordRingBufferTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-389.md`
