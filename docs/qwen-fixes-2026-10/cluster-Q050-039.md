IMPLEMENTED

# Q050-039 - MPE release-velocity capture fix

Goal: preserve the incoming MPE note-off velocity on the released note and send it through playback.
`R-instruments-4`.

## Acceptance

| # | Criterion | Test | Exact command | EXIT |
| --- | --- | --- | --- | --- |
| 1 | An MPE note-on + non-default note-off velocity is read back from the saved note | `MpeInputPathTest::noteOffVelocityIsCapturedOnTheReleasedNote`; `PianoRollRecordTest::theRecordedNoteKeepsTheReleaseVelocityOfItsNoteOff` | `ctest -R "MpeInputPathTest\|PianoRollRecordTest" --output-on-failure` (from `build/tests`) | 0 (both pass); RED with the production changes reverted: 8 |
| 2 | Playback emits the matching note-off velocity to a MIDI test client | `MpePlaybackTest::noteOffVelocityReachesTheInstrumentOnPlayback` | `ctest -R MpePlaybackTest --output-on-failure` (from `build/tests`) | 0 |
| 3 | Zero-velocity and non-MPE paths retain their existing behavior | `MpeInputPathTest::noteOffVelocityIsCapturedOnTheReleasedNote` (velocity-0 note-off, note-on velocity-0 note-off, MPE disabled) | `ctest -R MpeInputPathTest --output-on-failure` (from `build/tests`) | 0 |

Combined run actually executed:

```
cmake --build build --target zene MpeInputPathTest MpePlaybackTest PianoRollRecordTest mpe_test_consumer -- -j4
ctest -R "MpeInputPathTest|MpePlaybackTest|PianoRollRecordTest|MpeNoteStorageTest|MpeExpressionTest" --output-on-failure
```

Build `EXIT=0`; ctest `EXIT=0` (5/5 targets passed: MpeExpressionTest, MpeInputPathTest, MpeNoteStorageTest,
MpePlaybackTest, PianoRollRecordTest). Evidence line from the playback proof:
`MPE_EVIDENCE release velocity 96: sustained=0.250000 released=0.438976 ratio=1.755906` (`1 + 96/127 = 1.7559`).

Honest RED (no fix): with only `src/tracks/InstrumentTrack.cpp` and `src/gui/editors/PianoRoll.cpp` reverted,
`ctest -R "MpeInputPathTest|PianoRollRecordTest"` returns `EXIT=8`, both slots failing
`Actual (…->releaseVelocity()): -1` vs `Expected: 96`; the changes were restored and the build green again.

## What was built

- `InstrumentTrack::trackMpeInputEvent` now captures an MPE note-off's velocity onto the released handle
  (`captureMpeReleaseVelocity`, `src/tracks/InstrumentTrack.cpp:357,377`; declared `include/InstrumentTrack.h:381`).
- `PianoRoll::finishRecordNote` copies that velocity onto the recorded clip note
  (`src/gui/editors/PianoRoll.cpp:4572`).
- Test instrument `tests/src/plugins/MpeTestConsumer.cpp` consumes `MidiNoteOff` (`:174`) and folds the
  velocity into its release block (`:216`).
- No new commands; no format change (`relvel` already existed). No new files; the three changed inherited
  files are declared in `tests/upstream-modifications.txt`.

## Gates

`bash tests/complexity-gate.sh --check` `EXIT=1`, `bash tests/file-length-gate.sh --check` `EXIT=1` - both red
on train 5 on purpose; the regression sets are byte-identical to `../zene-cachyos` (diff empty), so no file of
mine is newly listed. `bash tests/fork-sources-gate.sh` 0, `bash tests/all-sources-reproduce.sh` 0,
`bash tests/no-upstream-regression-gate.sh` 0, `tests/scripted/check-namespace` 0,
`bash tests/no-tautology-gate.sh` 0.

## Known limits

The captured velocity lives on the note's volume scale (0..200, 100 = default), the same conversion a
note-on uses, so at base velocity 63 a round-trip can lose one unit to integer truncation; the tests pin the
base velocity to 100 (scales align, exact). Member channels only, matching the existing MPE channel rule.
