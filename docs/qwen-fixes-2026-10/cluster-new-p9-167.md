# Cluster new-p9-167: Odd groove steps reject valid timing offsets

**Verdict: FIXED**

## Evidence

- [GrooveTemplate.cpp:158](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-167/src/core/GrooveTemplate.cpp:158) compares twice the signed timing offset with the step width in `long long`. For a five-tick step, offsets `-2` and `+2` are accepted while `-3` and `+3` are refused.
- [GrooveTemplate.cpp:342](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-167/src/core/GrooveTemplate.cpp:342) clamps extracted timing to `(stepTicks - 1) / 2`, preserving the greatest integer offsets strictly inside half a step.
- [GrooveTemplateTest.cpp:97](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-167/tests/src/core/GrooveTemplateTest.cpp:97) covers setter acceptance and refusal; [GrooveTemplateTest.cpp:152](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-167/tests/src/core/GrooveTemplateTest.cpp:152) checks extraction retains both signs of the two-tick offset.

The original implementation failed both new regression cases. Reversing only the production patch, rebuilding `GrooveTemplateTest` and `zene`, and rerunning the same ctest command reproduced both failures. Reapplying the patch and rebuilding made the same test pass. The final affected family run passed both `GrooveTemplateTest` and `ControlGrooveCommandsTest` (2/2).

## Command evidence

Commands ran from the worktree root unless the working directory is listed as `build/tests`. Build and ctest output is in the named log under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r1/logs`.

| Purpose | Working directory | Exact command | Exit |
| --- | --- | --- | ---: |
| Build regression against original production code | worktree root | `cmake --build build --target GrooveTemplateTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r1/logs/build-1.log 2>&1` | 0 |
| RED: original implementation; 1 ctest test failed and both added cases failed | `build/tests` | `ctest -R '^GrooveTemplateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r1/logs/test-1.log 2>&1` | 8 |
| Build fixed regression and application | worktree root | `cmake --build build --target GrooveTemplateTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r1/logs/build-2.log 2>&1` | 0 |
| GREEN: fixed implementation; 1/1 ctest test passed | `build/tests` | `ctest -R '^GrooveTemplateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r1/logs/test-2.log 2>&1` | 0 |
| Save production-only fix patch | worktree root | `git diff -- src/core/GrooveTemplate.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r1/logs/fix.patch` | 0 |
| Reverse fix for proof | worktree root | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r1/logs/fix.patch` | 0 |
| Rebuild with fix reversed | worktree root | `cmake --build build --target GrooveTemplateTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r1/logs/build-3.log 2>&1` | 0 |
| RED proof: reversed fix; both regression cases failed | `build/tests` | `ctest -R '^GrooveTemplateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r1/logs/test-3.log 2>&1` | 8 |
| Restore fix | worktree root | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r1/logs/fix.patch` | 0 |
| Rebuild restored fix | worktree root | `cmake --build build --target GrooveTemplateTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r1/logs/build-4.log 2>&1` | 0 |
| GREEN proof: restored fix; 1/1 ctest test passed | `build/tests` | `ctest -R '^GrooveTemplateTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r1/logs/test-4.log 2>&1` | 0 |
| Build adjacent control test and application | worktree root | `cmake --build build --target ControlGrooveCommandsTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r1/logs/build-5.log 2>&1` | 0 |
| Final affected-family GREEN: 2/2 ctest tests passed | `build/tests` | `ctest -R '^(GrooveTemplateTest|ControlGrooveCommandsTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r1/logs/test-5.log 2>&1` | 0 |
| Complexity gate | worktree root | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r1/logs/gate-1.log 2>&1` | 0 |
| File-length gate | worktree root | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-167.r1/logs/gate-2.log 2>&1` | 0 |
| Whitespace check | worktree root | `git diff --check` | 0 |

## Files in the cluster commit

- `src/core/GrooveTemplate.cpp`
- `tests/src/core/GrooveTemplateTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p9-167.md`
