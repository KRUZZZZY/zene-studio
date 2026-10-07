# Cluster new-p8-37: misspelled note transform filter

**Verdict: FIXED**

`filterFrom()` now rejects keys outside the documented eight bounds, `scale`, and `invert` fields before parsing the filter. This prevents `{ "key_mni": 60 }` from becoming an empty filter that matches every note. The regression calls `note.transform_where` with `action: remove` and confirms both `InvalidArgs` and unchanged notes.

## File evidence

- `src/core/ControlCommandsNoteShape.cpp:300-320` checks the filter key allowlist and applies it in `filterFrom()`.
- `tests/src/core/NoteShapeValidationTest.cpp:176-185` covers the typo through the command path.

## Verification evidence

Each ctest command ran from `build/tests`. Each build command ran from the worktree root. Build and test output is in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-37.r1/logs`.

| Phase | Command | EXIT | Log |
|---|---|---:|---|
| RED build | `cmake --build build --target zene NoteShapeValidationTest` | 0 | `001-red-build.log` |
| RED regression | `ctest -R NoteShapeValidationTest --output-on-failure` | 8 | `002-red-test.log` |
| Fixed build | `cmake --build build --target zene NoteShapeValidationTest` | 0 | `003-green-build.log` |
| GREEN family | `ctest -R NoteShapeValidationTest --output-on-failure` | 0 | `004-green-test.log` |
| Reverse production diff | `git diff -- src/core/ControlCommandsNoteShape.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-37.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-37.r1/logs/fix.patch` | 0 | `005-rollback.log` |
| Rollback build | `cmake --build build --target zene NoteShapeValidationTest` | 0 | `006-rollback-build.log` |
| Rollback regression | `ctest -R NoteShapeValidationTest --output-on-failure` | 8 | `007-rollback-test.log` |
| Reapply production diff | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-37.r1/logs/fix.patch` | 0 | `008-reapply.log` |
| Reapplied build | `cmake --build build --target zene NoteShapeValidationTest` | 0 | `009-reapplied-build.log` |
| Reapplied family | `ctest -R NoteShapeValidationTest --output-on-failure` | 0 | `010-reapplied-test.log` |
| Complexity gate | `bash tests/complexity-gate.sh --check` | 0 | `011-complexity-gate.log` |
| File length gate | `bash tests/file-length-gate.sh --check` | 0 | `012-file-length-gate.log` |

The RED and rollback failures both show the regression assertion receiving `ControlErrorKind::None` instead of `InvalidArgs`; the GREEN and reapplied family runs pass. Each ctest run executed one registered `NoteShapeValidationTest` target (one of one). Its five behavioral test slots all ran in the RED and rollback runs; the regression slot failed there while the four neighboring slots passed.

## Files committed

- `src/core/ControlCommandsNoteShape.cpp`
- `tests/src/core/NoteShapeValidationTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p8-37.md`
