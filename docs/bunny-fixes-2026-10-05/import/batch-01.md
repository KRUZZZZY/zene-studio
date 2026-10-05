# Bunny import lane — batch-01 (2026-10-05)

Lane: DAWproject import (mixer snapshots in import undo, undo byte accounting, typed
refusal preservation). Branch `bugs/luna-import-2026-10-05`.

Six findings: five were already fixed by commits earlier on this branch (verified by
reading the current code and running the tests green), one was REAL and is fixed by a
new commit. One is a design decision, left alone.

### Cluster 5633 — The import undo budget omits mixer and tempo-map snapshot data

Verdict: ALREADY-FIXED

Evidence: `captureSession` now counts all three retained components —
`src/core/ControlCommandsDawProject.cpp:173` (track XML), `:180` (each mixer's compact
JSON) and `:183` (`map.size() * sizeof(TempoMapEvent)`) — and `SessionSnapshot::payloadBytes`
documents the budget contract at `:139-143`. The redo document is added on top at
`:505` via `modelDocumentBytes()` (`:238`). Fix commit `7732321fe`.

Commit: 7732321fe

Test: DawProjectInterchangeRoundTripTest::importUndoBudgetCountsBothSessionAndRedoDocuments

Red/green: none in this lane (fixed before the batch). Confirmed green:
`cd build/tests && ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure` -> EXIT=0.

### Cluster 5632 — Import undo does not remove mixer channels added by the import

Verdict: ALREADY-FIXED

Evidence: `restoreSession` trims the mixer top-down to the captured channel count at
`src/core/ControlCommandsDawProject.cpp:218-222`, after restoring the captured strips
(`:202-212`); the loop mirrors `applyDawProjectModel`'s create-what-is-missing loop
(`src/core/DawProjectSession.cpp:399`). Fix commit `6cfbafa33`.

Commit: 6cfbafa33

Test: DawProjectInterchangeRoundTripTest::importIsOneUndoStep

Red/green: none in this lane (fixed before the batch). Confirmed green: same ctest
command -> EXIT=0.

### Cluster 5634 — The import transaction records the post-import track count as its before-state

Verdict: ALREADY-FIXED

Evidence: the transaction's `before` half reads the CAPTURED count via
`before.trackXml.size()` at `src/core/ControlCommandsDawProject.cpp:530`, not
`song->tracks().size()` read after `applyDawProjectModel`. Fix commit `52631c614`.

Commit: 52631c614

Test: DawProjectInterchangeRoundTripTest::importIsOneUndoStep

Red/green: none in this lane (fixed before the batch). Confirmed green: same ctest
command -> EXIT=0.

### Cluster 5635 — dawproject.import omits a field declared in its result schema

Verdict: ALREADY-FIXED

Evidence: the handler inserts `after.mixerChannelCount()` at
`src/core/ControlCommandsDawProject.cpp:511`, matching the schema declaration at
`:454`. Fix commit `ba1b0411c`.

Commit: ba1b0411c

Test: DawProjectInterchangeRoundTripTest::importIsOneUndoStep

Red/green: none in this lane (fixed before the batch). Confirmed green: same ctest
command -> EXIT=0.

### Cluster 1057 — DAWproject export overwrites files without a transaction

Verdict: SKIPPED-SCOPE

Evidence: `dawproject.export` is classified `RC::NotMutating` with the reasoning
written down at `src/core/ControlReversibilityTableDawProject.cpp:88-94` and again in
the file header at `:14-19`: it writes a file OUTSIDE the project and changes no
session state, so there is nothing for a session inverse to restore. It refuses an
existing file unless `overwrite` is true (`src/core/ControlCommandsDawProject.cpp:347`),
and the reply carries path, bytes, sha256 and model digest so the caller can identify
or delete what it wrote. `project.save` carries the same class on the same ground
(`src/core/ControlReversibilityTableSnapshot.cpp:73`). Adding a bounded file-revision
mechanism is a new reversal class for out-of-project artefacts — a design change well
past a defect fix, and it would move the reversibility table the ControlCommandsSnapshot
canary reads. Rule 8 applies.

Commit: none

Test: none

Red/green: none

### Cluster 5065 — Beat-to-tick conversion has undefined behavior for invalid values

Verdict: FIXED

Evidence: `dawProjectTicksFromBeats` cast the rounded double straight to `qint64`
(`src/core/DawProjectModel.cpp:71` at the reported revision) — undefined behaviour for a
non-finite or out-of-range value — and nothing upstream validated the beat. The reader
takes `@time` straight from `doubleAttribute`
(`src/core/DawProjectReadShared.h:110`), which accepts `1e300`, and the writer validates a
point's VALUE but never its time (`src/core/DawProjectWrite.cpp:176-199`), so such a
document is both writable and importable. RED proved reachability: NaN produced
`-9223372036854775808` and the import returned ok. The conversion now refuses a value
that is not a beat and reports the grid's origin; the new
`dawProjectBeatRepresentable()` lets the applier refuse the point as a typed refusal
naming the offending value. The two automation loops were extracted into
`addTempoPoints()`/`addMeterPoints()` because the complexity ratchet rejected the inline
guards (33 -> 37); `applyDawProjectModel` keeps its previous complexity.

Commit: fb9b053d4

Test: DawProjectInterchangeRoundTripTest::beatConversionRejectsValuesThatAreNotBeats
and ::importRefusesAnUnrepresentableBeat

Red/green:
`cmake --build build --target zene DawProjectInterchangeRoundTripTest` -> EXIT=0, then
`cd build/tests && ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure` -> EXIT=8 (RED: NaN -9223372036854775808 vs 0; `!result.ok` FALSE)
Green, same commands after the fix and the helper extraction -> EXIT=0.
Covering suites: `ctest -R '^(ControlSurfaceReferenceTest|RevisionTimelineTest|ProjectContainerTest|ReversibilityContractTest|TrackRenderMenuTest|DawProjectInterchangeRoundTripTest)$' --output-on-failure` -> EXIT=0 (6/6).
Gates: `bash tests/complexity-gate.sh --check` -> EXIT=0; `bash tests/file-length-gate.sh --check` -> EXIT=0.

Leftovers: cluster 1057 stays unfixed by design — if the parent wants export to carry an
undoable file revision, that is a new reversal class and a reversibility-table change for
the ControlCommandsSnapshot canary's lane, not a defect fix. The reader still does not
validate `@time` itself; the refusal now happens at the conversion and the applier, which
covers import, but `dawproject.read` still reports such a point in its model rather than
refusing it (a read that does not mutate anything, so no undefined conversion runs).
