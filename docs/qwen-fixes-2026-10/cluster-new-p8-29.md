# cluster new-p8-29 — `note.move` can move a note's end past the song limit

Verdict: **FIXED**

## Finding
`note.move` validated the new START position (schema `position: 0..MaxSongLength`)
but never validated the note's resulting END. Moving a positive-length note to
`MaxSongLength` left the note ending past the song limit.

## Evidence
- `src/core/ControlCommandsNotes.cpp:221` — args schema:
  `{position: integerProperty(0, MaxSongLength)}` bounds the START only.
- `src/core/ControlCommandsNotes.cpp:246-247` (pre-fix) — the handler applied the
  position straight through: `clip->addJournalCheckPoint(); note->setPos(...)` with
  no end-of-note check.
- `include/Song.h:77` — `MaxSongLength = 9999 * DefaultTicksPerBar` (1,919,808 ticks).

## Fix
`src/core/ControlCommandsNotes.cpp` (registerNoteMove handler): refuse the move
before any mutation when `position + note->length().getTicks() > MaxSongLength`,
typed `ControlErrorKind::InvalidArgs` — the same kind the schema's own range
rejection and the sibling semantic range checks use (e.g.
`ControlCommandsPunch.cpp:150` "'end' is past the end of the timeline"). Both
operands are schema-bounded by `MaxSongLength`, so the sum cannot overflow.

## Regression
`tests/src/core/ControlEditCommandsTest.cpp` — new slot
`noteMovePastSongLimitIsRefused()`: adds a track/clip and a 24-tick note, moves it
to `MaxSongLength`, and asserts a typed refusal plus an unchanged note.

## Commands run (unpiped exit codes)
| step | command | EXIT |
|---|---|---|
| build (RED) | `cmake --build build --target ControlEditCommandsTest > LOGS/build-1.log 2>&1` | 0 |
| RED run | `ctest -R ControlEditCommandsTest --output-on-failure > LOGS/test-1.log 2>&1` (from build/tests) | 8 |
| build (GREEN) | `cmake --build build --target ControlEditCommandsTest > LOGS/build-2.log 2>&1` | 0 |
| GREEN run | `ctest -R ControlEditCommandsTest --output-on-failure > LOGS/test-2.log 2>&1` (from build/tests) | 0 |
| complexity gate | `bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1` | 0 |
| file-length gate | `bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1` | 0 |

LOGS = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-29.r1/logs`

RED assertion (test-1.log):
`FAIL! : ControlEditCommandsTest::noteMovePastSongLimitIsRefused() '!moved.ok' returned FALSE. (a move ending past the song limit was applied)` —
`Totals: 9 passed, 1 failed, 0 skipped, 0 blacklisted, 1260ms`.

GREEN (test-2.log): `1/1 Test #34: ControlEditCommandsTest .......... Passed` —
`100% tests passed, 0 tests failed out of 1` (the binary exits 0, i.e. all 10
QtTest slots pass; ctest hides per-slot output on success).

Affected-family test count: 1 ctest entry (`ControlEditCommandsTest`), 10 QtTest
slots (9 pre-existing + the new regression slot).

## Files committed
- `src/core/ControlCommandsNotes.cpp` (the fix)
- `tests/src/core/ControlEditCommandsTest.cpp` (the regression test)
- `docs/qwen-fixes-2026-10/cluster-new-p8-29.md` (this report)

Both source files are already registered in `tests/fork-sources.txt`
(lines 1211 and 1933) — no manifest change was needed.
