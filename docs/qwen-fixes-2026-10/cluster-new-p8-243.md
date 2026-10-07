# cluster new-p8-243: retry video paths after failed opens

Verdict: FIXED

## Evidence

At src/core/VideoFramePump.cpp:71-79, run() cached next.path before attempting the open. When a changed request names an empty path or its open fails, it now closes the decoder and clears openPath, so a later request for that same path attempts to open it again.

The regression at tests/src/core/VideoTrackTest.cpp:267-287 requests a missing file, waits for the null frame, creates a valid clip at that path, then requests it again and checks for a decoded red frame. Before the fix, request 2 failed the image assertion at line 287. The same failure was reproduced after applying the inverse of the production patch.

Both changed source files were already registered in tests/fork-sources.txt, so no source manifest change was needed. CTest selected 1 affected-family test (VideoTrackTest). The final run passed 1/1. RED runs reported 12 Qt test cases passed, 1 failed, and 1 skipped.

The fff grep tool was not available in this session's tool list or shell. I used targeted rg searches for error, FAILED, and Assertion in the build and test logs.

| Evidence | Result |
| --- | --- |
| RED command | ctest -R '^VideoTrackTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/test-1.log' 2>&1; test_exit_code=$?; echo "EXIT=$test_exit_code" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/test-1.log'; exit "$test_exit_code" — exit 8; assertion failed at VideoTrackTest.cpp:287. |
| GREEN command | ctest -R '^VideoTrackTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/test-4.log' 2>&1; test_exit_code=$?; echo "EXIT=$test_exit_code" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/test-4.log'; exit "$test_exit_code" — exit 0. |
| Affected-family count | 1 CTest test selected; final result 1/1 passed. |
| Reverse-patch proof | Same CTest command redirected to test-3.log; exit 8. |
| Complexity gate | bash tests/complexity-gate.sh --check; exit 0. |
| File-length gate | bash tests/file-length-gate.sh --check; exit 0. |
| Files committed | src/core/VideoFramePump.cpp; tests/src/core/VideoTrackTest.cpp; docs/qwen-fixes-2026-10/cluster-new-p8-243.md. |

## Commands and exit codes

Build and gate commands ran from the worktree root. CTest commands ran from build/tests. Each command below was executed as written; the shell exit code is recorded after it.

    cmake --build build --target VideoTrackTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/build-1.log' 2>&1; build_exit_code=$?; echo "EXIT=$build_exit_code" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/build-1.log'; exit "$build_exit_code"
    Exit: 0

    ctest -R '^VideoTrackTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/test-1.log' 2>&1; test_exit_code=$?; echo "EXIT=$test_exit_code" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/test-1.log'; exit "$test_exit_code"
    Exit: 8 (RED)

    cmake --build build --target zene VideoTrackTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/build-2.log' 2>&1; build_exit_code=$?; echo "EXIT=$build_exit_code" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/build-2.log'; exit "$build_exit_code"
    Exit: 0

    ctest -R '^VideoTrackTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/test-2.log' 2>&1; test_exit_code=$?; echo "EXIT=$test_exit_code" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/test-2.log'; exit "$test_exit_code"
    Exit: 0 (GREEN)

    git diff -- src/core/VideoFramePump.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/fix.patch' && git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/fix.patch'
    Exit: 0

    cmake --build build --target zene VideoTrackTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/build-3.log' 2>&1; build_exit_code=$?; echo "EXIT=$build_exit_code" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/build-3.log'; exit "$build_exit_code"
    Exit: 0

    ctest -R '^VideoTrackTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/test-3.log' 2>&1; test_exit_code=$?; echo "EXIT=$test_exit_code" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/test-3.log'; exit "$test_exit_code"
    Exit: 8 (inverse-patch RED)

    git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/fix.patch'
    Exit: 0

    cmake --build build --target zene VideoTrackTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/build-4.log' 2>&1; build_exit_code=$?; echo "EXIT=$build_exit_code" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/build-4.log'; exit "$build_exit_code"
    Exit: 0

    ctest -R '^VideoTrackTest$' --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/test-4.log' 2>&1; test_exit_code=$?; echo "EXIT=$test_exit_code" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/test-4.log'; exit "$test_exit_code"
    Exit: 0 (reapplied-fix GREEN)

    bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/gate-1.log' 2>&1; gate_exit_code=$?; echo "EXIT=$gate_exit_code" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/gate-1.log'; exit "$gate_exit_code"
    Exit: 0

    bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/gate-2.log' 2>&1; gate_exit_code=$?; echo "EXIT=$gate_exit_code" >> '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-243.r1/logs/gate-2.log'; exit "$gate_exit_code"

Exit: 0

## Files committed

- src/core/VideoFramePump.cpp
- tests/src/core/VideoTrackTest.cpp
- docs/qwen-fixes-2026-10/cluster-new-p8-243.md
