IMPLEMENTED

# Q050-039 - MPE release-velocity capture fix (rework r2: exact at the default base velocity)

Goal: preserve the incoming MPE note-off velocity on the released note and send it through playback.
`R-instruments-4`. Luna r1 verdict REWORK: the r1 tests pinned the port's base velocity to 100, where the
note-volume scale and the MIDI velocity scale agree; at the default base velocity 63 an incoming 96 was
stored (truncated) and played back as 95. Rework: the capture and the playback emission are now an exact
inverse pair with round-half-up conversions (`Note::releaseVelocityFromMidi` / `Note::midiReleaseVelocity`,
`src/core/Note.cpp:206,217`), and the tests prove exactness at the default base velocity as well as at 100,
including an exact playback velocity check.

## Acceptance

| # | Criterion | Test | Exact command | EXIT |
| --- | --- | --- | --- | --- |
| 1 | An MPE note-on + non-default note-off velocity is read back from the saved note - at the default base velocity too | `MpeInputPathTest::noteOffVelocityIsCapturedOnTheReleasedNote` (base 100), `::noteOffVelocityIsExactAtTheDefaultBaseVelocity` (untouched default 63: stored `relvel` 152, reads back 96 on the handle and on the restored note), `PianoRollRecordTest::theRecordedNoteKeepsTheReleaseVelocityOfItsNoteOff` | `ctest -R "^(MpeInputPathTest\|MpePlaybackTest\|PianoRollRecordTest)$" --output-on-failure` (from `build/tests`) | 0 (3/3) |
| 2 | Playback emits the matching note-off velocity to a MIDI test client | `MpePlaybackTest::noteOffVelocityReachesTheInstrumentOnPlayback` (base 100, ratio check + exact `named=96`) and `::noteOffVelocityIsExactAtTheDefaultBaseVelocity` (default base 63, stored 152): the fixture's release block is `1 + velocity/127x` the sustained one, the measured ratio `1.755906` rounds back to exactly 96 | same combined command | 0 (3/3) |
| 3 | Zero-velocity and non-MPE paths retain their existing behavior | `MpeInputPathTest::noteOffVelocityIsCapturedOnTheReleasedNote` (velocity-0 note-off, note-on velocity-0 note-off, MPE disabled) | same combined command | 0 (3/3) |

Commands actually executed and their exits:

```
cmake --build build --target zene MpeInputPathTest MpePlaybackTest PianoRollRecordTest -- -j4   -> EXIT=0  (logs/build-2.log)
ctest -R "^(MpeInputPathTest|MpePlaybackTest|PianoRollRecordTest)$" --output-on-failure          -> EXIT=0  (logs/test-1.log, test-3.log)
```

Evidence lines from the binaries (`logs/evidence-playback.log`):

```
MPE_EVIDENCE release velocity 96: sustained=0.250000 released=0.438976 ratio=1.755906        (base 100)
MPE_EVIDENCE default base velocity: stored=152 sustained=0.250000 released=0.438976 ratio=1.755906 named velocity=96
```

Honest RED (the r1 truncating conversions restored in `src/core/Note.cpp`, everything else as committed):
build `EXIT=0` (`logs/build-3-red.log`), `ctest -R "^(MpeInputPathTest|MpePlaybackTest)$"` `EXIT=8`
(`logs/test-2-red.log`) - `noteOffVelocityIsExactAtTheDefaultBaseVelocity` "Actual: 95 / Expected: 96" on
both the input and the playback side, `releaseVelocityRoundTripsAtEveryRepresentableVelocity`
"Actual: 0 / Expected: 1". Restored: build `EXIT=0` (`logs/build-4.log`), all three targets `EXIT=0`.
Ripple check over the neighbours that touch release velocity (`NoteEditingCommandsTest`,
`DawProjectInterchangeRoundTripTest`, `NoteEditorTest`): `EXIT=0` (`logs/test-4-neighbours.log`).

## What was built (on top of the r1 commit)

- `Note::releaseVelocityFromMidi( velocity, base )` and a rounding `Note::midiReleaseVelocity( base )` - the
  exact inverse pair for the note-off velocity on the note volume scale (`src/core/Note.cpp:206,217`,
  declared `include/Note.h:198`); round-half-up on both sides is what makes 96 -> volume 152 -> 96 at the
  default base 63, where truncation gave 95. Velocities that already convert exactly (e.g. `relvel` 150 at
  base 64) emit the same value as before.
- The capture (`InstrumentTrack::captureMpeReleaseVelocity`) stores through that pair's forward conversion
  (`src/tracks/InstrumentTrack.cpp:409`).
- New tests: `MpeInputPathTest::noteOffVelocityIsExactAtTheDefaultBaseVelocity` (capture + save/load at the
  untouched default base, stored 152 pinned),
  `MpeInputPathTest::releaseVelocityRoundTripsAtEveryRepresentableVelocity` (velocities 1..126 at base 63,
  1..127 at base 100), `MpePlaybackTest::noteOffVelocityIsExactAtTheDefaultBaseVelocity` (exact named
  velocity at the default base), plus an exact `named==96` check in the base-100 playback slot, which now
  also restores the port's default base velocity.
- The r1 pieces (capture on the MPE note-off, `PianoRoll::finishRecordNote` copy, `MpeTestConsumer`
  consuming `MidiNoteOff`) are unchanged in behavior.
- No new commands; no format change (`relvel` already existed). No new files; the five changed inherited
  files are declared in `tests/upstream-modifications.txt`.

## Gates

`bash tests/complexity-gate.sh --check` `EXIT=1`, `bash tests/file-length-gate.sh --check` `EXIT=1` - both red
on train 5 on purpose; the file lists are identical to `../zene-cachyos` (diff empty; complexity only moves
its scanned-function total 17748 -> 17749 for the new `releaseVelocityFromMidi`, which is not flagged), so
no file of mine is newly listed. `bash tests/fork-sources-gate.sh` 0, `bash tests/all-sources-reproduce.sh` 0,
`bash tests/no-upstream-regression-gate.sh` 0, `python3 tests/scripted/check-namespace` 0,
`bash tests/no-tautology-gate.sh` 0.

## Known limits

The note volume scale (0..200) names velocities up to twice the base velocity, so at the default base 63
MIDI's 127 saturates to 126 (exact for 0..126); bases above 100 have coarser resolution than one velocity
step and are not exact - the port's default is 63. A note-on's velocity keeps `MidiEvent::volume()`'s
truncating conversion (the non-MPE path is untouched). Member channels only, matching the existing MPE
channel rule.
