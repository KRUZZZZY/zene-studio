# Cluster new-p9-302: Note key construction range

**Verdict: FIXED**

`Note::Note` in `src/core/Note.cpp:51,55` used `NumKeys` (128) as the inclusive upper clamp for both the current and initial key. `Note::setKey()` at `src/core/Note.cpp:212` clamps to `NumKeys - 1`. The constructor now uses the same maximum for both fields.

The regression at `tests/src/core/NoteEditRangeTest.cpp:92-95` constructs a note with key `NumKeys` and asserts the public key is `NumKeys - 1`. Before the fix it failed with actual `128`, expected `127`. The test file is already registered in `tests/fork-sources.txt`; the `src/core/Note.cpp` ledger entry in `tests/upstream-modifications.txt:696` now records this fix.

## Build and test evidence

All ctest commands ran from `build/tests`. The family `^NoteEditRangeTest$` matched one registered test; it failed before the fix and passed after it.

| Stage | Command | Exit | Result |
|---|---|---:|---|
| Initial build before fix | `cmake --build build --target NoteEditRangeTest` | 0 | Test target built. Log: `build-1.log`. |
| RED before fix | `ctest -R '^NoteEditRangeTest$' --output-on-failure` | 8 | 1/1 failed; actual 128, expected 127. Log: `test-1-red.log`. |
| Remove product fix | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-302.r1/logs/fix.patch` | 0 | Fix removed for proof. |
| Rebuild without fix | `cmake --build build --target zene NoteEditRangeTest` | 0 | Both targets rebuilt. Log: `build-2-undo.log`. |
| RED proof | `ctest -R '^NoteEditRangeTest$' --output-on-failure` | 8 | 1/1 failed again; actual 128, expected 127. Log: `test-2-undo-red.log`. |
| Restore product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-302.r1/logs/fix.patch` | 0 | Fix restored. |
| Rebuild with fix | `cmake --build build --target zene NoteEditRangeTest` | 0 | Both targets rebuilt. Log: `build-3-green.log`. |
| GREEN | `ctest -R '^NoteEditRangeTest$' --output-on-failure` | 0 | 1/1 passed. Log: `test-3-green.log`. |

## Required gates

| Command | Exit | Log |
|---|---:|---|
| `bash tests/complexity-gate.sh --check` | 0 | `gate-1-complexity.log` |
| `bash tests/file-length-gate.sh --check` | 0 | `gate-2-file-length.log` |

The prescribed `fff grep` tool was not available in this environment (`command -v fff` returned exit 1); targeted log searches used `rg` instead.

## Files committed

- `src/core/Note.cpp`
- `tests/src/core/NoteEditRangeTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-302.md`
