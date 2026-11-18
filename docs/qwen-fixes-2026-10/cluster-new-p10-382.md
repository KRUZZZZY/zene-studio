# Cluster new-p10-382

Verdict: **FIXED**

The slot position was adding `startTicks` before applying modulo, so a positive
loop start shortened the playable range. `playbackForColumn()` now wraps elapsed
ticks by the published loop length and then adds the slot start.

## Evidence

- [src/core/SessionPlayback.cpp:140](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-382/src/core/SessionPlayback.cpp:140) computes elapsed ticks; lines 145–149 wrap elapsed ticks before adding the start.
- [tests/src/core/SessionPlaybackTest.cpp:235](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-382/tests/src/core/SessionPlaybackTest.cpp:235) covers a start of 48 and loop length of 96. It expects positions 96 at elapsed tick 48 and 48 at elapsed tick 96.
- The new test failed on the original code and after removing the fix: actual position `0`, expected `96`. With the fix restored, the test passed.
- Both changed C++ files were already registered in `tests/fork-sources.txt` (lines 1570 and 2031); no header changed.

## Build and test evidence

| Purpose | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Build regression on original code | `cmake --build build --target SessionPlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-382.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 | Built the test target before the red run. |
| RED on original code | `ctest -R '^SessionPlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-382.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 8 | 12 passed, the new loop-start assertion failed. |
| Build regression with fix | `cmake --build build --target SessionPlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-382.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 | Successful rebuild before the first green run. |
| GREEN with fix | `ctest -R '^SessionPlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-382.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 | 1/1 CTest test passed. |
| RED after reverse-patch proof | `ctest -R '^SessionPlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-382.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 8 | The same assertion failed with actual `0`, expected `96`. |
| GREEN after restoring fix | `ctest -R '^SessionPlaybackTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-382.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 | 1/1 CTest test passed. |

The affected family executed **1 CTest test**, `SessionPlaybackTest` (CTest test
#308). Both red and green runs were nonzero-test runs.

The reverse-patch proof used these exact commands:

```sh
git diff -- src/core/SessionPlayback.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-382.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-382.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-382.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"
```

Both exited 0. The reverse-patch build and restored build used the same target:

```sh
cmake --build build --target SessionPlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-382.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"
cmake --build build --target SessionPlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-382.r1/logs/build-5.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"
```

Both builds exited 0.

One intermediate invocation was `cmake --build build --target SessionPlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-382.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"`. Its exit line was not captured after the tool wait window. It is not used as evidence; the subsequent `build-3.log` build exited 0 before the first green test.

## Gates

| Gate | Exact command | Exit |
| --- | --- | ---: |
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-382.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-382.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"` | 0 |

## Files committed

- `src/core/SessionPlayback.cpp`
- `tests/src/core/SessionPlaybackTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-382.md`
