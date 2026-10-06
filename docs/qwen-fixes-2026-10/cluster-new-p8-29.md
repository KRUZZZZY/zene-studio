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
- `src/core/Note.cpp:411` — `Note::loadSettings` reads the length straight from XML
  with no `MaxSongLength` clamp, so a loaded note's `tick_t` length is NOT schema-bounded.

## Fix
`src/core/ControlCommandsNotes.cpp` (registerNoteMove handler): refuse the move
before any mutation when the note's end would pass `MaxSongLength`, typed
`ControlErrorKind::InvalidArgs` — the same kind the schema's own range rejection and
the sibling semantic range checks use (e.g. `ControlCommandsPunch.cpp:150`
"'end' is past the end of the timeline").

Rework (Luna r1): the first cut added two 32-bit `tick_t` values, which overflows for
a loaded length near `INT_MAX`. The guard now computes the end in a wide type BEFORE
comparing — `const qint64 end = static_cast<qint64>(position) + static_cast<qint64>(note->length().getTicks());`
— compares `end > MaxSongLength`, and reports the wide `end` in the error message.
The comment claiming "both operands are schema-bounded" was replaced with one stating
the opposite: lengths are NOT schema-bounded everywhere (`Note::loadSettings` reads
them straight from XML), which is exactly why the wide computation is required.

## Regression
`tests/src/core/ControlEditCommandsTest.cpp` — two slots:
- `noteMovePastSongLimitIsRefused()` (round 1): a 24-tick note moved to `MaxSongLength`
  is refused, typed, with nothing changed.
- `noteMoveWithHugeLoadedLengthIsRefused()` (this round): a note whose model length is
  set to 2,147,483,000 ticks (near `INT_MAX`, what `Note::loadSettings` could produce)
  is moved to `MaxSongLength`; asserts `InvalidArgs` and that the note is untouched
  (position 0, length unchanged). With the int32 guard the sum wraps negative and the
  move is applied — the slot fails.

## Commands run (unpiped exit codes, this round)
LOGS = `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p8-29.r2/logs`

| step | command | EXIT |
|---|---|---|
| build (RED state) | `cmake --build build --target ControlEditCommandsTest -j12 > LOGS/build-1.log 2>&1` | 2 (test TU compile error: `ClipRef` needed `control::` qualification) |
| build (RED state, fixed TU) | `cmake --build build --target ControlEditCommandsTest -j12 > LOGS/build-2.log 2>&1` | 0 |
| RED run | `ctest -R ControlEditCommandsTest --output-on-failure > LOGS/test-1.log 2>&1` (from build/tests) | 8 |
| build (GREEN) | `cmake --build build --target zene ControlEditCommandsTest -j12 > LOGS/build-3.log 2>&1` | 0 |
| GREEN run | `ctest -R ControlEditCommandsTest --output-on-failure > LOGS/test-2.log 2>&1` (from build/tests) | 0 |
| complexity gate | `bash tests/complexity-gate.sh --check > LOGS/gate-1.log 2>&1` | 0 |
| file-length gate | `bash tests/file-length-gate.sh --check > LOGS/gate-2.log 2>&1` | 0 |

RED/GREEN semantics: the RED run used the committed int32 guard — i.e. only the new
product hunk (the qint64 end computation) reversed — and the GREEN run restored it.

RED assertion (test-1.log):
`FAIL!  : ControlEditCommandsTest::noteMoveWithHugeLoadedLengthIsRefused() '!moved.ok' returned FALSE. (a move ending past the song limit was applied)` —
`Totals: 10 passed, 1 failed, 0 skipped, 0 blacklisted, 1279ms`.

GREEN (test-2.log): `1/1 Test #34: ControlEditCommandsTest .......... Passed` —
`100% tests passed, 0 tests failed out of 1` (the binary exits 0, i.e. all 11
QtTest slots pass; ctest hides per-slot output on success).

Affected-family test count: 1 ctest entry (`ControlEditCommandsTest`), 11 QtTest
slots (10 pre-existing + the new regression slot).

## Files committed
- `src/core/ControlCommandsNotes.cpp` (the wide-typed guard)
- `tests/src/core/ControlEditCommandsTest.cpp` (the overflow regression test)
- `docs/qwen-fixes-2026-10/cluster-new-p8-29.md` (this report)

Both source files are already registered in `tests/fork-sources.txt` (line 1933 for
the test; the product file was registered when the first fix landed) — no manifest
change was needed.
