# Cluster new-p10-97: negative DAWproject timeline positions

**Verdict: FIXED**

## Finding and change

The finding was real. `ticksFromBeats()` converted negative beats into negative ticks even though the DAWproject writer only supports finite, non-negative time values. The reader now sets `ReadState::error` and returns a safe placeholder for a negative value, causing the document read to fail. `pointTime()` also returns false immediately when that conversion records an error.

The regression covers a negative clip time, note time, and tempo map-point time.

## File:line evidence

- `src/core/DawProjectReadShared.h:189-206` — shared beat conversion rejects negative values before tick conversion.
- `src/core/DawProjectReadTracks.cpp:93-98,129-137` — note and clip time fields use the shared conversion.
- `src/core/DawProjectRead.cpp:113-124` — automation map-point time uses the shared conversion and now propagates its refusal immediately.
- `src/core/DawProjectWrite.cpp:126,143-175` — the writer accepts only finite non-negative times and applies that rule to clip, note, and map-point times.
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp:1275-1309` — regression fixtures for negative clip, note, and tempo map-point times.

## Verification

The affected family contains **1 registered ctest**: `DawProjectInterchangeRoundTripTest`.

| Evidence | Exact command | Exit | Result |
|---|---|---:|---|
| Original-code RED | `cd build/tests && ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-97.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result` | 8 | Regression failed because the negative clip time was accepted; QtTest reported 34 passed, 1 failed. |
| Final reversal RED (4b) | `cd build/tests && ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-97.r1/logs/test-6.log 2>&1; result=$?; echo EXIT=$result` | 8 | With both production changes reverted and rebuilt, the same negative-clip assertion failed; 34 passed, 1 failed. |
| Final GREEN | `cd build/tests && ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-97.r1/logs/test-7.log 2>&1; result=$?; echo EXIT=$result` | 0 | 1/1 registered ctest passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-97.r1/logs/gate-3.log 2>&1; result=$?; echo EXIT=$result` | 0 | Passed. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-97.r1/logs/gate-4.log 2>&1; result=$?; echo EXIT=$result` | 0 | Passed. |

Build commands used for the baseline and final reversal proof:

```text
cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-97.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result
EXIT=0

git diff -- src/core/DawProjectReadShared.h src/core/DawProjectRead.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-97.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-97.r1/logs/fix.patch; result=$?; echo EXIT=$result
EXIT=0

cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-97.r1/logs/build-7.log 2>&1; result=$?; echo EXIT=$result
EXIT=0

git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-97.r1/logs/fix.patch; result=$?; echo EXIT=$result
EXIT=0

cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-97.r1/logs/build-8.log 2>&1; result=$?; echo EXIT=$result
EXIT=0
```

## Files committed

- `src/core/DawProjectRead.cpp`
- `src/core/DawProjectReadShared.h`
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-97.md`
