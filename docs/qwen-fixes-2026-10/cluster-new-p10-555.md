# Cluster new-p10-555

Verdict: **FIXED**

The finding is real. `HostedPlugin::setEditorScale` previously accepted any value greater than zero, including positive infinity, and `openEditor` forwarded that value to CLAP.

## Evidence

- [ClapHostGui.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-555/plugins/ClapEffect/ClapHostGui.cpp:80): stores the requested scale only when it is finite and positive; otherwise it stores `1.0`.
- [ClapHostTest.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-555/tests/src/plugins/ClapHostTest.cpp:773): regression passes positive infinity to the host and requires the editor to open.
- [clap-test-gain.c](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-555/tests/data/clap-test-plugin/clap-test-gain.c:404): fixture records whether `set_scale` received a finite value and refuses attachment otherwise, making the test fail on the original code.

The regression failed before the product fix and passed after it. The required reverse-patch proof reproduced the failure with the fix removed and passed again after reapplying it.

## Test and gate evidence

All build and test output was captured under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-555.r1/logs`.

| Check | Command (working directory) | Exit | Log / result |
|---|---|---:|---|
| Initial test build | `cmake --build build --target ClapHostTest` (worktree root) | 0 | `build-1.log` |
| RED, original production code | `ctest -R ClapHostTest --output-on-failure` (`build/tests`) | 8 | `test-1.log`; infinity caused “editor refused to attach” |
| GREEN, fixed code | `cmake --build build --target zene ClapHostTest` (worktree root) | 0 | `build-2.log` |
| GREEN, affected family | `ctest -R ClapHostTest --output-on-failure` (`build/tests`) | 0 | `test-2.log`; 1/1 CTest test passed |
| Save product patch | `git diff -- plugins/ClapEffect/ClapHostGui.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-555.r1/logs/fix.patch` | 0 | patch saved |
| Reverse product patch | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-555.r1/logs/fix.patch` | 0 | fix removed for proof |
| Rebuild without fix | `cmake --build build --target zene ClapHostTest` (worktree root) | 0 | `build-3.log` |
| RED, fix removed | `ctest -R ClapHostTest --output-on-failure` (`build/tests`) | 8 | `test-3.log`; same infinity assertion failed |
| Reapply product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-555.r1/logs/fix.patch` | 0 | fix restored |
| Rebuild with fix | `cmake --build build --target zene ClapHostTest` (worktree root) | 0 | `build-4.log` |
| GREEN, repeated proof | `ctest -R ClapHostTest --output-on-failure` (`build/tests`) | 0 | `test-4.log`; 1/1 CTest test passed |
| Complexity gate | `bash tests/complexity-gate.sh --check` (worktree root) | 0 | `gate-1.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` (worktree root) | 0 | `gate-2.log` |

The affected family is one registered CTest test, `ClapHostTest`; it passed with the fix and failed on the regression both before the fix and in the reverse-patch proof.

## Files committed

- `plugins/ClapEffect/ClapHostGui.cpp`
- `tests/src/plugins/ClapHostTest.cpp`
- `tests/data/clap-test-plugin/clap-test-gain.c`
- `docs/qwen-fixes-2026-10/cluster-new-p10-555.md`
