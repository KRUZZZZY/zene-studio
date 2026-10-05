# notes lane, batch 07 — six findings

All six findings were re-read in the current tree before any change: four were REAL and are
fixed, each with a regression test confirmed RED before the product change and GREEN after;
cluster 4813 is a FALSE-POSITIVE (`MidiClip::addNote` already recalculates the length, proven by
a test that passed unfixed); cluster 2870 is REAL but only in its `note.randomize` third of the
cited locations — the add/remove halves it also names are the already-handled ones from 4813.

### Cluster 4856 — Rotating a left-trimmed clip can move notes into its silent region
Verdict: FIXED
Evidence: `rotate` in `src/core/ControlCommandsNoteTools.cpp:264` computed the wrapping span as
`length - startTimeOffset()` but wrapped each note's ABSOLUTE position from zero, so a note
whose offset from the clip start was under the trimmed length was pushed before the audible
region. Positions are now wrapped relative to `startTimeOffset()` and the offset added back.
Commit: d527dc72c
Test: NoteToolsTest::rotatingALeftTrimmedClipKeepsEveryNoteInTheAudibleRegion
Red/green: red `cmake --build build --target zene NoteToolsTest` EXIT=0 and
`cd build/tests && ctest -R '^NoteToolsTest$' --output-on-failure` EXIT=8 (the note landed at
tick 10 where the test expected 90); green
`cmake --build build --target zene NoteToolsTest` EXIT=0 and
`ctest -R '^NoteToolsTest$' --output-on-failure` EXIT=0. Both gates EXIT=0.

### Cluster 5693 — Rhythm generation silently clamps pulses to steps
Verdict: FIXED
Evidence: `generate` in `src/core/ControlCommandsNoteTools.cpp:302` ran
`std::clamp(pulses, 0, steps)`, so a schema-valid request (pulses 0..128) succeeded with fewer
pulses than asked for. The handler now refuses `pulses > steps` with `InvalidArgs` — the
handler-side refusal precedent of cf5504f80 — so no params or result schema (and no
`ControlCommandsSnapshot` canary entry) moved.
Commit: 61cecbcaf
Test: NoteToolsTest::morePulsesThanStepsIsRefusedNotSilentlyReduced
Red/green: red `cmake --build build --target zene NoteToolsTest` EXIT=0 and
`cd build/tests && ctest -R '^NoteToolsTest$' --output-on-failure` EXIT=8 (errorKind `Ok` where
`InvalidArgs` was expected); green the same build EXIT=0 and ctest EXIT=0. Both gates EXIT=0.

### Cluster 1154 — Random generation can checkpoint without adding notes
Verdict: FIXED
Evidence: the checkpoint at `src/core/ControlCommandsNoteTools.cpp:310` was taken before the
random draw, so a roll of zero hits still pushed an undo step and answered `added:0`. The hit
set is now decided first and, when empty, the handler returns without journalling — the
checkpoint-after-decision pattern `slideClear` already uses (01c82dbbe).
Commit: 5b437f7fd
Test: NoteToolsTest::aRandomRhythmThatHitsNothingTakesNoUndoStep
Red/green: red `cmake --build build --target zene NoteToolsTest` EXIT=0 and
`cd build/tests && ctest -R '^NoteToolsTest$' --output-on-failure` EXIT=8 (undo depth 21 where
20 was expected); green the same build EXIT=0 and ctest EXIT=0. Both gates EXIT=0.

### Cluster 5690 — Note transforms leave linked clips stale
Verdict: FIXED
Evidence: `src/core/ControlCommandsNoteTransform.cpp` did not include `ClipLinks.h`, and none of
`transpose` (~210-242), `velocityOffset` (~244-272) or `velocityScale` (~274-301) called
`ClipLinks::mirrorContent`, unlike `note.move`, `note.resize`, `note.velocity_set` and
`note.randomize`. All three handlers now mirror when `linkId() > 0`.
Commit: df11d9ddb
Test: ClipLinkTest::aTransformIsSeenByTheOtherMember
Red/green: red `cmake --build build --target zene ClipLinkTest` EXIT=0 and
`cd build/tests && ctest -R '^ClipLinkTest$' --output-on-failure` EXIT=8 (the other member still
held key 60 instead of 72); green the same build EXIT=0 and ctest EXIT=0. Both gates EXIT=0.

### Cluster 4813 — `note.add` does not recalculate an auto-resizing clip's length
Verdict: FALSE-POSITIVE
Evidence: `note.add` at `src/core/ControlCommandsNotes.cpp:133` calls `MidiClip::addNote`, and
`MidiClip::addNote` (`src/tracks/MidiClip.cpp:182`) already calls `updateLength()` at line 197
before the handler's own `dataChanged()`. A regression case
(`addingANotePastTheClipEndGrowsAnAutoResizingClip`, written first in
`tests/src/core/NoteEditingCommandsTest.cpp`) added a note past the end of an auto-resizing
clip and passed with no product change, then was removed rather than left behind as a test that
proves nothing. The same holds for `note.remove` via `MidiClip::removeNote` (line 213).
Commit: none
Test: none
Red/green: none — the candidate test passed unmodified, which is the evidence for this verdict.

### Cluster 2870 — Some note edits leave auto-resized clip lengths stale
Verdict: FIXED
Evidence: the report's cited `src/core/ControlCommandsNotes.cpp:133-135` and `198-200` are the
add/remove paths already covered above (the handler need not call `updateLength`; `MidiClip`
does it), so that half of the finding is a DUPLICATE of cluster 4813. The `ControlCommandsNoteRandom.cpp`
half is REAL: `randomize` rolled positions in place and then called `rearrangeAllNotes()`,
`dataChanged()` and `ClipLinks::mirrorContent()` at lines 343-345 with no `updateLength()`, so a
note rolled past the clip's end sat outside an auto-resizing clip. The handler now recomputes
the length, the same shape `finish()` uses (`src/core/ControlCommandsNoteShared.h`).
Commit: a0dfa86fd
Test: NoteToolsTest::aRandomizedClipGrowsToCoverTheNoteItMoved
Red/green: red `cmake --build build --target zene NoteToolsTest` EXIT=0 and
`cd build/tests && ctest -R '^NoteToolsTest$' --output-on-failure` EXIT=8 (`FAIL!` —
`clip->length().getTicks() >= clip->notes()[0]->endPos().getTicks()` returned FALSE, the clip
stayed 192 ticks with the note ending at 304); green
`cmake --build build --target zene NoteToolsTest NoteEditingCommandsTest
ControlNoteScaleVerbsTest ClipLinkTest NoteRandomTest` EXIT=0 and
`ctest -R '^(NoteToolsTest|NoteEditingCommandsTest|ControlNoteScaleVerbsTest|ClipLinkTest|NoteRandomTest)$'
--output-on-failure` EXIT=0 (5/5). Both gates EXIT=0.

Leftovers: nothing half-done. Two notes for the parent: (1) `NoteEditingCommandsTest.cpp` sits at
500 code lines, exactly on the file-length limit, so its next regression case has nowhere to go
and had to be placed in `NoteToolsTest.cpp` — that binary, not this one, now owns the note-roll
length case; (2) cluster 2870's add/remove locations are the same already-handled paths cluster
4813 names, so of six findings only one edit path in that report was actually stale.
