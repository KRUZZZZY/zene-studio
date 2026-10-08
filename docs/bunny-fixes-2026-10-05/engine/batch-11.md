# Engine batch-11 — Space Bunny fixer lane

Six findings; three fixed, one already fixed upstream of this batch, two recorded as
false positives with the line that settles them.

### Cluster 4322 — Note release velocity is ignored when sending note-off
Verdict: FALSE-POSITIVE
Evidence: `src/core/NotePlayHandle.cpp:439` passes the track's `baseVelocity()` into
`midiReleaseVelocity(...)`, which is the intended argument and not a substitute for the
note's own field: `src/core/Note.cpp:176-179` reads `m_releaseVelocity` from the note and
uses the passed value only as the base the release velocity is scaled against
(`m_releaseVelocity * midiBaseVelocity / DefaultVolume`, mirroring `Note::midiVelocity`
at `include/Note.h:337`). `-1` is the documented "unset" sentinel and falls back to 0 at
`src/core/Note.cpp:178`, which is the intended fallback. The note's release velocity
already reaches note-off.
Commit: none
Test: none
Red/green: none

### Cluster 4316 — Scale snapping can leave boundary notes out of scale
Verdict: FIXED
Evidence: `src/core/NoteTransform.cpp:100` searched `distance <= KeysPerOctave / 2`, so a
scale holding only pitch class 11 left key 0 unchanged (`isInScale` never true within six
semitones) even though key 11 is legal and in scale. The bound is now `distance < NumKeys`,
which keeps the existing down-then-up tie order and the documented "already in scale"
early return.
Commit: 76af578b2
Test: NoteTransformTest::snapToScaleMovesOnlyOutOfKeyNotes
Red/green: `cmake --build build --target zene NoteTransformTest` EXIT=0;
`cd build/tests && ctest -R '^NoteTransformTest$' --output-on-failure` EXIT=8
(`snapToScale({lowest},{11})` Actual 0, Expected 1) -> after the fix EXIT=0, plus
`ctest -R '^(ControlNoteScaleVerbsTest|NoteEditRangeTest|ControlGrooveCommandsTest|NoteTransformActionsTest)$'` EXIT=0.

### Cluster 3333 — Note transforms can overflow before clamping
Verdict: ALREADY-FIXED
Evidence: the two examples the report names are already guarded in the current tree.
`src/core/NoteTransform.cpp:155` bounds `semitones` with `std::clamp` *before* the key
addition (comment: "The shift is bounded first"), and `src/core/NoteTransform.cpp:173-174`
does the same for `delta` in `offsetVelocity` (comment: "no overflow"). The quantise
overhang is bounded at the caller instead: `src/core/ControlCommandsGrooveEdit.cpp:340`
reads `grid` through `readTicks(args, ..., 1, MaxSongLength, ...)` with the comment that
the bound "keeps the rounding (position + grid) inside tick_t", and `tick_t` is
`std::int32_t` (`include/LmmsTypes.h:35`) while `MaxSongLength` is `9999 * DefaultTicksPerBar`
(`include/Song.h:77`), so `pos + grid` cannot overflow. Nothing left to change.
Commit: none
Test: none
Red/green: none

### Cluster 5205 — Velocity scaling converts invalid or out-of-range results
Verdict: FIXED
Evidence: `src/core/NoteTransform.cpp:192` (old) computed
`std::lround(current * factor)` before the clamp, so a NaN factor passed straight through
`std::clamp` into `lround`'s domain and a large factor overflowed the product to infinity
the same way. Now a non-finite factor returns 0 changed (nothing moves), and the product is
clamped to the volume rails as a float *before* rounding.
Commit: c3b0c5a4d
Test: NoteTransformTest::velocityTransforms
Red/green: `cmake --build build --target zene NoteTransformTest` EXIT=0;
`cd build/tests && ctest -R '^NoteTransformTest$' --output-on-failure` EXIT=8
(`scaleVelocity({a}, NaN)` Actual 1, Expected 0) -> after the fix EXIT=0, plus
`ctest -R '^(NoteTransformTest|MultiTrackRecorderTest|ControlNoteScaleVerbsTest|NoteEditRangeTest|ControlGrooveCommandsTest|NoteTransformActionsTest)$'` EXIT=0.

### Cluster 5246 — Changing a recording tap can create concurrent ring-buffer producers
Verdict: SKIPPED-SCOPE
Evidence: the observation is accurate as written (`src/core/RecorderTaps.cpp:88-99`
publishes a new tap with a single `entry.store`, with no hand-off from the old producer),
but both candidate producers run on the one render thread — `channelAt` is called from
`src/core/Mixer.cpp:708,811,812` and `master` from `src/core/Mixer.cpp:2193`, all inside
the same mixer render pass, so for a given period a route is pushed by at most one of them.
Making the change take effect at a synchronized period boundary needs new render-path
state and coordination, which rule 6 puts off this lane.
Commit: none
Test: none
Red/green: none

### Cluster 5252 — Whitespace in a take path can hide its recovery journal
Verdict: FIXED
Evidence: `src/core/RecordingJournal.cpp:184` called `.trimmed()` on the whole line before
`applyLine` split it, so a take path ending in a space came back without it and
`isRecoverable`'s `QFileInfo::exists` check (`src/core/RecordingJournal.cpp:202`) looked at
the wrong path. The line is no longer trimmed; `applyLine` trims only the key
(`src/core/RecordingJournal.cpp:132-137`) and the value is carried verbatim.
Commit: 5194ac02f
Test: MultiTrackRecorderTest::JournalRoundTripsATakePathThatEndsInASpace
Red/green: `cmake --build build --target zene MultiTrackRecorderTest` EXIT=0;
`cd build/tests && ctest -R '^MultiTrackRecorderTest$' --output-on-failure` EXIT=8
(Actual `".../spaced take"`, Expected `".../spaced take "`) -> after the fix EXIT=0 (7
passed), plus `ctest -R '^(ControlNoteScaleVerbsTest|NoteEditRangeTest|ControlGrooveCommandsTest|GrooveTemplateTest|NoteTransformActionsTest|RecordingRecoveryMenuTest|RecordClipTest|NoteEditingCommandsTest)$'` EXIT=0.

## Gates
`bash tests/complexity-gate.sh --check` EXIT=0 and `bash tests/file-length-gate.sh --check`
EXIT=0, both re-run immediately before each of the three commits.

Leftovers: cluster 5246's period-boundary tap hand-off is left undone as SKIPPED-SCOPE
(rule 6, render path); no other finding is half-done and no test, gate or tolerance was
weakened.