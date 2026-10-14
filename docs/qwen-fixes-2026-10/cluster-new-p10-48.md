# Cluster new-p10-48

Verdict: **FIXED**

`transport.groove` converted `swing` to `float` before checking its bounds. That let `0.75000001` round to `0.75f` and `-1e-50` round to negative zero. The handler now checks that the original `double` is finite and within `0..MaxSwing` before converting it.

## Evidence

- [src/core/ControlCommandsNotePlayback.cpp:205](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-48/src/core/ControlCommandsNotePlayback.cpp:205) reads the value as `double`; line 206 checks finiteness and bounds before line 210 casts to `float`.
- [tests/src/core/NotePlaybackTest.cpp:268](/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p10-48/tests/src/core/NotePlaybackTest.cpp:268) covers `0.75000001`; line 279 covers `-1e-50`.
- RED on original production code: both regression slots returned success (`errorKind` 0) instead of `InvalidArgs` (3). The inverse-patch proof reproduced both failures.
- GREEN after the fix and after reapplying the production patch: `NotePlaybackTest` passed. The affected CTest family executed 1 test; RED was 0/1 and GREEN was 1/1.

## Commands and exit codes

| Command | Exit |
| --- | ---: |
| `cmake --build build --target zene NotePlaybackTest` (all four builds) | 0 each |
| `ctest -R '^NotePlaybackTest$' --output-on-failure` (original-code RED) | 8 |
| `ctest -R '^NotePlaybackTest$' --output-on-failure` (fixed-code GREEN) | 0 |
| `git diff -- src/core/ControlCommandsNotePlayback.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-48.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-48.r1/logs/fix.patch` | 0 |
| `ctest -R '^NotePlaybackTest$' --output-on-failure` (after reversing the fix) | 8 |
| `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-48.r1/logs/fix.patch` | 0 |
| `ctest -R '^NotePlaybackTest$' --output-on-failure` (after reapplying the fix) | 0 |
| `bash tests/complexity-gate.sh --check` | 0 |
| `bash tests/file-length-gate.sh --check` | 0 |

Build and test output was saved as `build-1.log` through `build-4.log` and `test-1.log` through `test-4.log` in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-48.r1/logs`. Gate output and exit codes were saved in the same directory.

Files committed:

- `src/core/ControlCommandsNotePlayback.cpp`
- `tests/src/core/NotePlaybackTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-48.md`
