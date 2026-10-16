IMPLEMENTED

# Cluster Q050-040: Unmute note-chase fix

With chase on, an unmute during playback now starts a note already under the playhead, for a track
and for a clip, exactly once. Before the fix, chase handled only a start, a jump and a loop, so an
unmute under the playhead stayed silent. Review round 2 then found that clip-wide deduplication
could suppress a valid note from another overlapping clip, and that a failed tryLock could consume
the track-unmute edge; both are fixed, each with its own regression. Review round 3 found the clip
mute still a GUI-only direct toggle with no registered agent command; round 4 adds `clip.set_mute`,
dispatches the song editor's own clip mute through it, and proves the clip-unmute chase through
ControlRegistry. The fix is in [docs/050-features/Q050-040.md](../050-features/Q050-040.md) with
file:line detail.

## Acceptance

| # | Criterion | Exact command | Result / EXIT |
|---|---|---|---|
| 1 | A regression test unmutes a track and a clip during playback and verifies the sounding note starts once | `ctest -R UnmuteChaseTest --output-on-failure` (slots `unmuteDuringPlaybackChasesTheNoteOnce`, `clipUnmuteThroughControlRegistryChasesTheNoteOnce`) | PASS; 1/1 tests, 5 slots, EXIT 0 - the clip unmute runs through the registered `clip.set_mute` command an agent uses |
| 2 | No duplicate note-on or stuck note-off after a seek, loop, or re-mute | `ctest -R UnmuteChaseTest --output-on-failure` (slots `unmuteDuringPlaybackChasesTheNoteOnce`, `unmuteChaseSurvivesRemuteAndLoop`) | PASS, EXIT 0 |
| 3 | Existing chase-on-start, jump and loop tests remain green | `ctest -R "^NotePlaybackTest$" --output-on-failure` (`chaseSoundsANotePlaybackStartsInside`, `aLoopedClipPlaysItsStartAgain`) | PASS; 23/23, EXIT 0 |

## Round-2 regressions (the round-2 review's required cases)

| # | Criterion | Exact command | Result / EXIT |
|---|---|---|---|
| 4 | Two overlapping clips with the same key and clip-relative note position each chase their own note | `ctest -R UnmuteChaseTest --output-on-failure` (slot `overlappingClipsEachChaseTheirOwnNote`) | PASS with the clip-identity check; `{60, 60}` after the second clip's unmute. RED (list-size mismatch) when the implementation is reversed |
| 5 | The track-unmute edge survives passes that cannot take the processing lock | `ctest -R UnmuteChaseTest --output-on-failure` (slot `unmuteEdgeSurvivesAContendedLock`) | PASS with the lock ordering fix; the note starts once on the release. RED when the implementation is reversed |

## Round-4 additions (the round-3 review's rework: GUI/agent parity for the clip mute)

| # | Criterion | Exact command | Result / EXIT |
|---|---|---|---|
| 6 | A registered, reversible clip mute/unmute command | `ctest -R ReversibilityContractTest --output-on-failure`; `ctest -R ReversibilityUndoTest --output-on-failure` | PASS, EXIT 0 each: `clip.set_mute` has a contract row (class `true_inverse`, mechanism "ProjectJournal (Clip mute BoolModel checkpoint)"), the A16 histogram matches the re-measured published figure (rows=838 true_inverse=418), and the command applied then one control.undo reads the pre-command mute state back |
| 7 | The GUI clip mute routes through that same command | `src/gui/clips/ClipView.cpp:1321-1341` (`ClipView::toggleMute` invokes `clip.set_mute`); the command resolves pattern-editor clips too (`resolveAnyClip`, `src/core/ControlCommandsClipMute.cpp:62-81`) | PASS - one implementation for the GUI gesture and the agent command |
| 8 | A playback regression that unmutes the clip through ControlRegistry and verifies the note starts once | `ctest -R UnmuteChaseTest --output-on-failure` (slot `clipUnmuteThroughControlRegistryChasesTheNoteOnce`) | PASS, EXIT 0: muted through the command, no note; unmuted through the same command, exactly `{60}`; an idempotent repeat costs no undo step |
| 9 | Command snapshot and canary updated | `ctest -R ControlCommandsSnapshot --output-on-failure`; `python3 tests/doc-figures-gate.py` | PASS, EXIT 0 each: snapshot regenerated from a live instance (829 ids / 76 groups, `clip.set_mute` present), README canary re-taken to 829 |

## Red -> green evidence (rounds 1-2, all four slots vs the reversed implementation)

| Phase | Exact command | Exit |
|---|---|---:|
| Save the fixed code files, reverse them to train 5 | `git checkout de5a85a96 -- include/InstrumentTrack.h include/NotePlayHandle.h src/tracks/InstrumentTrack.cpp src/tracks/InstrumentTrackNotes.cpp; echo EXIT=$?` | 0 |
| Build with the implementation reversed | `cmake --build build --target zene UnmuteChaseTest -- -j4 > .../logs/build-red.log 2>&1; echo EXIT=$?` | 0 |
| RED on the pre-fix tree | `ctest -R UnmuteChaseTest --output-on-failure > .../logs/test-red.log 2>&1; echo EXIT=$?` | 8 |
| Restore the fixed code | `git checkout HEAD -- include/InstrumentTrack.h include/NotePlayHandle.h src/tracks/InstrumentTrack.cpp src/tracks/InstrumentTrackNotes.cpp; echo EXIT=$?` | 0 |
| Build with the fix | `cmake --build build --target zene UnmuteChaseTest NotePlaybackTest -- -j4 > .../logs/build-green.log 2>&1; echo EXIT=$?` | 0 |
| GREEN with the fix | `ctest -R UnmuteChaseTest --output-on-failure > .../logs/test-green.log 2>&1; echo EXIT=$?` | 0 |
| The inherited chase tests stay green | `ctest -R "^NotePlaybackTest$" --output-on-failure > .../logs/test-noteplayback-3.log 2>&1; echo EXIT=$?` | 0 |

The RED failures were, verbatim:
`unmuteDuringPlaybackChasesTheNoteOnce() 'handleKeys(m_track).isEmpty()' returned FALSE. (a muted track must schedule no note under the playhead)`,
`unmuteChaseSurvivesRemuteAndLoop() 'handleKeys(m_track).size() <= 1' returned FALSE. (a loop must not double the note already sounding)`,
`overlappingClipsEachChaseTheirOwnNote() Compared lists have different sizes.`,
`unmuteEdgeSurvivesAContendedLock() 'handleKeys(m_track).isEmpty()' returned FALSE. (a muted track must schedule no note under the playhead)`.
All four slots pass with the fix. The round-1/2 logs are under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-040.r3/logs/`; this round's runs are
under `.../Q050-040.r4/logs/` (build-1..4, test-all-targeted, test-all-targeted-2, snapshot-1/2,
doc-figures-1..3, gate-*.log).

## New commands and reversibility

`clip.set_mute` (`src/core/ControlCommandsClipMute.cpp:83`): `{clip: "clip-<n>", muted: bool}`,
mutating, result `{clip, muted}`. Reversibility class `RC::TrueInverse`
(`src/core/ControlReversibilityTableLive.cpp:112`): the clip's mute model is a BoolModel, i.e. a
JournallingObject, so its `setValue` is ONE ProjectJournal checkpoint - the same mechanism
`track.set_mute` and the GUI's own toggle reverse through; one control.undo takes the mute back
(`ReversibilityUndoTest`). The A16 histogram was re-measured and re-published in the same change:
`rows=838 true_inverse=418 snapshot=42 irreversible=22 not_mutating=356`
(`docs/RELEASE-NOTES-v0.3.0-alpha.md:1004`). The song editor's clip mute
(`ClipView::toggleMute`) dispatches this command, so the GUI and the agents share one
implementation and one undo class. The command snapshot was regenerated from a live instance
(829 ids / 76 groups) and the README canary re-taken.

## Format changes

None. No project data was added or changed: the clip's `muted` attribute serialized as before;
the command adds runtime writes only.

## Known limits

- `noteIsAlreadySounding` mirrors the clip transpose but not a chord-follow re-keying.
- The dedup compares clip instance, key and position - not velocity or length; two genuinely
  identical overlapping clips sound as two notes, which is what the user asked for.
- A loop shorter than a note is not cut and retriggered (the retrigger is blocked so the note is
  not doubled).
- `clip.set_mute` addresses the song arrangement's clips and the pattern editor's
  (PatternStore) clips by their stable ids, but the OTHER clip.* commands still enumerate the song
  arrangement only - a pattern-editor clip is discoverable to an agent through this command alone.

## Required gates

| Gate | Exit |
|---|---|
| `bash tests/complexity-gate.sh --check` | 1 (train-5 red; REGRESSION set identical to `../zene-cachyos`, no file of this unit newly listed) |
| `bash tests/file-length-gate.sh --check` | 1 (train-5 red; fork scope: REGRESSION set identical to `../zene-cachyos`; `UnmuteChaseTest.cpp` 255, `ControlCommandsClipMute.cpp` 86 not listed) |
| `bash tests/fork-sources-gate.sh` | 0 |
| `bash tests/all-sources-reproduce.sh` | 0 |
| `bash tests/no-upstream-regression-gate.sh` | 0 |
| `tests/scripted/check-namespace` | 0 |
| `bash tests/no-tautology-gate.sh` | 0 |
| `python3 tests/doc-figures-gate.py` | 0 (829 ids / 76 groups) |
