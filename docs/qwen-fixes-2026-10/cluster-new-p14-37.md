FIXED

# new-p14-37: clipboard paste tick overflow

## Decision and evidence

The finding is **REAL**. DAWproject import converts note durations up to signed `int32_t` range in [DawProjectReadShared.h](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-37/src/core/DawProjectReadShared.h:237), then [DawProjectSession.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-37/src/core/DawProjectSession.cpp:581) constructs and adds `Note` objects directly. That path can hold a note length near `INT32_MAX`; it does not pass through native XML `Note::loadSettings`'s song-length clamp. The old `fits` check added the offset to the note endpoint in `tick_t`, so a paste at tick 1 could wrap the endpoint and pass validation.

[ControlCommandsNoteClipboard.cpp](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-37/src/core/ControlCommandsNoteClipboard.cpp:186) now computes spans, offsets, moved starts, and endpoints in `std::int64_t`. `fits` also verifies the moved start is representable by `tick_t`. Both `pasteNotes` and `duplicateNotes` call `fits` before taking a journal checkpoint; `addSelected` narrows only the already validated position. Copy reporting and duplicate-grid rounding use the same widened span arithmetic.

## Regression matrix

| Path | Evidence |
|---|---|
| Normal copy, paste, cut, duplicate, and undo | Existing `NoteEditingCommandsTest::theClipboardCopiesCutsPastesAndDuplicates` passed. |
| Paste endpoint overflow | `ControlNoteNoOpUndoTest::clipboardPasteRefusesAnOverflowingEndpoint` uses `numeric_limits<tick_t>::max()`, expects `InvalidArgs`, unchanged note state, and unchanged journal serial. |
| Duplicate endpoint overflow | `ControlNoteNoOpUndoTest::duplicateRefusesAnOverflowingEndpoint` checks the shared fit path and the same no-mutation/no-checkpoint contract. |
| Empty clipboard | Existing clipboard command test still checks `NotFound`. |
| Resource exhaustion or reconfiguration | Not applicable: this arithmetic path has no allocated resource or persistent configuration. The process clipboard's replacement behavior remains covered by existing copy/cut tests. |
| Audio thread and undo | No audio callback code changed. A refused edit leaves the journal serial unchanged; successful clipboard edits retain the existing one-checkpoint behavior. |
| Platform | The fix uses standard C++20 `<cstdint>` and `<limits>` only. The configured Linux build was run; separate MSVC and macOS builds were not available in this worktree. |

## Verification evidence

With only the product patch reversed, both overflow test cases failed because the commands returned success (`errorKind` 0 instead of `InvalidArgs` 3). Restoring the patch made the same two-test CTest family pass.

**RED** (working directory: worktree, then `build/tests` for CTest):

```sh
git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-37.r1/logs/fix.patch
# EXIT=0
cmake --build build --target zene NoteEditingCommandsTest ControlNoteNoOpUndoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-37.r1/logs/build-8.log 2>&1; build_exit=$?; printf 'EXIT=%s\n' "$build_exit"; exit "$build_exit"
# EXIT=0
ctest -R 'NoteEditingCommandsTest|ControlNoteNoOpUndoTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-37.r1/logs/test-6.log 2>&1; test_exit=$?; printf 'EXIT=%s\n' "$test_exit"; exit "$test_exit"
# EXIT=8; both regression cases failed as expected
```

**GREEN** (same worktree and CTest working directories):

```sh
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-37.r1/logs/fix.patch
# EXIT=0
cmake --build build --target zene NoteEditingCommandsTest ControlNoteNoOpUndoTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-37.r1/logs/build-9.log 2>&1; build_exit=$?; printf 'EXIT=%s\n' "$build_exit"; exit "$build_exit"
# EXIT=0
ctest -R 'NoteEditingCommandsTest|ControlNoteNoOpUndoTest' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-37.r1/logs/test-7.log 2>&1; test_exit=$?; printf 'EXIT=%s\n' "$test_exit"; exit "$test_exit"
# EXIT=0; 2/2 CTest tests passed
```

The source-manifest gate initially found 1,148 vendored sfizz entries in `tests/all-sources.txt` even though its header recipe excludes them. I re-derived the list with that recipe and documented the inherited manifest repair in `tests/upstream-modifications.txt`; no gate or baseline was changed. `bash tests/all-sources-reproduce.sh` exited 0.

After the final test-file and report edits, `cmake --build build --target zene NoteEditingCommandsTest ControlNoteNoOpUndoTest` exited 0 (`build-10.log`) and the same CTest family exited 0 with 2/2 tests passing (`test-8.log`).

Final gates:

| Gate | Result |
|---|---|
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-37` | PASS, exit 0 (`gate-5.log`; all eight checks passed, including complexity and file-length). |
| `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p14-37 new-p14-37` | PASS, exit 0 (`submit-1.log`; ran after commit `7366045da`). |

## Self-check

1. **Regression proof:** the product-only reverse build succeeded (exit 0) and the CTest family failed (exit 8); restoring the patch rebuilt successfully (exit 0) and the same family passed (exit 0, 2/2).
2. **Determinism:** the tests inject `numeric_limits<tick_t>::max()` directly into the model. They use no clock, sleep, race, or asynchronous seam; journal serial and note values are read synchronously.
3. **Cause and siblings:** `pasteNotes` and `duplicateNotes` are the only `fits` callers, and both validate before checkpointing. They are the only `addSelected` callers. `pasteOffset`, `duplicateOffset`, and their shared `spanOf` arithmetic all use the widened type.
4. **Collateral and limits:** project loading and saved formats were not changed. The new range check applies only to paste/duplicate results, which already promise notes inside `0..MaxSongLength`; source notes with longer authored durations remain loadable and unchanged.
5. **Verdict evidence:** this is a fixed real bug, not an already-fixed or false-positive finding; the DAWproject duration conversion and direct note construction cited above show the oversized model state is reachable.

## Files committed

- `src/core/ControlCommandsNoteClipboard.cpp`
- `tests/src/core/ControlNoteNoOpUndoTest.cpp`
- `tests/all-sources.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p14-37.md`
