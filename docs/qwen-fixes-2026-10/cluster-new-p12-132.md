# cluster new-p12-132 — Raw MIDI parser state is uninitialized

**Verdict: FIXED**

## Finding

`include/MidiClient.h` — `midiParserData` initialised only the three system-common
fields added by 0.3.0; `m_status`, `m_channel`, `m_bytes`, `m_bytesTotal` and
`m_buffer` kept whatever the client's memory held. `MidiClientRaw()` is `= default`
(include/MidiClient.h:201) and `MidiClientRaw::parseData()` reads `m_status` to decide
whether a DATA byte belongs to an event in progress
(src/core/midi/MidiClient.cpp:194), so a raw device opened mid-message — its first
received byte data — had that byte assembled into an event out of the leftover
status/channel/counter/payload instead of being discarded (and the leftover counter
became the running-status byte count for the next message).

## Fix

include/MidiClient.h:233-258 — every remaining `midiParserData` field gets a default
member initialiser (`m_status = 0`, `m_channel = 0`, `m_bytes = 0`,
`m_bytesTotal = 0`, `m_buffer[...] = {}`). Zero is the parser's own "no event in
progress" state: `parseData()` tests `m_status == 0` to drop orphan data bytes and
every status byte it stores is non-zero. No behaviour change on any wired path, where
a status byte writes all five fields before any of them is read; the change is at
construction, off the MIDI input thread, so nothing on the parse path was touched.
Registry: the reason was appended to the existing `include/MidiClient.h` row in
tests/upstream-modifications.txt:583.

## Regression

`tests/src/core/MidiClockTest.cpp::aFreshParserDiscardsALeadingDataByte` — the file's
`ParsingRawClient` harness is reused (it already drives `MidiClientRaw::parseData()`),
wrapped in a `PoisonedParsingRawClient` that memsets the client's whole storage to
0x90 (a note-on status, the value that turns a leftover counter into a *completed*
event rather than a silent drop) and then DEFAULT-initialises a `ParsingRawClient` over
it. Both bytes go through the real parser, and the parsed events are observed through
the client's own armed retrospective-capture ring (`processParsedEvent()`,
src/core/midi/MidiClient.cpp:279-282). The harness uses `new (p) ParsingRawClient`
without parentheses on purpose: `new (p) ParsingRawClient()` is value-initialisation,
and the compiler's zero-fill would hide exactly the fields under test (that was
observed — the first draft of the harness passed against the unfixed code).

## Evidence

| step | command | exit |
|---|---|---|
| RED (before fix) | `cmake --build build --target MidiClockTest` (logs/build-2.log) | 0 |
| RED run | `ctest -R '^MidiClockTest$' --output-on-failure` from build/tests (logs/test-2.log) — `FAIL! aFreshParserDiscardsALeadingDataByte() Actual bufferedCount: 1, Expected: 0`, 14 passed / 1 failed | 8 |
| GREEN (after fix) | `cmake --build build --target zene MidiClockTest` (logs/build-3.log) | 0 |
| GREEN run | `ctest -R '^MidiClockTest$' --output-on-failure` from build/tests (logs/test-3.log) | 0 |
| affected family | `ctest -R '^(MidiClockTest\|MidiLearnTest\|MidiLearnThreadTest\|ControllerSurfaceTest\|MidiRetroCaptureTest\|CaptureMidiMenuTest\|RecordingPassTest\|MidiReconnectTest)$'` from build/tests (logs/test-4.log) — 8/8 passed | 0 |
| proof the test catches the bug | `git diff -- include/MidiClient.h > logs/fix.patch && git apply -R logs/fix.patch` (apply 0), `cmake --build build --target zene MidiClockTest` (0), `ctest -R '^MidiClockTest$'` (logs/test-5.log) — same FAIL, 14 passed / 1 failed | 8 |
| fix restored | `git apply logs/fix.patch` (0), `cmake --build build --target zene MidiClockTest` (0), `ctest -R '^MidiClockTest$'` (logs/test-6.log) — passed | 0 |
| gate | `bash tests/complexity-gate.sh --check` (logs/gate-complexity.log) | 0 |
| gate | `bash tests/file-length-gate.sh --check` (logs/gate-file-length.log) | 0 |
| registry | `bash tests/fork-sources-gate.sh` (logs/gate-fork-sources.log) — 1510 fork-NEW, 1196 inherited, 0 stale | 0 |

Tests executed in the affected family: 8 ctest targets (MidiClockTest alone runs 15
QTest functions: 14 pass / 1 fail before the fix, 15 pass after).

## Files changed

- include/MidiClient.h
- tests/src/core/MidiClockTest.cpp
- tests/upstream-modifications.txt
- docs/qwen-fixes-2026-10/cluster-new-p12-132.md