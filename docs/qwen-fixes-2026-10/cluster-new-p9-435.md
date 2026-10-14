# Cluster new-p9-435

Verdict: **FIXED**

`src/core/SmfInterchangeReader.cpp:189-194` now refuses an `FF 58` payload whose numerator is zero, before `readTimeSignaturePayload` marks the candidate as a time signature at line 206. The refusal includes a time-signature reason. The regression at `tests/src/core/SmfInterchangeRoundTripTest.cpp:395-418` creates a valid SMF with `nn = 0`, checks that `interchange.smf_read` returns `InvalidArgs`, and confirms import leaves the tempo map empty.

## Regression evidence

| Check | Exact command | Result |
| --- | --- | --- |
| RED before the fix | `cd build/tests && ctest -R SmfInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-435.r1/logs/test-1.log 2>&1; echo EXIT=$?` | Exit 8. `aZeroMeterNumeratorIsRefused`: `!read.ok` returned false; the zero numerator was accepted. 15 passed, 1 failed. |
| RED with fix removed (proof) | `cd build/tests && ctest -R SmfInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-435.r1/logs/test-4.log 2>&1; echo EXIT=$?` | Exit 8, same failing assertion; 15 passed, 1 failed. |
| GREEN final affected family | `cd build/tests && ctest -R SmfInterchange --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-435.r1/logs/test-5.log 2>&1; echo EXIT=$?` | Exit 0; 2/2 CTest tests passed (`SmfInterchangeTest`, `SmfInterchangeRoundTripTest`). |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-435.r1/logs/gate-3.log 2>&1; echo EXIT=$?` | Exit 0. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-435.r1/logs/gate-4.log 2>&1; echo EXIT=$?` | Exit 0. |

## Build and reversal commands

Every build and ctest command below wrote its output to the named log. Exit codes are the values printed by the command.

- `cmake --build build --target SmfInterchangeRoundTripTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-435.r1/logs/build-1.log 2>&1; echo EXIT=$?` — exit 0.
- `cmake --build build --target SmfInterchangeRoundTripTest zene -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-435.r1/logs/build-2.log 2>&1; echo EXIT=$?` — exit 0.
- `cd build/tests && ctest -R SmfInterchange --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-435.r1/logs/test-2.log 2>&1; echo EXIT=$?` — exit 8 because the configured build did not yet contain the `SmfInterchangeTest` executable; ctest reported that test `Not Run`. The target was built before the successful family run.
- `cmake --build build --target SmfInterchangeTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-435.r1/logs/build-3.log 2>&1; echo EXIT=$?` — exit 0.
- `cd build/tests && ctest -R SmfInterchange --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-435.r1/logs/test-3.log 2>&1; echo EXIT=$?` — exit 0.
- `git diff -- src/core/SmfInterchangeReader.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-435.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-435.r1/logs/fix.patch; echo EXIT=$?` — exit 0; the production fix was removed for the proof run.
- `cmake --build build --target SmfInterchangeRoundTripTest zene -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-435.r1/logs/build-4.log 2>&1; echo EXIT=$?` — exit 0.
- `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-435.r1/logs/fix.patch; echo EXIT=$?` — exit 0; the production fix was restored.
- `cmake --build build --target SmfInterchangeRoundTripTest SmfInterchangeTest zene -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-435.r1/logs/build-5.log 2>&1; echo EXIT=$?` — exit 0.

The requested `fff grep` tool was not available in this session, so saved logs were searched with targeted `rg -n` queries.

## Files in this fix commit

- `src/core/SmfInterchangeReader.cpp`
- `tests/src/core/SmfInterchangeRoundTripTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-435.md`
