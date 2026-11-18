# Cluster new-p8-24: no-op note transforms

## Verdict

FIXED

## Evidence

The handlers took clip journal checkpoints before they knew whether any note would
change. The change preflights each operation using the same target calculation as
the edit, then checkpoints only if at least one selected note will change.

- `src/core/ControlCommandsNoteTransform.cpp:234` — `note.transpose`.
- `src/core/ControlCommandsNoteTransform.cpp:283` — `note.velocity_offset`.
- `src/core/ControlCommandsNoteTransform.cpp:322` — `note.velocity_scale`.
- `src/core/ControlCommandsNoteTools.cpp:81` — `note.invert`.
- `src/core/ControlCommandsNoteTools.cpp:215` — `note.transpose_in_key`.
- Regression cases: `tests/src/core/ControlNoteScaleVerbsTest.cpp:437`, `:455`, `:472`; `tests/src/core/NoteToolsTest.cpp:105`, `:151`.

## Verification

The RED run used the original production sources after reversing the saved product
patch. All five no-op regression cases failed on their undo-depth assertion. The
GREEN run used the same tests after reapplying the patch. The affected family is
two CTest targets; GREEN reported `100% tests passed, 0 tests failed out of 2`.

| Phase | Exact command | Exit |
|---|---|---:|
| RED build (`build-5.log`) | `cmake --build build --target zene ControlNoteScaleVerbsTest NoteToolsTest` | 0 |
| RED test (`test-4.log`) | `ctest -R '^(ControlNoteScaleVerbsTest|NoteToolsTest)$' --output-on-failure` | 8 (expected failures) |
| GREEN build (`build-6.log`) | `cmake --build build --target zene ControlNoteScaleVerbsTest NoteToolsTest` | 0 |
| GREEN test (`test-5.log`) | `ctest -R '^(ControlNoteScaleVerbsTest|NoteToolsTest)$' --output-on-failure` | 0 (2/2 targets passed) |
| Complexity gate (`gate-5.log`) | `bash tests/complexity-gate.sh --check` | 0 |
| File-length gate (`gate-6.log`) | `bash tests/file-length-gate.sh --check` | 0 |

## Files in the commit

- `src/core/ControlCommandsNoteTransform.cpp`
- `src/core/ControlCommandsNoteTools.cpp`
- `tests/src/core/ControlNoteScaleVerbsTest.cpp`
- `tests/src/core/NoteToolsTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p8-24.md`

Regression proof commands and results:

```sh
git diff -- src/core/ControlCommandsNoteTransform.cpp src/core/ControlCommandsNoteTools.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-24.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-24.r1/logs/fix.patch
# exit 0; original implementation restored for RED
ctest -R '^(ControlNoteScaleVerbsTest|NoteToolsTest)$' --output-on-failure
# exit 8; five no-op undo-depth assertions failed
git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-24.r1/logs/fix.patch
# exit 0; fix restored for GREEN
```

Build and test logs are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-24.r1/logs`.
The session did not provide the `fff` grep tool, so log searches used `rg` with one
term per search (`error`, `FAILED`, and `Assertion`).
