FIXED

## Decision and evidence

REAL: the existing test at `tests/src/core/SessionPlaybackTest.cpp:247` queued 300 cells without draining the command queue. `SessionScheduler::CommandQueueCapacity` is 256 (`include/SessionScheduler.h:218-220`); `publishSlotContent()` only reports whether the command was enqueued (`src/core/SessionPlayback.cpp:122-133`), and content reaches the fixed table when `processAudio()` drains it (`src/core/SessionScheduler.cpp:321-326`). The table itself is capped at 128 (`include/SessionPlayback.h:86`) and refuses an install at capacity (`src/core/SessionPlayback.cpp:82-85`).

The revised test at `tests/src/core/SessionPlaybackTest.cpp:245-280` drains after each 64-cell batch, asserts the configured 128-entry limit, then launches cells 128 and 129. Cell 128 renders pattern 4; cell 129 is playing but has no content. This exercises the queued audio-thread installation path and distinguishes table refusal from queue refusal.

This is a test coverage fix, and the product already enforces the contract. Therefore a normal pre-fix RED run against the actual product cannot be produced honestly. The current implementation passed the revised test. For sensitivity proof, I temporarily raised the table capacity to 256 and removed the explicit constant assertion, rebuilt, and reran the same behavior check: cell 129 rendered pattern 4 and the test failed. I restored both temporary changes and rebuilt before the final passing run. No production change remains.

## Evidence table

| Stage | Exact command | Exit | Result |
|---|---|---:|---|
| Initial revised test build | `cmake --build build --target SessionPlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3546.r1/logs/1-build-regression.log 2>&1; result=$?; echo EXIT=$result` | 0 | Test target built. |
| Initial revised test on current product | `ctest -R '^SessionPlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3546.r1/logs/2-test-baseline.log 2>&1; result=$?; echo EXIT=$result` | 0 | 1/1 CTest test passed; expected for this test-only finding. |
| Capacity-256 counterfactual build | `cmake --build build --target SessionPlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3546.r1/logs/3-build-capacity-counterfactual.log 2>&1; result=$?; echo EXIT=$result` | 0 | Rebuilt with temporary capacity 256. |
| Capacity-256 counterfactual test | `ctest -R '^SessionPlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3546.r1/logs/4-test-capacity-counterfactual.log 2>&1; result=$?; echo EXIT=$result` | 8 | Expected fail: `refused.patternId` was 4 instead of -1. |
| Restored test build | `cmake --build build --target SessionPlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3546.r1/logs/5-build-restored.log 2>&1; result=$?; echo EXIT=$result` | 0 | Restored 128-entry contract and rebuilt dependents. |
| Final family test | `ctest -R '^SessionPlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3546.r1/logs/6-test-final.log 2>&1; result=$?; echo EXIT=$result` | 0 | 1/1 CTest test passed. |
| Feature gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-3546 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/3546.r1/logs/7-feature-check.log 2>&1; result=$?; echo EXIT=$result` | 0 | All gates PASS, including complexity and file length. |

Product patch reversal step 4b is not applicable because no product file changed. The capacity-256 counterfactual above proves the behavioral regression test detects the reported failure mode. The test source was already registered in `tests/fork-sources.txt` and `tests/CMakeLists.txt`.

Files committed: `tests/src/core/SessionPlaybackTest.cpp`, `docs/qwen-fixes-2026-10/cluster-3546.md`.

## Self-check

- **Does the regression test FAIL with your product change reverted and PASS with it (exit codes)?** No product behavior change was made: the implementation already enforces 128. The coverage regression was proven with the counterfactual: capacity 256 made CTest fail with EXIT 8 because cell 129 rendered pattern 4; restoring capacity 128 made the same family pass with EXIT 0. The original implementation also passes the revised test with EXIT 0. The recorded failing assertion is `tests/src/core/SessionPlaybackTest.cpp:278` in the counterfactual log; the final assertion is at line 279.
- **Is the test deterministic?** Yes. It injects `clock(0)` and calls `processAudio()` synchronously after each batch and before inspection (`tests/src/core/SessionPlaybackTest.cpp:259,266,272`). It uses no wall-clock timing, sleeps, or races.
- **Was the cause and every relevant caller/sibling checked?** Yes. The test previously only filled the 256-command queue; `publishSlotContent()` enqueues at `src/core/SessionPlayback.cpp:122-133`, `drainCommands()` applies content at `src/core/SessionScheduler.cpp:321-326`, and `applySlotContent()` calls the table at `src/core/SessionPlayback.cpp:137-145`. The grid publisher uses the same queue API at `src/core/SessionPlayback.cpp:204-240`. The limit and refusal remain at `include/SessionPlayback.h:86` and `src/core/SessionPlayback.cpp:82-85`; the fix changes only the test of this path.
- **Is there collateral damage to old projects, saved data, or legitimate callers?** No product code or serialization changed, and no new limit was added. The existing 128-entry contract is only asserted and exercised more completely.
- **Does the ALREADY-FIXED/FALSE-POSITIVE evidence question apply?** No. The verdict is REAL: the old test only established enqueue refusal. The 256 queue versus 128 table is evidenced at `include/SessionScheduler.h:220` and `include/SessionPlayback.h:86`, and the table is reached only when `processAudio()` drains the command at `src/core/SessionScheduler.cpp:321-326`.
- **Did the focused test pass on the restored implementation, and how many CTest tests ran?** Yes, 1/1 (`SessionPlaybackTest`), EXIT 0.
- **Did the feature gates pass?** Yes, feature check EXIT 0; its table is in the log above.
- **Did the final change add product behavior or require registration changes?** No. Only the existing registered test and this report are changed.
