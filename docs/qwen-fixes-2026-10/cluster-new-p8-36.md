# Cluster new-p8-36: empty per-note lyrics undo step

**Verdict: FIXED**

`note.lyrics` with `lyrics: []` resolved the clip, then checkpointed and finalized despite having no writes. The handler now returns `{clip, set: 0}` before checkpointing. This also avoids publishing a clip edit for an empty request.

## File evidence

- [ControlCommandsNotePlayback.cpp:87](../../src/core/ControlCommandsNotePlayback.cpp#L87) returns the zero-write result before `addJournalCheckPoint()`.
- [NotePlaybackTest.cpp:300](../../tests/src/core/NotePlaybackTest.cpp#L300) checks the command response and asserts the journal undo depth does not change. Before the fix, line 312 observed depth 1 instead of 0.

## Verification evidence

Commands below were run from the worktree root except CTest commands, which were run from `build/tests`. Build output went to the numbered files under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-36.r1/logs`.

| Stage | Exact command | Exit | Result |
| --- | --- | ---: | --- |
| RED build | `cmake --build build --target zene NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-36.r1/logs/build-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Rebuilt `zene` and the affected test before the baseline run. |
| RED test | `ctest -R NotePlaybackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-36.r1/logs/test-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 | Failed at the regression assertion: actual undo depth 1, expected 0. |
| GREEN build | `cmake --build build --target zene NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-36.r1/logs/build-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Rebuilt after the fix. |
| GREEN test | `ctest -R NotePlaybackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-36.r1/logs/test-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Passed, 1/1 matching CTest tests. |
| Remove product patch | `git diff -- src/core/ControlCommandsNotePlayback.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-36.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-36.r1/logs/fix.patch` | 0 | Applied the reverse of only the production change. |
| Reversal build | `cmake --build build --target zene NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-36.r1/logs/build-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Rebuilt without the fix. |
| Reversal test | `ctest -R NotePlaybackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-36.r1/logs/test-3.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 8 | Failed at the same assertion: actual undo depth 1, expected 0. |
| Restore product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-36.r1/logs/fix.patch` | 0 | Restored the production change. |
| Restore build | `cmake --build build --target zene NotePlaybackTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-36.r1/logs/build-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Rebuilt with the fix restored. |
| Restore test | `ctest -R NotePlaybackTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-36.r1/logs/test-4.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Passed, 1/1 matching CTest tests. |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-36.r1/logs/gate-1.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Passed with no regressions. |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-36.r1/logs/gate-2.log 2>&1; result=$?; echo EXIT=$result; exit $result` | 0 | Passed with no regressions. |

The affected CTest family contained one test, `NotePlaybackTest`; it passed both with the fix and after restoring it. The reversal run confirms the regression fails without the production change.

## Files in the fix commit

- `src/core/ControlCommandsNotePlayback.cpp`
- `tests/src/core/NotePlaybackTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-36.md`
