# Cluster new-p8-38: `note.select_where` group mates

**Verdict: FIXED**

`note.select_where` previously selected only filter hits. In [ControlCommandsNoteShape.cpp](../../src/core/ControlCommandsNoteShape.cpp:329), filter hits are converted to note-list positions and expanded to their positive group IDs at line 342. `selectWhere` applies the expanded positions to note flags and the selection index store at lines 357–397. Its `matched` count remains the number of direct filter hits; `selected` reports those hits and their group mates.

The regression in [NoteShapeValidationTest.cpp](../../tests/src/core/NoteShapeValidationTest.cpp:229) gives two notes the same group and filters for only one. On the original product code, the test fails because the second note's selection flag is false; it also checks the selection index store and returned IDs after the fix.

## Evidence

Commands below were run from the worktree root unless marked `build/tests`. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-38.r1/logs`.

| Check | Exact command | Exit |
|---|---|---:|
| Save and remove final product diff | `git diff -- src/core/ControlCommandsNoteShape.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-38.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-38.r1/logs/fix.patch` | 0 |
| Build regression without fix (`build-14.log`) | `cmake --build build --target NoteShapeValidationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-38.r1/logs/build-14.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| **RED**, from `build/tests` (`test-13.log`) | `ctest -R NoteShapeValidationTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-38.r1/logs/test-13.log 2>&1; result=$?; echo EXIT=$result; exit $result` | **8** |
| Restore product diff | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-38.r1/logs/fix.patch` | 0 |
| Build app and affected tests (`build-15.log`) | `cmake --build build --target zene NoteShapeValidationTest NoteEditingCommandsTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-38.r1/logs/build-15.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 |
| **GREEN**, from `build/tests` (`test-14.log`) | `ctest -R 'NoteShapeValidationTest|NoteEditingCommandsTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-38.r1/logs/test-14.log 2>&1; result=$?; echo EXIT=$result; exit $result` | **0** |
| Affected-family count | `NoteEditingCommandsTest` and `NoteShapeValidationTest`: 2/2 CTest tests passed | 0 |
| Complexity gate (`complexity-4.log`) | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-38.r1/logs/complexity-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | **0** |
| File-length gate (`file-length-4.log`) | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-38.r1/logs/file-length-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | **0** |

## Files committed

- `src/core/ControlCommandsNoteShape.cpp`
- `tests/src/core/NoteShapeValidationTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-38.md`
