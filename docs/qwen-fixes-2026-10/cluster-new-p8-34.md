# Cluster new-p8-34: `note.slide_clear` inverse

**Verdict: FIXED**

The finding is real. In `src/core/ControlCommandsNoteSlide.cpp:194-205`, a clear now records its journal inverse only when at least one slide flag was cleared, and the `control.undo` inverse carries an empty args object. The checkpoint at line 179 is already conditional on a slide flag being present in scope.

Regression coverage is in `tests/src/core/ControlNoteExpressionCommandsTest.cpp:138-185`. It checks that a no-op leaves the prior transaction (`note.add`) at the top, and that a real clear advertises empty inverse args, replays through `control.undo`, and restores the slide flag.

## Evidence

Logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-34.r1/logs`.

| Phase | Exact command (working directory) | Exit | Result |
|---|---|---:|---|
| Initial test-target build | `cmake --build build --target ControlNoteExpressionCommandsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-34.r1/logs/build-1.log 2>&1` (worktree) | 2 | Test-source `tx` scope error; corrected before running RED. |
| Corrected pre-fix build | `cmake --build build --target ControlNoteExpressionCommandsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-34.r1/logs/build-2.log 2>&1` (worktree) | 0 | Compiled the regression against the original product code. |
| Initial RED | `ctest -R ControlNoteExpressionCommandsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-34.r1/logs/test-1.log 2>&1` (`build/tests`) | 8 | Before the production edit, the no-op transaction and non-empty inverse args assertions both failed. |
| Initial fixed build | `cmake --build build --target zene ControlNoteExpressionCommandsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-34.r1/logs/build-3.log 2>&1` (worktree) | 0 | App and regression target rebuilt with the first fix. |
| Initial GREEN | `ctest -R ControlNoteExpressionCommandsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-34.r1/logs/test-2.log 2>&1` (`build/tests`) | 0 | 1/1 affected CTest tests passed. The final test was then strengthened to invoke the advertised inverse before checking its args. |
| Reverse product patch | `git diff -- src/core/ControlCommandsNoteSlide.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-34.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-34.r1/logs/fix.patch` (worktree) | 0 | Product fix removed for proof. |
| Reversed build | `cmake --build build --target zene ControlNoteExpressionCommandsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-34.r1/logs/build-6.log 2>&1` (worktree) | 0 | App and final regression test rebuilt without the fix. |
| RED | `ctest -R ControlNoteExpressionCommandsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-34.r1/logs/test-5.log 2>&1` (`build/tests`) | 8 | No-op transaction assertion failed; replay of the advertised inverse failed with `args: unexpected property 'clip'`. |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-34.r1/logs/fix.patch` (worktree) | 0 | Product fix restored. |
| Final build | `cmake --build build --target zene ControlNoteExpressionCommandsTest -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-34.r1/logs/build-7.log 2>&1` (worktree) | 0 | App and final test rebuilt with the fix. |
| GREEN | `ctest -R ControlNoteExpressionCommandsTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-34.r1/logs/test-6.log 2>&1` (`build/tests`) | 0 | 1/1 affected CTest tests passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-34.r1/logs/gate-3.log 2>&1` (worktree) | 0 | Passed after the final test edit. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-34.r1/logs/gate-4.log 2>&1` (worktree) | 0 | Passed after the final test edit. |

## Files committed

- `src/core/ControlCommandsNoteSlide.cpp`
- `tests/src/core/ControlNoteExpressionCommandsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-34.md`
