# cluster new-p10-438 — Failed FFmpeg allocations are dereferenced

Verdict: **FIXED**

## Finding and regression

The finding was real. [VideoTestClip.h](../../tests/src/core/VideoTestClip.h#L100) returns failure
when `av_frame_alloc()` fails, and [VideoTestClip.h](../../tests/src/core/VideoTestClip.h#L116)
checks both `avformat_new_stream()` and `avcodec_alloc_context3()` before dereferencing them.

Round 3 adds an independent stream-allocation failure injection in
[VideoTrackTest.cpp](../../tests/src/core/VideoTrackTest.cpp#L146). The case at lines 446–450
forces `avformat_new_stream()` to return null and requires `addStream()` to return false while
the codec context remains non-null. The existing codec-context failure after successful stream
creation and frame-allocation failure cases remain in
[VideoTrackTest.cpp](../../tests/src/core/VideoTrackTest.cpp#L452).

The `fff` grep tool was unavailable (`command -v fff; fff --help` exited 127); build and test
logs were searched with `rg -n` for `error`, `FAILED`, and `Assertion`.

## Round 3 evidence

Build commands ran from the worktree root. CTest commands ran from `build/tests`.

| Purpose | Exact command | Exit |
|---|---|---:|
| Build with product guards restored | `cmake --build build --target zene VideoTrackTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r3/logs/build-1.log 2>&1; build_exit=$?; echo BUILD_EXIT=$build_exit; exit "$build_exit"` | 0 |
| Baseline fixed test | `QT_QPA_PLATFORM=offscreen ctest -R '^VideoTrackTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r3/logs/test-1.log 2>&1; test_exit=$?; echo TEST_EXIT=$test_exit; exit "$test_exit"` | 0 |
| Extract product fix and reverse its checks | `git show 2861e4e67 --format= -- tests/src/core/VideoTestClip.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r3/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r3/logs/fix.patch` | 0 |
| Rebuild with checks reversed | `cmake --build build --target zene VideoTrackTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r3/logs/build-2.log 2>&1; build_exit=$?; echo BUILD_EXIT=$build_exit; exit "$build_exit"` | 0 |
| RED test with checks reversed | `QT_QPA_PLATFORM=offscreen ctest -R '^VideoTrackTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r3/logs/test-2.log 2>&1; test_exit=$?; echo TEST_EXIT=$test_exit; exit "$test_exit"` | **8** |
| Restore product checks | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r3/logs/fix.patch` | 0 |
| Rebuild with checks restored | `cmake --build build --target zene VideoTrackTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r3/logs/build-3.log 2>&1; build_exit=$?; echo BUILD_EXIT=$build_exit; exit "$build_exit"` | 0 |
| GREEN test with checks restored | `QT_QPA_PLATFORM=offscreen ctest -R '^VideoTrackTest$' --output-on-failure -V > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r3/logs/test-3.log 2>&1; test_exit=$?; echo TEST_EXIT=$test_exit; exit "$test_exit"` | **0** |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r3/logs/gate-3.log 2>&1; gate_exit=$?; echo GATE_EXIT=$gate_exit; exit "$gate_exit"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-438.r3/logs/gate-4.log 2>&1; gate_exit=$?; echo GATE_EXIT=$gate_exit; exit "$gate_exit"` | 0 |

The RED output failed specifically at the new `avformat_new_stream failure was not reported
independently` assertion (12 passed, 1 failed, 1 skipped). The restored GREEN run executed one
matching CTest case; QtTest reported 13 passed, 0 failed, 1 skipped. The skip is the existing
no-FFmpeg branch.

## Files in this commit

- `tests/src/core/VideoTrackTest.cpp` — isolated stream-allocation failure injection and assertion.
- `docs/qwen-fixes-2026-10/cluster-new-p10-438.md` — this evidence report.

The production fix remains in the earlier commit `2861e4e67`.
