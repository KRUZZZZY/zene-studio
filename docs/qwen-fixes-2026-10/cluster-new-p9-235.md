# Cluster new-p9-235: LocklessList value lifetime

Verdict: **FIXED**

`include/LocklessList.h:53-63` now destroys still-linked elements and returns their slots before deleting the allocator. `push` constructs an `Element` in the allocator slot at lines 65-85, and `free` destroys it before returning the slot at lines 102-106. Construction failure returns the slot to the allocator before rethrowing.

The regression cases in `tests/src/core/AudioEngineTeardownTest.cpp:88-156` use a nontrivial lifetime probe. On the original header, the push/free case observes no live stored object and the teardown case observes no value destructor. The final probe failed against the original header with 4 passed and 2 failed cases, then passed with 6 cases after restoring the fix.

## Evidence

Commands below were run in the worktree; ctest commands ran from `build/tests`. Build and test output is in the numbered log shown by each command.

| Phase | Exact command | Exit |
|---|---|---:|
| Build regression target before fix | `cmake --build build --target AudioEngineTeardownTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-235.r1/logs/build-1.log 2>&1` | 0 |
| RED on original header | `ctest -R '^AudioEngineTeardownTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-235.r1/logs/test-1.log 2>&1` | 8 |
| Build after fix | `cmake --build build --target zene AudioEngineTeardownTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-235.r1/logs/build-2.log 2>&1` | 0 |
| GREEN after fix | `ctest -R '^AudioEngineTeardownTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-235.r1/logs/test-2.log 2>&1` | 0 |
| Save and remove the product patch | `git diff -- include/LocklessList.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-235.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-235.r1/logs/fix.patch` | 0 |
| Rebuild without fix, final probe | `cmake --build build --target zene AudioEngineTeardownTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-235.r1/logs/build-8.log 2>&1` | 0 |
| RED without fix, final probe | `ctest -R '^AudioEngineTeardownTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-235.r1/logs/test-8.log 2>&1` | 8 |
| Reapply the product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-235.r1/logs/fix.patch` | 0 |
| Rebuild with fix, final probe | `cmake --build build --target zene AudioEngineTeardownTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-235.r1/logs/build-9.log 2>&1` | 0 |
| GREEN after restored patch, final probe | `ctest -R '^AudioEngineTeardownTest$' --output-on-failure --verbose > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-235.r1/logs/test-9.log 2>&1` | 0 |
| Complexity gate after final edits | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-235.r1/logs/gate-7.log 2>&1` | 0 |
| File-length gate after final edits | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-235.r1/logs/gate-8.log 2>&1` | 0 |

Affected family count: ctest ran 1/1 target; the final verbose QtTest run reported 6 passed, 0 failed, 0 skipped (two regression cases, two existing worker cases, setup, and cleanup).

Files committed:

- `include/LocklessList.h`
- `tests/src/core/AudioEngineTeardownTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-235.md`
