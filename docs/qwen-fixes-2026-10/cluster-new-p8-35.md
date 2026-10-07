# Cluster new-p8-35: per-note lyric validation

**Verdict: FIXED**

`note.lyrics` now rejects malformed per-note entries before adding a journal checkpoint or changing notes. In [ControlCommandsNotePlayback.cpp](../../src/core/ControlCommandsNotePlayback.cpp#L79), each array item must be an object with string `note` and `text` fields; the resolved note and text are collected before the checkpoint at line 100. This prevents absent and non-string `text` values from being converted to an empty string and clearing an existing lyric.

The regression cases in [NotePlaybackTest.cpp](../../tests/src/core/NotePlaybackTest.cpp#L268) preserve an existing lyric and expect `InvalidArgs` for a missing `text` field and for numeric `text`.

## Evidence

| Check | Command and working directory | Exit | Result |
|---|---|---:|---|
| Initial RED, original handler | `ctest -R NotePlaybackTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/test-1.log' 2>&1; status=$?; echo EXIT=$status` (cwd: `build/tests`) | 8 | Both malformed entries unexpectedly returned success. |
| Fix-reversal RED | `ctest -R NotePlaybackTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/test-4.log' 2>&1; status=$?; echo EXIT=$status` (cwd: `build/tests`) | 8 | With the production patch removed, both cases changed `original` to `""`. |
| Final GREEN | `ctest -V -R NotePlaybackTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/test-6.log' 2>&1; status=$?; echo EXIT=$status` (cwd: `build/tests`) | 0 | 1/1 CTest target passed; QtTest totals: 12 passed, 0 failed. Both new regression cases passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/gate-1.log' 2>&1; status=$?; echo EXIT=$status` (cwd: worktree) | 0 | Passed. |
| File length gate | `bash tests/file-length-gate.sh --check > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/gate-2.log' 2>&1; status=$?; echo EXIT=$status` (cwd: worktree) | 0 | Passed. |

## Build and test command record

Every build and test invocation and its observed exit code:

| Command | Working directory | Exit |
|---|---|---:|
| `cmake --build build --target NotePlaybackTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/build-1.log' 2>&1; status=$?; echo EXIT=$status` | worktree | 0 |
| `ctest -R NotePlaybackTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/test-1.log' 2>&1; status=$?; echo EXIT=$status` | `build/tests` | 8 |
| `cmake --build build --target NotePlaybackTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/build-2.log' 2>&1; status=$?; echo EXIT=$status` | worktree | 0 |
| `ctest -R NotePlaybackTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/test-2.log' 2>&1; status=$?; echo EXIT=$status` | `build/tests` | 0 |
| `cmake --build build --target NotePlaybackTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/build-3.log' 2>&1; status=$?; echo EXIT=$status` | worktree | 0 |
| `ctest -R NotePlaybackTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/test-3.log' 2>&1; status=$?; echo EXIT=$status` | `build/tests` | 0 |
| `cmake --build build --target NotePlaybackTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/build-4.log' 2>&1; status=$?; echo EXIT=$status` | worktree | 0 |
| `ctest -R NotePlaybackTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/test-4.log' 2>&1; status=$?; echo EXIT=$status` | `build/tests` | 8 |
| `cmake --build build --target zene NotePlaybackTest > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/build-5.log' 2>&1; status=$?; echo EXIT=$status` | worktree | 0 |
| `ctest -R NotePlaybackTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/test-5.log' 2>&1; status=$?; echo EXIT=$status` | `build/tests` | 0 |
| `ctest -V -R NotePlaybackTest --output-on-failure > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/test-6.log' 2>&1; status=$?; echo EXIT=$status` | `build/tests` | 0 |

Patch-reversal proof commands:

| Command | Working directory | Exit |
|---|---|---:|
| `git diff -- src/core/ControlCommandsNotePlayback.cpp > '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/fix.patch' && git apply -R '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/fix.patch'; status=$?; echo EXIT=$status` | worktree | 0 |
| `git apply '/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-35.r1/logs/fix.patch'; status=$?; echo EXIT=$status` | worktree | 0 |

The `fff` grep tool was not available in this session's tool list; log checks used focused `rg` searches instead.

## Files committed

- `src/core/ControlCommandsNotePlayback.cpp`
- `tests/src/core/NotePlaybackTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-35.md`
