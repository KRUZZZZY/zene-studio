IMPLEMENTED

# Cluster Q050-040: Unmute note-chase fix

With chase on, an unmute during playback now starts a note already under the playhead, for a track
and for a clip, exactly once. Before the fix, chase handled only a start, a jump and a loop, so an
unmute under the playhead stayed silent. The fix is in
[docs/050-features/Q050-040.md](../050-features/Q050-040.md) with file:line detail.

## Acceptance

| # | Criterion | Exact command | Result / EXIT |
|---|---|---|---|
| 1 | A regression test unmutes a track and a clip during playback and verifies the sounding note starts once | `ctest -R NotePlaybackTest --output-on-failure` (slot `unmuteDuringPlaybackChasesTheNoteOnce`) | PASS; whole class 25/25, EXIT 0 |
| 2 | No duplicate note-on or stuck note-off after a seek, loop, or re-mute | `ctest -R NotePlaybackTest --output-on-failure` (slots `unmuteDuringPlaybackChasesTheNoteOnce`, `unmuteChaseSurvivesRemuteAndLoop`) | PASS, EXIT 0 |
| 3 | Existing chase-on-start, jump and loop tests remain green | `ctest -R NotePlaybackTest --output-on-failure` (`chaseSoundsANotePlaybackStartsInside`, `aLoopedClipPlaysItsStartAgain`) | PASS, EXIT 0 |

## Red -> green evidence (the same two slots)

| Phase | Exact command | Exit |
|---|---|---:|
| Build with the product patch reverted (only the test kept) | `cmake --build build --target NotePlaybackTest -- -j4 > .../logs/build-15-prefix.log 2>&1; echo EXIT=$?` | 0 |
| RED on the pre-fix tree | `ctest -R NotePlaybackTest --output-on-failure > .../logs/test-prefix-red.log 2>&1; echo EXIT=$?` | 8 |
| Reapply the saved patch | `git apply --include=include/Clip.h --include=include/InstrumentTrack.h --include=src/tracks/InstrumentTrack.cpp --include=src/tracks/InstrumentTrackNotes.cpp .../fix2.patch; echo EXIT=$?` | 0 |
| Build with the fix | `cmake --build build --target NotePlaybackTest -- -j4 > .../logs/build-16-postfix.log 2>&1; echo EXIT=$?` | 0 |
| GREEN with the fix | `ctest -R NotePlaybackTest --output-on-failure > .../logs/test-7-green.log 2>&1; echo EXIT=$?` | 0 |
| Snapshot matches the rebuilt binary | `ctest -R ControlCommandsSnapshot --output-on-failure > .../logs/test-snapshot.log 2>&1; echo EXIT=$?` | 0 |

The RED assertions were, verbatim:
`unmuteDuringPlaybackChasesTheNoteOnce() 'handleKeys(m_track).isEmpty()' returned FALSE. (a muted track must schedule no note under the playhead)`
and `unmuteChaseSurvivesRemuteAndLoop() 'handleKeys(m_track).size() <= 1' returned FALSE. (a loop must not double the note already sounding)`.
Both slots pass with the fix. All logs are under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-040.r1/logs/`.

## New commands and reversibility

None. `transport.midi_options` (GUI Project > Chase Notes) gains the unmute in its description
only; its reversibility class stays `RC::TrueInverse`, one undo step.

## Format changes

None. No project data was added or changed.

## Known limits

- `noteIsAlreadySounding` mirrors the clip transpose but not a chord-follow re-keying.
- A loop shorter than a note is not cut and retriggered (the retrigger is blocked so the note is
  not doubled).

## Required gates

| Gate | Exit |
|---|---|
| `bash tests/complexity-gate.sh --check` | 1 (train-5 red; REGRESSION set identical to `../zene-cachyos`, no file of this unit newly listed) |
| `bash tests/file-length-gate.sh --check` | 1 (train-5 red; `NotePlaybackTest.cpp` already listed) |
| `bash tests/fork-sources-gate.sh` | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `tests/scripted/check-namespace` | 0 |
| `bash tests/no-tautology-gate.sh` | 0 |
| `python3 tests/doc-figures-gate.py` | 0 (828 ids / 76 groups) |
