# Engine batch-10

Worktree `zene-bunny-engine`, branch `bugs/bunny-engine-2026-10-05`, starting HEAD `2e3ee5581`.
Six findings: 3 fixed in code (covering 4 reports), 2 left alone as SKIPPED-SCOPE.

### Cluster 2344 — Note probability and velocity jitter preserve NaN

Verdict: FIXED

Evidence: `src/core/Note.cpp:228` and `src/core/Note.cpp:234` (`Note::setProbability`,
`Note::setVelocityJitter`) clamped with `std::clamp` only, and the loader at
`src/core/Note.cpp:494` / `src/core/Note.cpp:514` fed `toFloat()` results in unchecked, so
`prob="nan"` survived the clamp and reached playback maths. A file-local
`loadMidiDepthValue()` helper (`src/core/Note.cpp:45`) now checks conversion success and
`std::isfinite`, falling back to the documented defaults of probability 1 and jitter 0; the
setters reject non-finite input the same way.

Commit: c63297dd7

Test: `MidiProbabilityPersistenceTest::nonFiniteAndMalformedValuesFallBackToTheDocumentedDefaults`

Red/green:
- red: `cmake --build build --target zene MidiProbabilityPersistenceTest > build/bunny-b.log 2>&1; echo EXIT=$?` → EXIT=0, then `ctest -R '^MidiProbabilityPersistenceTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` → EXIT=8 (`std::isfinite( fromNan.probability() )` returned FALSE)
- green: same build EXIT=0, same ctest EXIT=0; siblings `MpeNoteStorageTest`, `NoteShapeValidationTest`, `StableTrackIdsTest` each EXIT=0

### Cluster 3324 — Duplicate note IDs remain ambiguous after project load

Verdict: FIXED

Evidence: `Note::setId()` accepted any non-negative id and only advanced the counter, so two
notes saved with `id="5"` both kept id 5 after load. A file-local `repairDuplicateNoteIds()`
helper (`src/core/Song.cpp:1506`) walks tracks → clips → notes during `Song::loadProject`,
called at `src/core/Song.cpp:1783` just before `endLoad()`, and re-assigns a fresh id via
`ProjectIds::allocate()` / `ProjectIds::noteLoadAssignment()` to every later duplicate.

Commit: d4789a5fb

Test: `StableTrackIdsTest::duplicateNoteIdsAreRepairedOnLoad`

Red/green:
- red: `cmake --build build --target zene StableTrackIdsTest > build/bunny-b.log 2>&1; echo EXIT=$?` → EXIT=0, then `ctest -R '^StableTrackIdsTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` → EXIT=8 (`'ids[1] != 5' returned FALSE`)
- green: same build EXIT=0, same ctest EXIT=0; the other five cases of the same executable EXIT=0

### Cluster 3325 — Invalid unquantised timing can restore a note to tick zero

Verdict: FIXED

Evidence: the loader parsed `qpos` / `qlen` with `toInt()` and never checked success, so
`qpos="bad"` became 0 and a missing `qlen` became 1, and `hasUnquantisedTiming()` then
reported true. `loadUnquantisedTiming()` (`src/core/Note.cpp:59`) requires BOTH values to
parse and to be in range, otherwise the note keeps the `-1/-1` sentinel and timing is not
restored. Called from the loader alongside the 2344 fix.

Commit: f187cf511

Test: `MidiProbabilityPersistenceTest::unparseableUnquantisedTimingIsNotTreatedAsTickZero`

Red/green:
- red: `Note.cpp` temporarily reverted to its pre-fix state (`git checkout -- src/core/Note.cpp`,
   backup in `/tmp`), `cmake --build build --target zene MidiProbabilityPersistenceTest > build/bunny-b.log 2>&1; echo EXIT=$?` → EXIT=0, then `ctest -R '^MidiProbabilityPersistenceTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` → EXIT=8
- green: fix restored, build EXIT=0, ctest EXIT=0; all five sibling note cases EXIT=0

The test lives in `MidiProbabilityPersistenceTest.cpp` rather than `NoteEditingCommandsTest.cpp`
because the latter is already at the 500-code-line file-length ratchet and the new case did not
fit without tripping the gate.

### Cluster 3326 — Malformed or non-finite note probability can change playback

Verdict: DUPLICATE (of 2344)

Evidence: same code path and same defect as 2344 — `toFloat()` without a success check plus a
NaN-preserving `std::clamp` at `src/core/Note.cpp:228` and the loader near
`src/core/Note.cpp:494`. The report's extra detail (invalid text converting to 0 and making the
note silent) is the same root cause: the documented default of probability 1 applies.

Commit: c63297dd7 (same commit as 2344)

Test: `MidiProbabilityPersistenceTest::nonFiniteAndMalformedValuesFallBackToTheDocumentedDefaults`

Red/green: same commands and EXIT codes as cluster 2344 above.

### Cluster 5197 — Lyric interning retains every distinct lyric for the process lifetime

Verdict: SKIPPED-SCOPE

Evidence: `lyricTable()` at `src/core/NoteConditions.cpp:74` is a function-local
`static std::vector<QString>` and `internLyric()` at `src/core/NoteConditions.cpp:82` only ever
`push_back`s after a linear `std::find`, so the table grows for the life of the process and
loading N distinct lyrics is O(N²). The leak is real, but a correct fix means either a
project-scoped pool with reference-counted reclamation or an indexed (hashed) lookup — a design
change to a shared table with cross-module consumers, well beyond the minimal behaviour-preserving
fix this lane allows. Left alone deliberately.

Commit: none

Test: none

Red/green: none

### Cluster 5200 — MPE pressure and timbre leak into later notes

Verdict: SKIPPED-SCOPE

Evidence: `sendMpeExpressionMidi()` at `src/core/NotePlayHandle.cpp:183` intentionally returns
without sending anything when the note captured no expression, and `noteOff()` at
`src/core/NotePlayHandle.cpp:412` emits only `MidiNoteOff`, so the channel's pressure and CC74
retain their previous values for the next note on that channel. The leak is real, but resetting
the channel at note-off adds MIDI output on the render/play path — an audible output change that
the report itself does not account for, and one the test fixtures hide (the test consumer resets
all axes on note-on). Deciding the correct MPE channel lifecycle is a design decision, not a
minimal fix.

Commit: none

Test: none

Red/green: none

Leftovers: cluster 5197 (lyric pool unbounded growth + quadratic interning) and cluster 5200 (MPE
pressure/CC74 channel state not reset at note-off) are both REAL but need design decisions — a
project-scoped, reclaimable, indexed lyric pool and a defined MPE channel reset lifecycle — and
are handed back unfixed. No test was written for either.