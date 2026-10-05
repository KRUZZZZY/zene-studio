# notes lane, batch 05 — six findings

All six findings verified against the current tree with direct source evidence; all six were
REAL and all six are now fixed, each with a regression test that was confirmed RED before the
product change and GREEN after it.

### Cluster 3918 — Randomize succeeds on an empty selection
Verdict: FIXED
Evidence: `randomize()` (`src/core/ControlCommandsNoteRandom.cpp`) called `scopeNotes()`,
then took an undo checkpoint and returned success without ever checking that the scope was
non-empty, so `scope:"selection"` with nothing selected reported `notes_rolled:0` and recorded
a checkpoint that undid nothing. The empty-scope refusal now happens before
`snapshotNotes()`/`addJournalCheckPoint()`, mirroring the existing precedent at
`src/core/ControlCommandsNoteTransform.cpp:110-117`. `randomize()` was already at its CCN
baseline of 16, so the extra branch was paid for by extracting the roll loop into a new
`rollNotes()` helper rather than growing the function past the complexity target.
Commit: 842c28d47
Test: ControlNoteScaleVerbsTest::theRollRefusesWhatItCannotMean
Red/green: red `cmake --build build --target zene ControlNoteScaleVerbsTest` EXIT=0 and
`cd build/tests && ctest -R '^ControlNoteScaleVerbsTest$' --output-on-failure` EXIT=8
(`FAIL!` — `emptyScope.errorKind` actual 0, expected `Refused` (5), at line 341); green the
same build EXIT=0 and
`ctest -R '^(ClipLinkTest|ClipLinkPersistenceTest|ControlNoteScaleVerbsTest|ControlVerbInverseTest|SlideNotesTest|MidiProbabilityPersistenceTest|NoteRandomTest)$' --output-on-failure`
EXIT=0 (7/7). Both gates EXIT=0.

### Cluster 5689 — Randomizing a linked clip leaves its siblings stale
Verdict: FIXED
Evidence: the roll rewrote note positions and velocities and then re-sorted the clip, but the
handler returned without mirroring the result to linked clips, so only the clip named in the
command changed and its siblings kept the pre-roll content. `randomize()` now mirrors through
`ClipLinks::mirrorContent(clip)` when `clip->linkId() > 0`, after `rearrangeAllNotes()` and
`dataChanged()` — the same pattern already used at
`src/core/ControlCommandsNoteProbability.cpp:148` (precedent commit `58be4ef18`).
Commit: 8cb4fc739
Test: ClipLinkTest::aRollIsSeenByTheOtherMember
Red/green: red `cmake --build build --target zene ClipLinkTest` EXIT=0 and
`cd build/tests && ctest -R '^ClipLinkTest$' --output-on-failure` EXIT=8 (`FAIL!` —
`'after != before' returned FALSE` at line 405); green the same build EXIT=0 and
`ctest -R '^(ClipLinkTest|ClipLinkPersistenceTest|ControlNoteScaleVerbsTest|ControlVerbInverseTest|SlideNotesTest|MidiProbabilityPersistenceTest|NoteRandomTest)$' --output-on-failure`
EXIT=0 (7/7). The regression asserts both the other member's notes through the public surface
and `divergentCount(a) == 0`. Both gates EXIT=0.

### Cluster 4816 — `note.add_many` accepts positions that can overflow note end calculations
Verdict: FIXED
Evidence: `readNewNote()` (`src/core/ControlCommandsNoteShape.cpp`) validated only that
position was nonnegative and length positive, never that their sum stayed inside the song. A
note at `2147483600` with length `100` was accepted and its end computed past `tick_t`, and
past `include/Song.h:77`'s `MaxSongLength` in any case. The new `checkNewEndPosition()`
sums in `qint64` and refuses `end > MaxSongLength` before the note is constructed, mirroring
the precedent `checkEndPosition()` from commit `72e123645`. The bound is enforced in the
handler; the schema was left alone so the `ControlCommandsSnapshot` canary does not move.
Commit: 9f89beb4e
Test: NoteShapeValidationTest::addManyRefusesANoteThatEndsPastTheSongLimit
Red/green: red `cmake --build build --target zene NoteShapeValidationTest` EXIT=0 and
`cd build/tests && ./NoteShapeValidationTest addManyRefusesANoteThatEndsPastTheSongLimit`
EXIT=1 (`FAIL!` — `errorKind` actual 0, expected `Refused` (3)); green the same build EXIT=0
and `./NoteShapeValidationTest addManyRefusesANoteThatEndsPastTheSongLimit` EXIT=0, plus
`ctest -R '^(NoteEditingCommandsTest|NoteEditRangeTest)$' --output-on-failure` EXIT=0 (2/2)
and the full `ctest -R '^NoteShapeValidationTest$'` EXIT=0 (4/4). Both gates EXIT=0.
This commit also lands the new `tests/src/core/NoteShapeValidationTest.cpp` binary and its
three registrations; its body names the three cases that were still RED at that point and
points at the three following commits.

### Cluster 2873 — A malformed transform filter can become an unfiltered destructive edit
Verdict: FIXED
Evidence: `filterFrom()` read `filter.scale` with `toString()` and `filter.invert` with
`toBool()`, so a non-string scale silently became the empty string — which the transform
treats as "no scale filter" — and a non-boolean invert silently became false. Combined with
an empty filter object that matches every note, `action:"remove"` with `filter:{"scale":42}`
deleted all notes in the clip. `filterFrom()` is now split into `readInvert()` (must be
boolean) and `readScale()` (must be a string, then `"in"`/`"out"` with a scale set).
Genuinely-absent values (undefined/null) still mean "not given", and an empty `scale` string
still means "no scale channel", so existing behaviour is preserved and only values of the
wrong *type* are refused. (`ControlSchema.cpp:46-59` shows `"object"` only checks `isObject()`,
and `ControlVocabulary.cpp:85-106` shows `objectProperty()` is bare, so nothing else caught this.)
Commit: d74e1dcbb
Test: NoteShapeValidationTest::aMalformedFilterFieldIsRefusedNotIgnored
Red/green: red `cd build/tests && ./NoteShapeValidationTest aMalformedFilterFieldIsRefusedNotIgnored`
EXIT=1 (`FAIL!` — the malformed scale was accepted rather than refused); green
`cmake --build build --target zene NoteShapeValidationTest` EXIT=0 and the same direct run
EXIT=0, plus `ctest -R '^(NoteEditingCommandsTest|NoteTransformTest)$' --output-on-failure`
EXIT=0 (2/2). Both gates EXIT=0. Note: the test's own helper had a defect found while
diagnosing this — its default `amount` parameter produced JSON null rather than undefined, so
every call was schema-refused and the case was not genuinely RED; the helper was corrected to
`QJsonValue(QJsonValue::Undefined)` before the RED was accepted.

### Cluster 2877 — `note.transform_where` accepts unbounded amounts
Verdict: REAL but narrower than reported (FIXED)
Evidence: the overflow half of the finding is a FALSE-POSITIVE — `transpose`
(`src/core/NoteTransform.cpp:147-162`) clamps semitones to ±NumKeys before adding, and
`offsetVelocity` (lines 166-181) clamps the delta to ±MaxVolume, so `INT_MAX` cannot overflow
that arithmetic. The provable defect is real and narrower: `amount` is declared unbounded
(`integerProperty()`, `src/core/ControlVocabulary.cpp:85`) and narrowed with `toInt()`, so a
fractional or out-of-`int` value (e.g. `2147483648.0`) reads as `0` and produces a success
that moved nothing. The new `readAmount()` requires a whole number inside `int` range and
refuses anything else; `transformWhere()` validates `amount` when present and passes the
checked `int` to `act()`. The bound lives in the handler rather than the schema deliberately,
because editing `argsSchema` would move the `ControlCommandsSnapshot` canary (rule 8).
Commit: 4e288ecb5
Test: NoteShapeValidationTest::aTransformAmountMustBeAWholeNumberTheHandlerCanHold
Red/green: red `cd build/tests && ./NoteShapeValidationTest aTransformAmountMustBeAWholeNumberTheHandlerCanHold`
EXIT=1 (`FAIL!` — `2147483648.0` was accepted); green
`cmake --build build --target zene NoteShapeValidationTest` EXIT=0 and the same direct run
EXIT=0, plus `ctest -R '^(NoteEditingCommandsTest|NoteTransformTest)$' --output-on-failure`
EXIT=0 (2/2). Both gates EXIT=0.

### Cluster 3898 — `note.select_where` leaves the selection index store stale
Verdict: FIXED
Evidence: `selectWhere()` (`src/core/ControlCommandsNoteShape.cpp`) set each note's `selected`
flag and returned without touching the selection index store that `control::selectNotes()`
owns (`src/core/ControlEditSupport.cpp:262-278`), so `roll.get_state` and the next
`note.select` still reported the selection the verb had just replaced — `note.select` writes
both halves (`src/core/ControlCommandsNotes.cpp:477`). `selectWhere()` now indexes the clip's
notes and calls `control::selectNotes(clipId(ref.id), noteCount, wanted)` before emitting
`dataChanged()`. The index list is built from the *resulting* selection, so the notes that
`additive` keeps are carried in alongside the filter hits; the reported `selected`/`previous`
arrays keep their existing meaning.
Commit: 18d362ce0
Test: NoteShapeValidationTest::select_whereWritesTheSelectionStoreToo
Red/green: red `cd build/tests && ./NoteShapeValidationTest select_whereWritesTheSelectionStoreToo`
EXIT=1 (`FAIL!` — the store held 1 index where the flags held 2); green
`cmake --build build --target zene NoteShapeValidationTest NoteEditingCommandsTest` EXIT=0 and
the same direct run EXIT=0, plus
`ctest -R '^(NoteShapeValidationTest|NoteEditingCommandsTest|NoteEditorTest|ControlVerbInverseTest|ClipLinkTest)$' --output-on-failure`
EXIT=0 (5/5, i.e. all four NoteShapeValidationTest slots green together). Both gates EXIT=0.

Leftovers: none. Cluster 2877's reported `INT_MAX` overflow in transpose/velocity-offset
arithmetic remains unfixed *by design* — that half of the finding is a false positive, since
both `NoteTransform.cpp:147-162` and `:166-181` already clamp; only the handler's silent
`toInt()` narrowing was real and is fixed.