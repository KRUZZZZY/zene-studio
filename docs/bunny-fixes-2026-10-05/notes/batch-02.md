# notes lane, batch 02 — six findings

### Cluster 4782 — Splitting a partial final pass comps an uncovered full loop
Verdict: FIXED
Evidence: `src/core/ControlCommandsCompGroup.cpp:279` selected the last pass's lane over
`[begin, begin + loop)` whatever the last piece's length, so a clip of 3.5 passes named a lane with
no take behind it over the tail — the exact case `TakeLaneModel::takeAt()` reports `unresolved` and
the engine plays nothing for. Now only `min(loop, pieces.back()->length())` is composited.
Commit: 95d9e1844
Test: RecordToolsTest::splittingPassesCompsOnlyWhatTheLastPassCovers
Red/green: red `cmake --build build --target zene RecordToolsTest` EXIT=0 and
`cd build/tests && ctest -R '^RecordToolsTest$' --output-on-failure` EXIT=8
(`FAIL! compared 384 != 288`); green the same two commands EXIT=0 / EXIT=0.

### Cluster 1041 — `comp.split_passes` checkpoints the track but returns no transaction
Verdict: FIXED
Evidence: `splitPasses` called `ref.track->addJournalCheckPoint()` but returned no `__transaction`
(`ControlCommandsCompGroup.cpp:265` and the plain success at `:281`), unlike its siblings `moveEdge`
(`:174-181`) and `selectGroup` (`:236-239`) — so the record carried no composite before-state and no
inverse. It now returns the Track checkpoint transaction, mirroring `moveEdge`.
Commit: e547c8f26
Test: RecordToolsTest::splittingPassesRecordsItsBeforeStateAndInverse
Red/green: red build EXIT=0 and `ctest -R '^RecordToolsTest$' --output-on-failure` EXIT=8
(`FAIL! 'before' contained no 'composite' key`); green EXIT=0 / EXIT=0.

### Cluster 4805 — `note.across` can leave a partially applied batch
Verdict: SKIPPED-SCOPE
Evidence: real in principle — `prepare()` (`src/core/ControlCommandsNoteAcross.cpp:62-88`) validates
schemas and clip ids only, so a clip-specific refusal such as `note.scale` hitting `MaxSongLength`
(`src/core/ControlCommandsNoteShape.cpp:87-91`) is discovered mid-loop and the batch still returns
success for the clips that did change. Fixing it as proposed means either preflighting every
handler's own refusal condition (eleven handlers, each with its own rules — a redesign, not a
minimal change) or rolling back successful calls (needs an undo-during-handler path `across` has no
access to from inside the registry's own invocation). No small correct fix; code left alone. Note the
per-clip outcome is already reported honestly in the `clips` array
(`ControlCommandsNoteAcross.cpp:125-127`), so the caller is not misled about which clips changed.
Commit: none
Test: none
Red/green: none

### Cluster 4804 — `note.across` counts no-op commands as edits
Verdict: FIXED
Evidence: `ControlCommandsNoteAcross.cpp:126` incremented `edited` for any `done.ok`, and the outer
handler then returned a `control.undo` transaction. `note.restore_timing` succeeds with `restored: 0`
and returns no transaction when no note keeps an original timing
(`src/core/ControlCommandsNoteShape.cpp:196-200`), so the batch claimed edits and an inverse that no
undo step backs. A clip now counts only when its handler described a transaction.
Commit: b3b437339
Test: NoteCurvesAcrossTest::anEditThatChangedNothingIsNotAnUndoStep
Red/green: red `cmake --build build --target zene NoteCurvesAcrossTest` EXIT=0 and
`cd build/tests && ctest -R '^NoteCurvesAcrossTest$' --output-on-failure` EXIT=8 (`FAIL!`, no NotFound
raised); green EXIT=0 / EXIT=0. `ctest -R '^UndoHonestyTest$'` EXIT=0.

### Cluster 2857 — NaN pitch-curve points can reach tick rounding
Verdict: FIXED
Evidence: `readCurve` (`src/core/ControlCommandsNoteCurves.cpp:90`) ranged over `at`/`semitones`
without a finiteness test, and every comparison against NaN is false, so a NaN reached
`std::lround` in `putCurve`. The finding's own doubt about reachability does not hold: Qt6's
`QJsonValue` does store a NaN, which the new regression case demonstrated (`accepted a NaN tick`).
Both values are now required finite before the range check.
Commit: 2391e4155
Test: NoteCurvesAcrossTest::aNonFiniteCurvePointIsRefused
Red/green: red build EXIT=0 and `ctest -R '^NoteCurvesAcrossTest$' --output-on-failure` EXIT=8
(`FAIL! 'accepted a NaN tick'`); green EXIT=0 / EXIT=0.

### Cluster 1133 — Multi-note pitch-curve transactions capture only one note's previous curve
Verdict: ALREADY-FIXED
Evidence: `curvesBefore` (`src/core/ControlCommandsNoteCurves.cpp:119-126`) already records every
target note's prior curve, one `{note, points}` entry each under `notes`, and the legacy `points`
key kept for readers of it; the helper's own comment names this past defect. Regression-covered by
`UndoHonestyTest::aCurveWriteRecordsEveryNotesBefore` (`tests/src/core/UndoHonestyTest.cpp:377-391`).
No code change.
Commit: none
Test: UndoHonestyTest::aCurveWriteRecordsEveryNotesBefore
Red/green: none

Leftovers: cluster 4805 is unfixed by choice — an all-or-nothing `note.across` needs a per-handler
preflight or a rollback path inside the registry invocation, which is more than a minimal change.