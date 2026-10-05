# notes lane, batch 04 — six findings

### Cluster 5680 — Split-by-pitch keeps every cloned clip at the source clip's start
Verdict: FIXED
Evidence: `keyTrack()` identified the source clip's twin in the clone by comparing start
positions (`src/core/ControlCommandsNoteMoves.cpp:90,94`), so any other clip on the source
track starting at the same offset survived the cleanup and was filtered into every generated
key track. A new `clipOrder()` helper names the twin by its place in the clip order, which
`Track::clone()` (`src/Track.cpp:422`) preserves, and every other clone is dropped.
Commit: b92715e81
Test: NoteToolsTest::aSplitKeepsOnlyTheClonedCounterpartOfTheNamedClip
Red/green: red `cmake --build build --target zene NoteToolsTest` EXIT=0 and
`cd build/tests && ctest -R '^NoteToolsTest$' --output-on-failure` EXIT=8 (`FAIL!` — made
track held 2 clips, expected 1); green the same build EXIT=0 and
`ctest -R '^(NoteToolsTest|StemSplitPipelineTest)$' --output-on-failure` EXIT=0 (2/2). Both
gates EXIT=0.

### Cluster 4810 — `note.split_by_pitch` redo silently does nothing after the source track is restored
Verdict: FIXED
Evidence: the redo closure held `QPointer<Track>`/`QPointer<MidiClip>` of the source
(`src/core/ControlCommandsNoteMoves.cpp:113-114`) and returned early when either was null.
Undoing a `track.remove` rebuilds that track as a new object, so both pointers were null at
redo time and no track was built. The closure now captures the clip's persistent name and
resolves the live clip and its track through `resolveMidiClip()` inside the closure, matching
the ids-not-pointers rule the surrounding code already follows.
Commit: 394fbbd49
Test: NoteToolsTest::aSplitIsRebuiltAfterItsSourceTrackIsRemovedAndUndone
Red/green: red `cmake --build build --target zene NoteToolsTest` EXIT=0 and
`ctest -R '^NoteToolsTest$' --output-on-failure` EXIT=8 (`FAIL!` — 5 tracks after the redo,
expected 7); green the same build EXIT=0 and
`ctest -R '^(NoteToolsTest|StemSplitPipelineTest)$' --output-on-failure` EXIT=0 (2/2). Both
gates EXIT=0.

### Cluster 5684 — Articulation updates can stall the audio callback
Verdict: SKIPPED-SCOPE
Evidence: `writeArticulations()` takes `track->lock()` around
`articulations().set(entries)` (`src/core/ControlCommandsNotePlayback.cpp:153-161`), and
`InstrumentTrack::sendArticulation()` reads that map under `play()`'s lock
(`src/tracks/InstrumentTrackNotes.cpp:159-162`, `include/InstrumentTrack.h:97` documents
"write with the track locked"). The report is accurate, but the fix it asks for — publishing
an immutable map through an RT-safe swap — changes the audio-thread read path, which rule 6
forbids this lane from touching.
Commit: none
Test: none
Red/green: none

### Cluster 2858 — NaN probability passes validation and can be stored on a note
Verdict: FIXED
Evidence: `checkProbability()` compared the value against 0 and 1 with two plain comparisons
(`src/core/ControlCommandsNoteProbability.cpp:81-90`), and both are false for NaN, so a
non-finite value passed; `Note::setProbability`'s `std::clamp` then returns NaN unchanged. The
check now starts with `!std::isfinite(probability)`. Infinity was already refused by the
existing `> 1` comparison, so the fix is specifically about NaN.
Commit: 43bf38e70
Test: ControlVerbInverseTest::noteProbabilitySetRefusesOutsideTheRange (extended with
`quiet_NaN()` and `infinity()`)
Red/green: red `cmake --build build --target zene ControlVerbInverseTest` EXIT=0 and
`ctest -R '^ControlVerbInverseTest$' --output-on-failure` EXIT=8 (`FAIL! … '!notANumber.ok'
returned FALSE`); green the same build EXIT=0 and
`ctest -R '^(ControlVerbInverseTest|ClipLinkTest|MidiProbabilityPersistenceTest|ControlNoteScaleVerbsTest)$'`
EXIT=0 (4/4). Both gates EXIT=0.

### Cluster 5688 — Probability and slide edits do not propagate to linked clips
Verdict: FIXED
Evidence: `note.probability_set`, `note.slide_set` and `note.slide_clear` changed per-note
serialized state and returned without touching the group
(`src/core/ControlCommandsNoteProbability.cpp`, `src/core/ControlCommandsNoteSlide.cpp`),
while linked clips are only kept in step by `ClipLinks::mirrorContent()` (`include/ClipLinks.h:136`).
Each verb now mirrors the edited clip; a `publishSlideEdit()` helper was extracted so
`slideClear` stayed under the CCN 10 ratchet.
Commit: 58be4ef18
Test: ClipLinkTest::probabilityAndSlideEditsAreSeenByTheOtherMember
Red/green: red `cmake --build build --target zene ClipLinkTest` EXIT=0 and
`ctest -R '^ClipLinkTest$' --output-on-failure` EXIT=8 (`FAIL! … a probability written on one
member was never mirrored`); green the same build EXIT=0 and
`ctest -R '^(ClipLinkTest|ControlNoteScaleVerbsTest|ControlVerbInverseTest|ClipLinkPersistenceTest|SlideNotesTest|MidiProbabilityPersistenceTest)$'`
EXIT=0 (6/6). Both gates EXIT=0.

### Cluster 2868 — Fractional seeds are silently truncated
Verdict: FALSE-POSITIVE
Evidence: `readSeed()` itself only checks numeric-ness and range
(`src/core/ControlCommandsNoteRandom.cpp:131-147`), but both callers declare `seed` as
`integerProperty(0, kMaxSchemaInteger)` (`src/core/ControlCommandsNoteRandom.cpp:391,425`) and
`ControlRegistry::invoke()` validates arguments before any handler runs
(`src/core/ControlRegistry.cpp:251`). `typeMatches()` rejects a non-integral double with
`std::floor(value.toDouble()) != value.toDouble()` (`src/core/ControlSchema.cpp:52`), so
`seed: 12.9` is refused with `InvalidArgs` before `readSeed()` is reached. NaN is refused by the
same check (`std::floor(NaN)` is NaN, and NaN != NaN).
Commit: none
Test: none
Red/green: none

Leftovers: nothing half-done. Cluster 5684 is left for a change that may touch the
audio-thread read path, which this lane must not do.