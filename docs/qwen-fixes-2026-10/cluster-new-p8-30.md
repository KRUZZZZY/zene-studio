# Cluster new-p8-30: `note.resize` song limit

**Verdict: FIXED**

`note.resize` accepted a requested length up to `MaxSongLength` without considering the note's
position, so a note starting after tick 0 could end beyond the song limit. In
[`ControlCommandsNotes.cpp`](../../src/core/ControlCommandsNotes.cpp#L322), the handler now checks
the sum in `qint64` before creating a journal checkpoint or changing the note. It returns
`invalid_args` and leaves the note unchanged when the end exceeds the limit. An end exactly at the
limit remains accepted.

The regression is [`NoteEditRangeTest.cpp`](../../tests/src/core/NoteEditRangeTest.cpp#L127): it
resizes a note starting at tick 48 to `MaxSongLength`, checks refusal and unchanged length, then
checks that ending exactly at `MaxSongLength` succeeds. Before the fix, the test failed because the
command returned success (`errorKind` 0 instead of `InvalidArgs` 3).

## Evidence

The affected ctest family is `NoteEditRangeTest`; each run matched **1 CTest test**. The original
RED and the proof RED both failed in `noteResizeMayNotEndPastTheSongLimit`; both fixed runs passed.

| Phase | Exact command | Exit |
| --- | --- | ---: |
| Initial rebuild, fix absent | `cmake --build build --target NoteEditRangeTest zene -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-30.r1/logs/build-1-red.log 2>&1; echo EXIT=$?` | 0 |
| Initial RED (cwd: `build/tests`) | `QT_QPA_PLATFORM=offscreen ctest -R NoteEditRangeTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-30.r1/logs/test-1-red.log 2>&1; echo EXIT=$?` | 8 |
| Fixed rebuild | `cmake --build build --target NoteEditRangeTest zene -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-30.r1/logs/build-2-green.log 2>&1; echo EXIT=$?` | 0 |
| Fixed GREEN (cwd: `build/tests`) | `QT_QPA_PLATFORM=offscreen ctest -R NoteEditRangeTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-30.r1/logs/test-2-green.log 2>&1; echo EXIT=$?` | 0 |

### Patch removal proof

| Step | Exact command | Exit |
| --- | --- | ---: |
| Save and reverse only the product patch | `git diff -- src/core/ControlCommandsNotes.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-30.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-30.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild with the fix removed | `cmake --build build --target NoteEditRangeTest zene -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-30.r1/logs/build-3-proof-red.log 2>&1; echo EXIT=$?` | 0 |
| Same regression RED (cwd: `build/tests`) | `QT_QPA_PLATFORM=offscreen ctest -R NoteEditRangeTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-30.r1/logs/test-3-proof-red.log 2>&1; echo EXIT=$?` | 8 |
| Restore the product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-30.r1/logs/fix.patch; echo EXIT=$?` | 0 |
| Rebuild with the fix restored | `cmake --build build --target NoteEditRangeTest zene -- -j12 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-30.r1/logs/build-4-proof-green.log 2>&1; echo EXIT=$?` | 0 |
| Same regression GREEN (cwd: `build/tests`) | `QT_QPA_PLATFORM=offscreen ctest -R NoteEditRangeTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-30.r1/logs/test-4-proof-green.log 2>&1; echo EXIT=$?` | 0 |

### Gates

| Gate | Exact command | Exit |
| --- | --- | ---: |
| Complexity | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-30.r1/logs/gate-3-final-complexity.log 2>&1; echo EXIT=$?` | 0 |
| File length | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-30.r1/logs/gate-4-final-file-length.log 2>&1; echo EXIT=$?` | 0 |

## Files committed

- `src/core/ControlCommandsNotes.cpp`
- `tests/src/core/NoteEditRangeTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-30.md`
