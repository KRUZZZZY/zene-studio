# Cluster new-p9-182

Verdict: **FIXED**

`InlineAutomation` now copies the automation clip only when one exists. The clip
object is cleared and reattached to the copy only in that case. The regression
test copies a fresh `DetuningHelper` and checks that its value is preserved and
that the copy has no automation.

## File and line evidence

- [include/InlineAutomation.h:42](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-182/include/InlineAutomation.h:42): guarded clip clone and guarded object reattachment.
- [tests/src/tracks/AutomationTrackTest.cpp:176](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-182/tests/src/tracks/AutomationTrackTest.cpp:176): `testCopyUnautomatedInlineAutomation`.
- [tests/upstream-modifications.txt:34](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-182/tests/upstream-modifications.txt:34): inherited production header registration.

## Build and test evidence

All build and CTest output was written to the specified log folder. The RED
run crashed in `AutomationTrackTest` (CTest 1/1, SegFault). After the fix, the
same family passed (CTest 1/1). Removing the production patch reproduced the
crash; restoring it passed again.

| Check | Exact command (working directory as shown) | Exit |
|---|---|---:|
| Initial RED build (worktree root) | `cmake --build build --target AutomationTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-182.r1/logs/build-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Initial RED test (`build/tests`) | `ctest -R AutomationTrackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-182.r1/logs/test-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 8 |
| Fixed build (worktree root) | `cmake --build build --target zene AutomationTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-182.r1/logs/build-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Fixed test (`build/tests`) | `ctest -R AutomationTrackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-182.r1/logs/test-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Save and reverse product patch (worktree root) | `git diff -- include/InlineAutomation.h > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-182.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-182.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Reverted build (worktree root) | `cmake --build build --target zene AutomationTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-182.r1/logs/build-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Reverted test, expected RED (`build/tests`) | `ctest -R AutomationTrackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-182.r1/logs/test-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 8 |
| Restore product patch (worktree root) | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-182.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Final build (worktree root) | `cmake --build build --target zene AutomationTrackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-182.r1/logs/build-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Final GREEN test (`build/tests`) | `ctest -R AutomationTrackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-182.r1/logs/test-4.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| Complexity gate (worktree root) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-182.r1/logs/gate-1.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |
| File-length gate (worktree root) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-182.r1/logs/gate-2.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit "$rc"` | 0 |

The `fff grep` tool was not available in this session (`command -v fff` exited
1). Logs were searched with `rg` for build errors and CTest results instead.

## Files committed

- `include/InlineAutomation.h`
- `tests/src/tracks/AutomationTrackTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-182.md`
