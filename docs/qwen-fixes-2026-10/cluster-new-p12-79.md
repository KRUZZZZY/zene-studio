# Cluster new-p12-79: Negative note durations are accepted

**Verdict: FIXED**

`parseNotes` now checks the raw note duration before tick conversion. A negative value records a reader refusal and is not appended to the model. Checking before conversion also rejects small negative beat values that would round to zero ticks.

## File and line evidence

- [src/core/DawProjectReadTracks.cpp:93](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-79/src/core/DawProjectReadTracks.cpp:93) reads the duration in beats; lines 94–100 refuse a negative value before the note is appended.
- [tests/src/core/DawProjectInterchangeRoundTripTest.cpp:964](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-79/tests/src/core/DawProjectInterchangeRoundTripTest.cpp:964) checks that `-1` and `-0.001` are refused and that the error identifies the duration.

## Regression evidence

The family is `DawProjectInterchangeRoundTripTest`: **1/1 CTest test ran** (CTest test #210). On the original code the new slot failed because duration `-1` was accepted. It passed after the fix, failed again when the product patch was reversed, and passed after the patch was restored. The `cmake` commands ran from the worktree root; `ctest` ran from `/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p12-79/build/tests`.

| Run | Exact command | Exit |
|---|---|---:|
| Original build | `cmake --build build --target DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-79.r1/logs/build-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| RED | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-79.r1/logs/test-red-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 8 |
| Fixed build | `cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-79.r1/logs/build-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| GREEN | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-79.r1/logs/test-green-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| Reverse product patch | `git diff -- src/core/DawProjectReadTracks.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-79.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-79.r1/logs/fix.patch` | 0 |
| Build without fix | `cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-79.r1/logs/build-3.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| Proof RED | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-79.r1/logs/test-red-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 8 |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-79.r1/logs/fix.patch` | 0 |
| Rebuild with fix | `cmake --build build --target zene DawProjectInterchangeRoundTripTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-79.r1/logs/build-4.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| Proof GREEN | `ctest -R DawProjectInterchangeRoundTripTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-79.r1/logs/test-green-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-79.r1/logs/gate-1.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-79.r1/logs/gate-2.log 2>&1; result=$?; printf 'EXIT=%s\n' "$result"; exit "$result"` | 0 |

The `fff` grep tool was unavailable in this session; build and test logs were searched with `rg`.

## Files committed

- `src/core/DawProjectReadTracks.cpp`
- `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p12-79.md`
