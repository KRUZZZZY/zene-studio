# Cluster new-p10-39

Verdict: **FIXED**

The finding is real. `src/core/ControlCommandsNoteEdit.cpp:161-169` checked each row's
position and length against the note's original shape. Duplicate note IDs could therefore pass
individually while leaving an invalid combined shape. Lines 172-217 now accumulate the final
position and length for each note and check their sum before the batch can be applied. The
per-row checks remain in place.

The regression is in `tests/src/core/NoteEditRangeTest.cpp:126-141`. It starts a note at
position 0 with length 1, then edits its length to `MaxSongLength / 2` and its position to
`MaxSongLength / 2 + 1` under the same ID. The command now returns `InvalidArgs` and leaves the
note unchanged. Before the fix, the assertion saw actual error kind `0` and expected `3`.

The affected CTest family contains one registered test: `NoteEditRangeTest` (1/1). The test
source and production source are already registered in `tests/fork-sources.txt`.

## Commands and results

All build and CTest output is saved under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs`.

| Run | Exact command | Exit |
| --- | --- | ---: |
| Baseline build | `cmake --build build --target NoteEditRangeTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/build-1.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| RED | `ctest -R NoteEditRangeTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/test-1.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 8 |
| Fixed build | `cmake --build build --target NoteEditRangeTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/build-2.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| GREEN | `ctest -R NoteEditRangeTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/test-2.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 (1/1) |
| Save product fix for reversal proof | `git diff -- src/core/ControlCommandsNoteEdit.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/fix.patch` | 0 |
| Reverse product fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| Reversed build | `cmake --build build --target NoteEditRangeTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/build-3.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| Reversal RED | `ctest -R NoteEditRangeTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/test-3.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 8 |
| Restore product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/fix.patch; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| Restored build | `cmake --build build --target NoteEditRangeTest zene > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/build-4.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| Restored GREEN | `ctest -R NoteEditRangeTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/test-4.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 (1/1) |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/gate-complexity-1.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/gate-file-length-1.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| Final complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/gate-complexity-2.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |
| Final file-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/gate-file-length-2.log 2>&1; rc=$?; echo EXIT=$rc; exit "$rc"` | 0 |

The `fff` grep tool was not available in this session, so saved logs were searched with `rg`:

- `rg -n -A3 'FAIL!' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/test-1.log` — exit 0; actual `0`, expected `3`.
- `rg -n 'FAIL' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/test-1.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/test-2.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/test-3.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/test-4.log` — exit 0; failures only in RED and reversal RED.
- `rg -n 'tests passed' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/test-1.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/test-2.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/test-3.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/test-4.log` — exit 0; each invocation ran one test.
- `rg -n -i 'error' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/build-1.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/build-2.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/build-3.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/build-4.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/gate-complexity-1.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/gate-file-length-1.log` — exit 1; no matches.
- `rg -n -i 'error' /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/gate-complexity-2.log /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-39.r1/logs/gate-file-length-2.log` — exit 1; no matches.

## Files committed

- `src/core/ControlCommandsNoteEdit.cpp`
- `tests/src/core/NoteEditRangeTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-39.md`
