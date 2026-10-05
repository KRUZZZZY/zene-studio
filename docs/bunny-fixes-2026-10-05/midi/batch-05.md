# Space Bunny fixer lane `midi` — batch 05

Branch `bugs/bunny-midi-2026-10-05`. One finding was real and fixed; four were
false positives against code that already refuses, counts, or documents the
behaviour; one is out of lane scope.

### Cluster 3553 — Invalid MIDI tempo values can produce an invalid or overflowing BPM

Verdict: FALSE-POSITIVE

Evidence: `src/core/SmfInterchangeReader.cpp:514-515` — `tempoOf()` already
divides by `std::max(1, candidate.microseconds)`, so a zero payload cannot
divide by zero, and the largest value it can produce is
`60000000 / 1 = 60000000`, which fits `int` without wrapping or going negative.
The claim that the value is "potentially negative after the cast" does not hold
for any reachable input: the numerator is a constant `SmfMicrosecondsPerMinute`
(`include/SmfInterchange.h:109`) and the denominator is clamped to >= 1, so the
result is always in [1, 60000000]. There is no overflow and no invalid value
produced here — only an out-of-engine-range one, which the import refuses with a
reason (see cluster 5383).

Commit: none

Test: none

Red/green: none

### Cluster 5383 — Imported MIDI tempos can exceed the engine's supported range

Verdict: ALREADY-FIXED

Evidence: `src/core/SmfInterchange.cpp:442` — every read event is checked with
`TempoMap::validEvent()` before it is added, and an out-of-range tempo is
refused with a message naming the tempo and the accepted bounds
(`src/core/SmfInterchange.cpp:446-451`). The engine bounds are `TempoMapMinTempo`
.. `TempoMapMaxTempo`, statically asserted equal to `Song.h`'s own `MinTempo` /
`MaxTempo` (`src/core/TempoMap.cpp:45-46`). This is deliberate: the reader
reports what the file says and the engine decides (`SmfInterchangeTest.cpp:311`
— "the reader reports it, the engine decides"). The existing case
`too-slow.mid` (0xFFFFFF microseconds per quarter = 4 bpm) asserts exactly the
finding's scenario: `smf_read` reports bpm 4, `smf_import` is refused, and the
map is left untouched (`tests/src/core/SmfInterchangeTest.cpp:311-331`). The
finding's `when` clause is therefore already a covered, passing behaviour.

Commit: none

Test: SmfInterchangeTest (existing case at tests/src/core/SmfInterchangeTest.cpp:311)

Red/green: none (no change made). Verified green in this batch:
`ctest -R '^(SmfInterchangeTest|SmfInterchangeRoundTripTest)$' --output-on-failure`
-> EXIT=0

### Cluster 3244 — A full ALSA output queue can lose note-offs silently

Verdict: SKIPPED-SCOPE

Evidence: `src/core/midi/MidiAlsaSeq.cpp:282` does discard `push()`'s return,
but the event is not lost silently: `MidiOutQueue::push()` counts every refused
event in an atomic (`include/MidiOutQueue.h:49`) and the count is exposed to the
control surface through `outputQueueOverflowCount()`
(`include/MidiClient.h:140`, surfaced at `src/core/ControlCommandsMidi.cpp:275`).
The proposed remedy — note-off priority or an all-notes-off recovery — would run
on the audio-thread producer side and needs either unbounded growth or locking
to be correct, which lane rule 6 forbids. The gap left is the recovery
behaviour, not the accounting, and supplying it is a design change beyond this
lane.

Commit: none

Test: none

Red/green: none

### Cluster 5129 — ALSA output events can wait 250 ms before processing

Verdict: SKIPPED-SCOPE

Evidence: the mechanism described is accurate — `run()` drains the queue and then
blocks in `poll()` with `EventPollTimeOut = 250`
(`src/core/midi/MidiAlsaSeq.cpp:43`, `:722-727`), and nothing in `processOutEvent`
(`:279-283`) wakes it, so an event arriving just after the drain waits for the
timeout. Fixing it as proposed means writing to an eventfd/pipe from
`processOutEvent`, which is the audio-thread producer. That is a realtime-path
design change on a driver this lane cannot exercise headlessly (no ALSA
sequencer on the test box), and rule 6 plus the "cannot make a deterministic
failing test headlessly" rule both block it here.

Commit: none

Test: none

Red/green: none

### Cluster 5130 — The configured MIDI clock source port is ignored

Verdict: FALSE-POSITIVE

Evidence: this is documented intended behaviour, stated in the command's own
contract at `src/core/ControlCommandsClock.cpp:280-281`: "`source_port` names the
readable client port to follow (informational: the engine's MIDI readers deliver
a clock from wherever it arrives)". `MidiAlsaSeq::run()` states the same reason
inline — the clock family is system-wide, a pulse names no port of ours, so it
is dispatched before the destination lookup, and "a slave follows a clock from
wherever it arrives" (`src/core/midi/MidiAlsaSeq.cpp:771-774`). Changing this
would alter a documented command's semantics (rule 8) and is a feature, not a
defect.

Commit: none

Test: none

Red/green: none

### Cluster 5132 — Raw MIDI time-code payload is stored in the wrong byte slot

Verdict: FIXED

Evidence: `src/core/midi/MidiClient.cpp:318` (pre-fix) stored the payload at
`m_commonBuffer[2 - remaining]`, counting down from the parse buffer's
capacity. That agrees with the reader only for the two-byte Song Position
Pointer (remaining 2 -> 0, 1 -> 1). The one-byte time-code quarter frame wrote
index 1 while completion read index 0 (`MidiClient.cpp:330`), so the engine
reported a stale byte left by an earlier song position, or zero. The slot is
now counted from this message's own declared length, so both forms land where
completion reads them. `MidiClient.cpp:317-330` post-fix.

Commit: 5f3c295b8

Test: MidiClockTest::systemCommonPayloadsLandInTheSlotsCompletionReads

Red/green:
RED: `cmake --build build --target zene MidiClockTest` -> EXIT=0;
`cd build/tests && ctest -R '^MidiClockTest$' --output-on-failure` -> EXIT=8
("Actual (tracker.lastTimeCode()): 97, Expected (0x25): 37" — the stale LSB of
the song position fed immediately before).
GREEN: same build -> EXIT=0; `ctest -R '^MidiClockTest$' --output-on-failure`
-> EXIT=0 (13 passed, 0 failed).
Gates: `bash tests/complexity-gate.sh --check` -> EXIT=0;
`bash tests/file-length-gate.sh --check` -> EXIT=0.
Wider: the 10 suites touching the changed files
(`ctest -R '^(SmfInterchangeTest|SmfInterchangeRoundTripTest|MidiLearnTest|ControllerSurfaceTest|MidiReconnectTest|RecordingPassTest|RetroMidiCaptureCommandsTest|UndoBoundsTest|MidiLearnThreadTest|MidiClockTest)$'`)
-> EXIT=0, 10/10 passed.

Leftovers: none. Clusters 3244 and 5129 are real gaps in the ALSA output path
(overflow recovery and a worker wakeup) but both remedies land on the
audio-thread producer and neither is headlessly testable on this box; they are
worth re-filing as ALSA-driver design work rather than as regression fixes.
