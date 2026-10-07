# Cluster new-p8-31: `note.scale` note-end validation

**Verdict: FIXED**

`src/core/ControlCommandsNoteShape.cpp:63-75` now checks the scaled start plus scaled length before rounding, then checks the rounded/clamped tick values that `note.scale` will store. This prevents both a scaled interval past `MaxSongLength` and a one-tick overrun caused by separately rounding an otherwise in-range interval.

Regression coverage is in `tests/src/core/NoteShapeValidationTest.cpp:151-176`. It verifies the reported 1,000,000 + 500,000 × 2 case is refused without editing the note, and verifies a half-tick rounding boundary whose stored end would exceed the limit is also refused.

## Evidence

| Check | Exact command | Exit/result |
|---|---|---|
| Revert product fix for RED proof | `git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-31.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | `EXIT=0` |
| Build before RED | `cmake --build build --target zene NoteShapeValidationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-31.r1/logs/build-9.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | `EXIT=0` |
| **RED** | From `build/tests`: `ctest -R NoteShapeValidationTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-31.r1/logs/test-9.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | `EXIT=8`; both new regression slots failed, 6 passed / 2 failed in the QtTest target |
| Restore product fix | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-31.r1/logs/fix.patch; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | `EXIT=0` |
| Build before GREEN | `cmake --build build --target zene NoteShapeValidationTest > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-31.r1/logs/build-10.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | `EXIT=0` |
| **GREEN** | From `build/tests`: `ctest -R NoteShapeValidationTest --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-31.r1/logs/test-10.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | `EXIT=0`; 1/1 CTest target passed (8 QtTest cases) |
| Complexity gate | `bash tests/complexity-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-31.r1/logs/complexity-gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | `EXIT=0` |
| File-length gate | `bash tests/file-length-gate.sh --check > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-31.r1/logs/file-length-gate-3.log 2>&1; rc=$?; printf 'EXIT=%s\n' "$rc"; exit 0` | `EXIT=0` |

The first file-length check, before relocating the regression from `NoteEditingCommandsTest.cpp`, exited 1 because that placement made the test file 509 lines. The regression was moved into the existing note-shape validation target; the final gate above passes. Logs were searched with `rg -n` because an fff grep tool was not available in this session.

## Files committed

- `src/core/ControlCommandsNoteShape.cpp`
- `tests/src/core/NoteShapeValidationTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-31.md`
