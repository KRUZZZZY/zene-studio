# Cluster new-p10-10: collaboration request ID overflow

Verdict: **FIXED**

`m_nextRequest` is an `int` ([CollabClient.h:127](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-10/include/CollabClient.h:127)). The old increment overflowed at `INT_MAX`. [CollabClient.cpp:295](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-10/src/core/CollabClient.cpp:295) now wraps to ID 1 without signed overflow and skips IDs still present in `m_inflight`. The deterministic regression is [CollabTransportTest.cpp:232](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-10/tests/src/core/CollabTransportTest.cpp:232); it sets the counter to `INT_MAX`, reserves ID 1, and verifies the request is sent as ID 2.

All build and test output was saved under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-10.r1/logs`. Build commands ran from the worktree; ctest commands ran from `build/tests`.

| Evidence | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| Initial RED (`test-1.log`) | `ctest -R '^CollabTransportTest$' --output-on-failure` | 8 | Regression failed: actual ID `-2147483648`, expected `2`; 11 passed, 1 failed. |
| GREEN (`test-2.log`) | `ctest -R '^CollabTransportTest$' --output-on-failure` | 0 | CTest target passed. |
| Full-patch inverse RED (`test-5.log`) | `ctest -R '^CollabTransportTest$' --output-on-failure` | 8 | After reverting the combined product patch and rebuilding, the same regression failed with actual `-2147483648`, expected `2`. |
| Restored GREEN (`test-6.log`) | `ctest -R '^CollabTransportTest$' --output-on-failure --verbose` | 0 | 1/1 CTest target; 12 Qt test cases passed, 0 failed, 0 skipped. |
| Complexity gate (`gate-3.log`) | `bash tests/complexity-gate.sh --check` | 0 | Passed. |
| File-length gate (`gate-4.log`) | `bash tests/file-length-gate.sh --check` | 0 | Passed. |

The full inverse proof used `git diff -- include/CollabClient.h src/core/CollabClient.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-10.r1/logs/fix.patch` followed by `git apply -R` (exit 0). The header's test-access friend was temporarily restored from `test-access.patch` while the allocator was reverted, so the regression could compile. The test failed, then the test-access patch was removed and the full product patch reapplied; the rebuilt test passed.

The affected CTest family is one target (`CollabTransportTest`); it contains 12 Qt test cases. `cmake --build build --target zene CollabTransportTest` exited 0 before each recorded test run. The final build is in `build-6.log`.

Files committed:

- `include/CollabClient.h`
- `src/core/CollabClient.cpp`
- `tests/src/core/CollabTransportTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-10.md`
