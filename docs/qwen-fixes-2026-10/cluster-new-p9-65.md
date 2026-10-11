# Cluster new-p9-65

**Verdict: FALSE-POSITIVE** for branch `bugs/qwen-new-p9-65`.

The reported `src/core/ControlCommandsUndoHistory.cpp` is absent, and the source tree has no `project.set_undo_history` or `set_undo_history` command. The described `off` setting therefore cannot be invoked on this branch. The related live command is `control.set_undo_depth`: [src/core/ControlCommandsUndo.cpp:129](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p9-65/src/core/ControlCommandsUndo.cpp:129) defines its handler; lines 183–198 include the previous caps and a `control.set_undo_depth` inverse in `__transaction`; lines 259–288 register that command. Its schema accepts positive integer caps, not an `off` value. This is a different command and behavior, so no production change is justified by this finding.

The `fff` grep tool was not available in this session. Source checks used `git grep`; gate log searches used `rg` and returned no matches for the requested error terms.

## Evidence

| Check | Exact command | Exit / result |
|---|---|---|
| Reported file exists | `test -e src/core/ControlCommandsUndoHistory.cpp` | 1 (absent) |
| Reported command appears in tracked source/tests/tools | `git grep -n 'project.set_undo_history\|set_undo_history' -- src tests tools` | 1 (no matches) |
| RED regression | N/A: the reported command and handler are absent on this branch | Not run |
| GREEN regression | N/A: no production fix or regression was applicable | Not run |
| Affected-family test count | N/A: no matching command/test family exists on this branch | No ctest run |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-65.r1/logs/gate-1.log 2>&1; printf 'EXIT=%s\n' "$?"` | Gate exit 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-65.r1/logs/gate-2.log 2>&1; printf 'EXIT=%s\n' "$?"` | Gate exit 0 |
| Gate log search: `error` | `rg -n -i 'error' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-65.r1/logs/gate-1.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-65.r1/logs/gate-2.log` | 1 (no matches) |
| Gate log search: `FAILED` | `rg -n -i 'FAILED' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-65.r1/logs/gate-1.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-65.r1/logs/gate-2.log` | 1 (no matches) |
| Gate log search: `Assertion` | `rg -n -i 'Assertion' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-65.r1/logs/gate-1.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-65.r1/logs/gate-2.log` | 1 (no matches) |

No build or test was run because this branch contains neither the reported command nor a relevant handler to regress. No product or test files were changed.

## Files committed

- `docs/qwen-fixes-2026-10/cluster-new-p9-65.md`
