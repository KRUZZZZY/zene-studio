# Cluster new-p16-118

## Verdict: FIXED

`writeToByteSeq()` (src/core/midi/MidiEventToByteSeq.cpp) wrote every MIDI payload without
checking the range of the fields it put into the byte sequence. Only the key of a note
message was checked (cluster new-p12-136). The remaining fields were written as they came:
a controller number was masked with `& 0x7F` (128 became 0 - a *different* controller), a
controller value, a program number and a channel pressure were written as-is (200 is a byte
with the high bit set - not a MIDI data byte at all), and the 14-bit pitch bend was split
into two bytes without being checked against 0..16383 (16384 produced the high byte 128).
The encoder now refuses an event whose data fields are outside their MIDI range and returns
size 0, exactly as it already did for an out-of-range key.

Evidence:

- [MidiEventToByteSeq.cpp:36-66](../../src/core/midi/MidiEventToByteSeq.cpp#L36): the new
  helpers `isDataByte()` (0..127), `isPitchBendValue()` (0..16383) and `isValidNoteMessage()`
  (key 0..127 and a 7-bit velocity / release time).
- [MidiEventToByteSeq.cpp:79-146](../../src/core/midi/MidiEventToByteSeq.cpp#L79): every
  encoded message type now checks its own fields before writing a byte - note on / note off /
  key pressure (key and velocity), control change (controller number and value), program
  change (program), channel pressure (pressure) and pitch bend (the 14-bit value).
- The values are read raw (`ev.param(0)`, `ev.param(1)`), not through the accessors that
  mask them (`MidiEvent::controllerNumber()` masks with `& 0x7F`, `MidiEvent::velocity()`
  masks with `& 0x7F`): a masked value is exactly what the bug was about.
- [MidiEventToByteSeqTest.cpp:14-115](../../tests/src/core/MidiEventToByteSeqTest.cpp#L14):
  the regression - an out-of-range controller number, controller value, program, channel
  pressure, velocity and pitch bend are all refused, while the maximum key pressure, the
  maximum control change and the maximum pitch bend still serialize.
- [upstream-modifications.txt](../../tests/upstream-modifications.txt): the divergence from
  upstream is declared with its reason.

Callers checked (both behave correctly on a refused event, so no caller had to change):

- [Lv2Proc.cpp:303](../../src/core/lv2/Lv2Proc.cpp#L303) - `if (bufsize)` guards the write, so
  a refused event is not put into the LV2 atom sequence.
- [CarlaMidiHandoff.h:120](../../plugins/CarlaBase/CarlaMidiHandoff.h#L120) - `if (written)`
  guards the staging, so a refused event is not handed to the native process callback.

## Command evidence

All builds and tests wrote output to new files under
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-118.r1/logs`.

| Evidence | Exact command | Exit |
|---|---|---:|
| Regression target build (test written first, product code untouched) | `cmake --build build --target MidiEventToByteSeqTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-118.r1/logs/build-1.log" 2>&1` | 0 |
| RED on the original code | From `build/tests`: `ctest -R '^MidiEventToByteSeqTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-118.r1/logs/test-1-red.log" 2>&1` | 8 |
| Rebuild with the fix | `cmake --build build --target MidiEventToByteSeqTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-118.r1/logs/build-2.log" 2>&1` | 0 |
| GREEN with the fix | From `build/tests`: `ctest -R '^MidiEventToByteSeqTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-118.r1/logs/test-2-green.log" 2>&1` | 0 |
| Product build | `cmake --build build --target zene > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-118.r1/logs/build-3-zene.log" 2>&1` | 0 |
| Save and reverse the product patch | `git diff -- src/core/midi/MidiEventToByteSeq.cpp > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-118.r1/logs/fix.patch && git apply -R /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-118.r1/logs/fix.patch` | 0 |
| Rebuild with the fix removed | `cmake --build build --target CarlaMidiHandoffTest MidiEventToByteSeqTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-118.r1/logs/build-6-revert.log" 2>&1` | 0 |
| RED with the fix removed (the regression catches the bug) | From `build/tests`: `ctest -R '^(MidiEventToByteSeqTest\|CarlaMidiHandoffTest)$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-118.r1/logs/test-4-red-revert.log" 2>&1` | 8 |
| Restore the product patch | `git apply /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-118.r1/logs/fix.patch` | 0 |
| Rebuild after restoring the patch | `cmake --build build --target MidiEventToByteSeqTest CarlaMidiHandoffTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-118.r1/logs/build-9-green.log" 2>&1` | 0 |
| GREEN after restoring the patch, 1/1 test | From `build/tests`: `ctest -R '^MidiEventToByteSeqTest$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-118.r1/logs/test-7-green-final.log" 2>&1` | 0 |
| MIDI family build (20 test targets + the plugins the tests load) | `cmake --build build --target MidiEventToByteSeqTest CarlaMidiHandoffTest MidiHandoffQueueTest MidiOutQueueTest MidiInputTransformTest MidiExportTest MidiRetroCaptureTest MidiClipControllersLoadTest MpeExpressionTest MpePlaybackTest NotePlaybackTest Lv2StateTest InstrumentRoutingTest ZynNoteOffTest RetroMidiCaptureCommandsTest RetroMidiRingTest VstMidiEventBatchTest RetroAudioCaptureTest NoteCurvesAcrossTest MidiJackOutScheduleTest > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-118.r1/logs/build-11-midi-family2.log" 2>&1` and `cmake --build build --target monstro nes tripleoscillator > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-118.r1/logs/build-12-plugins.log" 2>&1` | 0 / 0 |
| MIDI family run, 20/20 tests passed | From `build/tests`: `ctest -R '^(MidiEventToByteSeqTest\|CarlaMidiHandoffTest\|MidiHandoffQueueTest\|MidiOutQueueTest\|MidiInputTransformTest\|MidiExportTest\|MidiRetroCaptureTest\|MidiClipControllersLoadTest\|MpeExpressionTest\|MpePlaybackTest\|NotePlaybackTest\|Lv2StateTest\|InstrumentRoutingTest\|ZynNoteOffTest\|RetroMidiCaptureCommandsTest\|RetroMidiRingTest\|VstMidiEventBatchTest\|RetroAudioCaptureTest\|NoteCurvesAcrossTest\|MidiJackOutScheduleTest)$' --output-on-failure > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-118.r1/logs/test-10-family-final.log" 2>&1` | 0 |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p16-118 > "/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-118.r1/logs/gate-1-feature-check.log" 2>&1` | 0 |

Gate table (`logs/gate-1-feature-check.log`, exit 0, `RESULT: PASS`):

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
```

The RED run (`logs/test-1-red.log`) failed exactly the six new assertions on the original
code - the controller number, the controller value, the program, the channel pressure, the
velocity and the pitch bend were all converted where the contract says they must not be:

```
FAIL!  : MidiEventToByteSeqTest::outOfRangeControlNumberIsRejected()  Actual 3, Expected 0
FAIL!  : MidiEventToByteSeqTest::outOfRangeControlValueIsRejected()    Actual 3, Expected 0
FAIL!  : MidiEventToByteSeqTest::outOfRangeProgramIsRejected()         Actual 2, Expected 0
FAIL!  : MidiEventToByteSeqTest::outOfRangeChannelPressureIsRejected() Actual 2, Expected 0
FAIL!  : MidiEventToByteSeqTest::outOfRangeVelocityIsRejected()        Actual 3, Expected 0
FAIL!  : MidiEventToByteSeqTest::outOfRangePitchBendIsRejected()       Actual 3, Expected 0
Totals: 6 passed, 6 failed, 0 skipped, 0 blacklisted, 1ms
```

### One pre-existing flaky test, not this fix

`CarlaMidiHandoffTest::concurrentMidiStressIsOrderedAndAllocationFree()` fails
intermittently with "processing did not overlap MIDI producer activity" - it needs the
producer thread to be running while the consumer renders. It is flaky on the *unmodified*
code as well: with the product patch reversed, 3 of 6 runs failed
(`logs/test-6-carla-pristine1..6.log`), with the patch applied 1 of 5 runs failed
(`logs/test-5-carla-run1..5.log`). The final family run
(`logs/test-10-family-final.log`) passed 20/20. The test is not touched by this fix and its
assertion says nothing about the encoded bytes.

`NotePlaybackTest` failed twice in an earlier family run
(`logs/test-8-midi-family.log`) with `The plugin "monstro" wasn't found or could not be
loaded!` - the plugin binaries had not been built in this build tree (the guard only allows
named targets). After building `monstro`, `nes` and `tripleoscillator`
(`logs/build-12-plugins.log`) the test passes (`logs/test-9-family-rerun.log`, exit 0).

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.**
   RED with the patch reversed: `ctest -R '^(MidiEventToByteSeqTest|CarlaMidiHandoffTest)$'`
   in `build/tests` -> exit 8, six `outOfRange*` assertions failed
   (`logs/test-4-red-revert.log`; the same six failures on the untouched code are in
   `logs/test-1-red.log`). GREEN with the patch restored: `ctest -R '^MidiEventToByteSeqTest$'`
   -> exit 0, 1/1 test passed (`logs/test-7-green-final.log`).
2. **The test is deterministic.** It only calls `lmms::writeToByteSeq()` with hand-built
   `MidiEvent`s and compares the returned size and the three bytes: no clock, no sleep, no
   thread, no race, no injected timestamp needed - the function under test is a pure
   encoder.
3. **The fix is at the cause and complete.** The cause is the encoder writing a field
   without checking it, so the check belongs in `writeToByteSeq()` and not in the
   accessors: `MidiEvent::controllerNumber()` / `velocity()` mask their value by design and
   are used elsewhere (`MpeExpression::handleExpressionEvent`,
   `InstrumentTrack::processInEvent`) where the masked value is what the caller wants. Every
   message type the encoder writes is now checked (note on, note off, key pressure, control
   change, program change, channel pressure, pitch bend) and the two callers of the encoder
   (`Lv2Proc::copyModelsFromCore`, `CarlaMidiHandoff::stageMidiEvent`) already drop a
   refused event, so both stay consistent. The sibling of the key check - the same 7-bit
   range for the other data fields - is what the fix adds, so note on, note off and key
   pressure now share one helper.
4. **No collateral damage.** Every value the product itself produces is inside the checked
   ranges, so nothing legitimate is refused: the core sends velocities from
   `Note::midiVelocity()` / `Note::midiReleaseVelocity()` (0..127, `src/core/Note.cpp:267`),
   the keyswitch velocity is 100 (`include/InstrumentTrack.h:236`), the controller lanes
   carry 0..127 with a 0..16383 pitch bend (`include/MidiClipControllers.h:49-55`), the
   MPE axes are clamped to 7 bits and the bend to 14 bits
   (`src/core/midi/MpeExpression.cpp:65,219-227`), the pitch knob maps to 0..16383
   (`include/InstrumentTrack.h:182-185`) and the MIDI input parser reads 7-bit bytes
   (`src/core/midi/MidiClient.cpp:248-256`). No project file format, save path or load path
   was touched, so old projects load and render exactly as before; the change only affects
   events that were never valid MIDI.
5. **Realtime, undo and commands.** `writeToByteSeq()` is on the audio path
   (`tests/rt-safety-scope.txt:114` names it) and the fix adds only two integer comparisons
   per message and three file-static helper functions - no allocation, no free, no lock, no
   wait, no logging, no signal. No command was added or changed (the
   `ControlCommandsSnapshot` canary is untouched), and no undo step was introduced: the
   encoder is not a command and writes no model state.

## Files in the cluster commit

- `src/core/midi/MidiEventToByteSeq.cpp`
- `tests/src/core/MidiEventToByteSeqTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p16-118.md`
