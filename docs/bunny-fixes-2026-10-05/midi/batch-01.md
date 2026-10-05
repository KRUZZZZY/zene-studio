# Bunny fixes 2026-10-05 — lane `midi`, batch 01

Worktree `zene-bunny-midi`, branch `bugs/bunny-midi-2026-10-05`. Six findings, three fixed with
a red/green regression each, three real but with no deterministic headless test.

### Cluster 1486 — An inverted key range is accepted and drops every note

Verdict: FIXED

Evidence: `src/core/MidiInputTransform.cpp:142-143` declared `key_low`/`key_high` as two
independent settings and `fromJson()` range-checked each value on its own, so `{key_low: 100,
key_high: 20}` - two legal keys - was stored as a range that holds no key at all.
`noteOn()` (line 89, before the fix) then failed every note against `key < low || key > high`
and dropped it, silently, with the transform `active`. The ordering is now checked before
anything is stored, against whichever end the request does not name
(`src/core/MidiInputTransform.cpp:184-189`).

Commit: f05430161

Test: MidiInputTransformTest::anInvertedKeyRangeIsRefused

Red/green:
- Red: `cd build/tests && ctest -R '^MidiInputTransformTest$' --output-on-failure` → EXIT=8
  (`'!set(... key_low 100, key_high 20)' returned FALSE`)
- Green: same command → EXIT=8 with only cluster 557's case still red at this point; final
  `EXIT=0` (10 passed) after f02abced5, with this case green throughout.

### Cluster 557 — Fractional transform settings can be accepted as integers

Verdict: FIXED

Evidence: `src/core/MidiInputTransform.cpp:160` (before the fix) tested `value.isDouble()` and
then compared `value.toInt()` with the setting's range. `QJsonValue::toInt()` answers its
default, 0, for a number that is not whole, so `transpose: 2.5` passed the range check and was
stored as 0 - the request was accepted and transposed by nothing. (The same conversion also let
a huge value such as `velocity_scale: 1e20` through as 0.) Every setting is now compared as the
double it is and must be integral before anything is stored
(`src/core/MidiInputTransform.cpp:168-178`).

Commit: f02abced5

Test: MidiInputTransformTest::aFractionalSettingIsRefused

Red/green:
- Red: `cd build/tests && ctest -R '^MidiInputTransformTest$' --output-on-failure` → EXIT=8
  (`'!set(... transpose 2.5)' returned FALSE`)
- Green: same command → EXIT=0 (10 passed, 0 failed)

### Cluster 4850 — Exact bar-length MIDI captures become one bar longer

Verdict: FIXED

Evidence: `src/core/ControlCommandsMidi.cpp:325-336` (before the fix) computed the default clip
length as `((windowEnd - windowStart) / bar + 1) * bar` - a bar added after the integer
division, which contradicts its own comment ("the window rounded up to whole bars"). A window
of exactly one bar therefore produced a two-bar clip. The span is now rounded up
(`((span + bar - 1) / bar) * bar`) and the one-bar minimum is enforced separately
(`src/core/ControlCommandsMidi.cpp:325-341`). A non-multiple span is unchanged, which is why the
pre-existing 220-tick → two-bar case still passes.

Commit: 88b8c26c5

Test: RetroMidiCaptureCommandsTest::theDefaultLengthIsTheWindowRoundedUpToWholeBars

Red/green:
- Red: `cd build/tests && ctest -R '^RetroMidiCaptureCommandsTest$' --output-on-failure` → EXIT=8
  (`Actual 384, Expected 192`)
- Green: same command → EXIT=0 (16 passed, 0 failed), plus
  `ctest -R '^CaptureMidiMenuTest$' --output-on-failure` → EXIT=0 (the other test that covers the
  changed file)

### Cluster 187 — MIDI snapshot buffer can be too small for the captured window

Verdict: REAL-UNTESTED

Evidence: `src/core/ControlCommandsMidi.cpp:89-113`. `snapshotWindow()` sizes the vector from
`ring.bufferedCount()` at line 93 and only then calls `ring.beginSnapshot()` at line 95.
`copyOut()` copies `min(retained, maxEvents)` OLDEST events (`include/RetroMidiRing.h:178`), so
events pushed between the count read and the snapshot are silently dropped rather than reported:
`snapshot.refused` is set only when `copied == 0` (line 107-112). Once `writerIdle()` is true the
producer is serialized behind its own acknowledgement, so `bufferedCount()` read after the wait is
exact - the read is simply one step early. Confirmed by reading the ring, not from the report.

Not fixed: the defect is a genuine race whose window is ~tens of nanoseconds between an
allocation and an atomic store. Reproducing it deterministically headlessly needs a producer
thread timed to land inside that window (a hot spinning producer would do it, but neither the
RED nor a future regression is deterministic), and there is no seam to inject one - `liveCapture()`
hands out the engine's own client and `snapshotWindow()` is file-local. Per the lane rule
(no product change without a deterministic failing test) the code is left alone; the one-line fix
is to move the `resize()` after the quiesce wait at line 102.

Commit: none

Test: none

Red/green: none

### Cluster 1115 — Binding undo can dereference a deleted MIDI port

Verdict: REAL-UNTESTED

Evidence: `src/core/ControlCommandsMidiReconnectEdit.cpp:204-211`. `recordBindingInverse()` captures
a raw `MidiPort*` in both the undo and the redo lambda, and the handler calls it with
`target.port` (line 304). A track deletion destroys its `MidiPort`
(`InstrumentTrack::midiPort()`), and the step survives on the undo stack, so undoing the binding
calls `applyBinding()` on freed memory. The fix is the `recordedChainFinder()` shape already
introduced for device chains in `include/ControlStructuralSupport.h:150` - capture the
`trk-<n>` id and resolve the live port on each replay.

Not fixed: `midi.reconnect_set` needs the engine's live MIDI client to publish a port list
(`liveClient()` reads `AudioEngine::midiClient()`, and `AudioEngine::m_midiClient` is private with
no setter, `include/AudioEngine.h:608`). Headless the client is the dummy one with an empty port
list, so the command refuses at `resolveTargetName()` before any binding is recorded; the engine
test `MidiReconnectTest` drives `MidiReconnect` with its own synthetic client, not this handler,
and `docs/KNOWN-LIMITATIONS.md:1848` records the bind as socket-only. The use-after-free itself is
also unobservable without a sanitizer: it writes into freed memory. Two reasons not to touch the
code blind.

Commit: none

Test: none

Red/green: none

### Cluster 188 — Repeating an existing MIDI binding makes undo detach it

Verdict: REAL-UNTESTED

Evidence: `src/core/ControlCommandsMidiReconnectEdit.cpp:293-305`. `recordBindingInverse()` is
called before every write, including when the bind changes nothing (`heldBefore` already true at
line 293; the result reports `changed: false` at line 309). That recorded step's undo action is
`applyBinding(port, readable, name, !subscribe)`, i.e. an unsubscribe - so undoing a bind that
changed nothing takes off the binding the first bind created. The fix is to skip the write and
record no step when the command changes nothing (and report it as not reversible rather than
claiming a step).

Not fixed: same reason as cluster 1115 - the handler is unreachable headlessly without a live MIDI
client, so no regression test can bind anything to begin with.

Commit: none

Test: none

Red/green: none

Leftovers: cluster 187 (one-line fix identified, no deterministic test), cluster 1115 and cluster
188 (real, reachable only through the socket-only `midi.reconnect_set` path; both want the same
track-id resolution in `recordBindingInverse`, and 188 additionally wants the no-op write skipped).
Gates (`tests/complexity-gate.sh --check`, `tests/file-length-gate.sh --check`) returned EXIT=0
before every commit in this batch.
