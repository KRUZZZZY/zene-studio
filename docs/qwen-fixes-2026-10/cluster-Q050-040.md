IMPLEMENTED

# Cluster Q050-040: Unmute note-chase fix

With chase on, an unmute during playback now starts a note already under the playhead, for a track
and for a clip, exactly once. Before the fix, chase handled only a start, a jump and a loop, so an
unmute under the playhead stayed silent. Review round 2 then found that clip-wide deduplication
could suppress a valid note from another overlapping clip, and that a failed tryLock could consume
the track-unmute edge; both are fixed, each with its own regression. The fix is in
[docs/050-features/Q050-040.md](../050-features/Q050-040.md) with file:line detail.

## Acceptance

| # | Criterion | Exact command | Result / EXIT |
|---|---|---|---|
| 1 | A regression test unmutes a track and a clip during playback and verifies the sounding note starts once | `ctest -R UnmuteChaseTest --output-on-failure` (slot `unmuteDuringPlaybackChasesTheNoteOnce`) | PASS; 1/1 slots, EXIT 0 |
| 2 | No duplicate note-on or stuck note-off after a seek, loop, or re-mute | `ctest -R UnmuteChaseTest --output-on-failure` (slots `unmuteDuringPlaybackChasesTheNoteOnce`, `unmuteChaseSurvivesRemuteAndLoop`) | PASS, EXIT 0 |
| 3 | Existing chase-on-start, jump and loop tests remain green | `ctest -R "^NotePlaybackTest$" --output-on-failure` (`chaseSoundsANotePlaybackStartsInside`, `aLoopedClipPlaysItsStartAgain`) | PASS; 23/23, EXIT 0 |

## Round-2 regressions (the review's two required cases)

| # | Criterion | Exact command | Result / EXIT |
|---|---|---|---|
| 4 | Two overlapping clips with the same key and clip-relative note position each chase their own note | `ctest -R UnmuteChaseTest --output-on-failure` (slot `overlappingClipsEachChaseTheirOwnNote`) | PASS with the clip-identity check; `{60, 60}` after the second clip's unmute. RED (list-size mismatch) when the implementation is reversed |
| 5 | The track-unmute edge survives passes that cannot take the processing lock | `ctest -R UnmuteChaseTest --output-on-failure` (slot `unmuteEdgeSurvivesAContendedLock`) | PASS with the lock ordering fix; the note starts once on the release. RED when the implementation is reversed |

## Red -> green evidence (all four slots vs the reversed implementation)

| Phase | Exact command | Exit |
|---|---|---:|
| Save the fixed code files, reverse them to train 5 | `git checkout de5a85a96 -- include/InstrumentTrack.h include/NotePlayHandle.h src/tracks/InstrumentTrack.cpp src/tracks/InstrumentTrackNotes.cpp; echo EXIT=$?` | 0 |
| Build with the implementation reversed | `cmake --build build --target zene UnmuteChaseTest -- -j4 > .../logs/build-red.log 2>&1; echo EXIT=$?` | 0 |
| RED on the pre-fix tree | `ctest -R UnmuteChaseTest --output-on-failure > .../logs/test-red.log 2>&1; echo EXIT=$?` | 8 |
| Restore the fixed code | `git checkout HEAD -- include/InstrumentTrack.h include/NotePlayHandle.h src/tracks/InstrumentTrack.cpp src/tracks/InstrumentTrackNotes.cpp; echo EXIT=$?` | 0 |
| Build with the fix | `cmake --build build --target zene UnmuteChaseTest NotePlaybackTest -- -j4 > .../logs/build-green.log 2>&1; echo EXIT=$?` | 0 |
| GREEN with the fix | `ctest -R UnmuteChaseTest --output-on-failure > .../logs/test-green.log 2>&1; echo EXIT=$?` | 0 |
| The inherited chase tests stay green | `ctest -R "^NotePlaybackTest$" --output-on-failure > .../logs/test-noteplayback-3.log 2>&1; echo EXIT=$?` | 0 |
| Snapshot matches the rebuilt binary | `ctest -R ControlCommandsSnapshot --output-on-failure > .../logs/test-snapshot.log 2>&1; echo EXIT=$?` | 0 |

The RED failures were, verbatim:
`unmuteDuringPlaybackChasesTheNoteOnce() 'handleKeys(m_track).isEmpty()' returned FALSE. (a muted track must schedule no note under the playhead)`,
`unmuteChaseSurvivesRemuteAndLoop() 'handleKeys(m_track).size() <= 1' returned FALSE. (a loop must not double the note already sounding)`,
`overlappingClipsEachChaseTheirOwnNote() Compared lists have different sizes.`,
`unmuteEdgeSurvivesAContendedLock() 'handleKeys(m_track).isEmpty()' returned FALSE. (a muted track must schedule no note under the playhead)`.
All four slots pass with the fix. All logs are under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-040.r3/logs/`.

## New commands and reversibility

None. `transport.midi_options` (GUI Project > Chase Notes) gains the unmute in its description
only; its reversibility class stays `RC::TrueInverse`, one undo step.

## Format changes

None. No project data was added or changed.

## Known limits

- `noteIsAlreadySounding` mirrors the clip transpose but not a chord-follow re-keying.
- The dedup compares clip instance, key and position - not velocity or length; two genuinely
  identical overlapping clips sound as two notes, which is what the user asked for.
- A loop shorter than a note is not cut and retriggered (the retrigger is blocked so the note is
  not doubled).

## Required gates

| Gate | Exit |
|---|---|
| `bash tests/complexity-gate.sh --check` | 1 (train-5 red; REGRESSION set identical to `../zene-cachyos`, no file of this unit newly listed) |
| `bash tests/file-length-gate.sh --check` | 1 (train-5 red; `NotePlaybackTest.cpp` back at its inherited 624 lines; `UnmuteChaseTest.cpp` 225, not listed) |
| `bash tests/fork-sources-gate.sh` | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `tests/scripted/check-namespace` | 0 |
| `bash tests/no-tautology-gate.sh` | 0 |
| `python3 tests/doc-figures-gate.py` | 0 (828 ids / 76 groups) |
