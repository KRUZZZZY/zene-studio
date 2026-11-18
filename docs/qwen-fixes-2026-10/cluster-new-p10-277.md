# Cluster new-p10-277: non-finite NoteGrid zoom values

**Verdict: FIXED**

## Evidence

| File and lines | Finding |
|---|---|
| `src/gui/noteeditor/NoteGrid.cpp:70-74` | `setPixelsPerTick()` now ignores non-finite input before clamping and storing it. |
| `src/gui/noteeditor/NoteGrid.cpp:89-93` | `setScrollTicks()` now ignores non-finite input before storing it. |
| `tests/src/gui/NoteGridTest.cpp:136-151` | Regression covers NaN and positive/negative infinity for both setters, checking that prior finite values remain intact. |

Finite zoom values retain their existing clamp, and finite scroll values retain their existing non-negative clamp.

## Verification

`ctest` commands ran from `build/tests`; build and gate commands ran from the worktree root. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-277.r1/logs/`.

| Phase | Exact command | Exit | Evidence |
|---|---|---:|---|
| Build test before fix | `cmake --build build --target NoteGridTest` | 0 | `build-1.log` |
| RED on original code | `ctest -R '^NoteGridTest$' --output-on-failure` | 8 | `test-1.log`: 1/1 CTest target failed; assertion showed `pixelsPerTick()` was NaN instead of 0.5. |
| Build after fix | `cmake --build build --target zene NoteGridTest` | 0 | `build-2.log` |
| GREEN after fix | `ctest -R '^NoteGridTest$' --output-on-failure` | 0 | `test-2.log`: 1/1 CTest target passed. |
| Remove only production fix | `git diff -- src/gui/noteeditor/NoteGrid.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-277.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-277.r1/logs/fix.patch` | 0 | `qa-remove-1.log` |
| Rebuild with fix removed | `cmake --build build --target zene NoteGridTest` | 0 | `build-3.log` |
| RED with fix removed | `ctest -R '^NoteGridTest$' --output-on-failure` | 8 | `test-3.log`: same NaN assertion failed; 16 other QtTest checks passed. |
| Restore production fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-277.r1/logs/fix.patch` | 0 | `qa-restore-1.log` |
| Rebuild after restore | `cmake --build build --target zene NoteGridTest` | 0 | `build-4.log` |
| GREEN after restore | `ctest -R '^NoteGridTest$' --output-on-failure` | 0 | `test-4.log`: 1/1 CTest target passed. |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `gate-3.log` |
| File-length gate | `bash tests/file-length-gate.sh --check` | 0 | `gate-4.log` |

The affected-family count is one CTest target, `NoteGridTest`; it passed on both final GREEN runs. The fff grep tool was not exposed in this session, so the log folder was searched with `rg` for `error`, `FAILED`, and `Assertion`.

## Files committed

- `src/gui/noteeditor/NoteGrid.cpp`
- `tests/src/gui/NoteGridTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-277.md`
