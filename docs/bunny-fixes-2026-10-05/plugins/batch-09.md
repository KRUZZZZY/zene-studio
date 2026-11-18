# Bunny fixes — plugins batch-09

Lane: `plugins` (plugin hosts and plugins). Branch `bugs/bunny-plugins-2026-10-05`.
Six Luna clusters were investigated by hand against the current tree.

## Summary

| Cluster | Title | Verdict |
|---|---|---|
| 5459 | Reprepare can clear buffers while the audio thread uses them | REAL-UNTESTED |
| 5460 | Boundary MIDI events are sent past the end of the VST3 block | SKIPPED-SCOPE |
| 3625 | Non-finite plug-in parameter values reach audio processing | FIXED |
| 863 | VST3 class names are used as unique class identifiers | SKIPPED-SCOPE |
| 2613 | MIDI drop counters race | REAL-UNTESTED |
| 5458 | VST3 transport sample positions are not valid project positions | SKIPPED-SCOPE |

1 FIXED, 2 REAL-UNTESTED, 3 SKIPPED-SCOPE, 0 FALSE-POSITIVE, 0 ALREADY-FIXED, 0 DUPLICATE.

### Cluster 5459 — Reprepare can clear buffers while the audio thread uses them
Verdict: REAL-UNTESTED

Evidence: `plugins/Vst3Effect/Vst3Host.cpp:461-464` — `prepare()` calls `release()`, and
`release()` (`plugins/Vst3Effect/Vst3Host.cpp:549-574`) clears the bus and event vectors that
`process()`/`sliceEvents()` (`plugins/Vst3Effect/Vst3Host.cpp:600-723`) read. There is no
synchronization between the prepared flag and those containers, so the GUI-thread entry point
`Vst3Effect::reprepare()` (`plugins/Vst3Effect/Vst3Effect.cpp:121-133`) racing the audio thread
is a genuine data race.

No deterministic headless reproduction exists for this ordering, so per the lane rules no
product code was changed. Verified: the race is real, but not testable as written.

Commit: none
Test: none
Red/green: none

### Cluster 5460 — Boundary MIDI events are sent past the end of the VST3 block
Verdict: SKIPPED-SCOPE

Evidence: real. `drainMidiIntoEventList()` used `std::clamp(frameOffset, 0, frames)`, so an
event written at the request boundary reached the plug-in at `sampleOffset == numSamples`, one
past the last frame — outside the block every VST3 event offset must lie in. Confirmed by
reproducing it: `Vst3HostTest::testBoundaryMidiEventsStayInsideTheRequest` failed RED with
`event.sampleOffset` = 256 for `Frames` = 256.

The in-block mapping is the part that is out of scope. Clamping the boundary event to
`frames - 1` — the same bound the host already applies to a parameter point on that boundary —
was implemented and measured: it fires a boundary note-off one sample early, which changes
audible behaviour and breaks three existing, deliberate assertions in
`Vst3InstrumentTest` (`testDistinctInputGivesDistinctOutput`,
`testNoEventLeaksBetweenBlocks`, `testStateRoundTripsIntoAFreshInstance`). That is the exact
"a note-off written at the block boundary becomes a note that never releases" case the current
comment in `sliceEvents` records as caught earlier. The other option — carrying the event into
the next block at offset 0, which is what the report actually asks for — needs cross-block
carry state on the audio-thread path, a design change beyond this lane's scope. Per rules 5, 6
and 7 the fix was reverted and no test was weakened to make it green.

Commit: none
Test: none (the RED case was written and then reverted with the fix)
Red/green:
  red:   ctest -R '^Vst3HostTest$' --output-on-failure -> EXIT=8
         event.sampleOffset < Frames returned FALSE (256)
  green: n/a — the corresponding change breaks Vst3InstrumentTest (EXIT=8, 9 passed / 3 failed)
         measured against the reverted baseline, which is EXIT=0

### Cluster 3625 — Non-finite plug-in parameter values reach audio processing
Verdict: FIXED

Evidence: `std::clamp` does not sanitize NaN (both of its comparisons are false for it), and a
`ParamValue` too large for `float` overflowed to infinity on the cast. Four sites stored the
result unsanitized: `setParamNormalized()` (`plugins/Vst3Effect/Vst3Host.cpp:366-373`),
`notifyController()` (`:375-384`), `loadState()` (`:449-457`) and
`Vst3ComponentHandler::performEdit` (`plugins/Vst3Effect/Vst3ComponentHandler.h:71-82`). The
array they write is what the audio thread hands to the processor's parameter queue.

Added `sanitizedNormalized(double, float keep)` to
`plugins/Vst3Effect/Vst3ParamDescriptor.h`: a finite value is clamped into [0, 1], anything not
finite is refused and the last good value stays. All four sites now use it. No allocation, no
locking, no change to any control command or its schema.

Commit: dad1217e6
Test: lmms::vst3::Vst3HostTest::testNonFiniteParameterValuesAreRefused
Red/green:
  red:   ctest -R '^Vst3HostTest$' --output-on-failure -> EXIT=8
         Totals: 14 passed, 2 failed
         paramNormalized(GainParamId) actual nan, expected 0.5
  green: ctest -R '^Vst3HostTest$' --output-on-failure -> EXIT=0
         Totals: 15 passed, 0 failed
         Vst3BusMapTest, Vst3ChunkProbeTest, Vst3EffectIntegrationTest, Vst3InstrumentTest,
         Vst3InstrumentIntegrationTest all EXIT=0
         complexity-gate.sh --check EXIT=0; file-length-gate.sh --check EXIT=0

### Cluster 863 — VST3 class names are used as unique class identifiers
Verdict: SKIPPED-SCOPE

Evidence: real, but the name is the persisted project key. `load()` matches on
`info.name()` (`plugins/Vst3Effect/Vst3Host.cpp:213`, with the display name set at `:165`), and
`Vst3SubPluginFeatures.cpp:113` writes that name into the saved `class` attribute
(read back at `:51`). Switching the key to the VST3 class UID changes the project format and
requires a migration for every existing file, which is a design decision above this lane.

Commit: none
Test: none
Red/green: none

### Cluster 2613 — MIDI drop counters race
Verdict: REAL-UNTESTED

Evidence: the headline claim is stale — `droppedMidi` is already
`std::atomic<std::uint64_t>` (`plugins/Vst3Effect/Vst3HostInternals.h:91`). One part of it
stands: `droppedOutput` (`plugins/Vst3Effect/Vst3HostInternals.h:104`) is a plain
`std::uint64_t` written on the audio thread and read unsynchronized by
`HostedPlugin::droppedOutputEvents()` (`plugins/Vst3Effect/Vst3HostReports.cpp:57`).

The residual race cannot be made to fail deterministically headlessly, and fixing it changes no
observable behaviour, so per rule 2 no product code was changed.

Commit: none
Test: none
Red/green: none

### Cluster 5458 — VST3 transport sample positions are not valid project positions
Verdict: SKIPPED-SCOPE

Evidence: real. `context.projectTimeSamples = continuousSamples`
(`plugins/Vst3Effect/Vst3Host.cpp:692`, incremented at `:693`, reset at `:550`) is a host-local
counter that survives a seek, and the context advertises no validity flag for the sample-time
fields. Populating it from the song timeline means new host API plus song-position plumbing
through `Vst3Effect::processImpl()`, which is a host-interface change beyond this lane's scope.

Commit: none
Test: none
Red/green: none

Leftovers: 5460 still reaches the plug-in at `sampleOffset == numSamples` and needs an
explicit design decision (carry the event into the next block at offset 0, or accept a
one-sample-early boundary note-off); 2613's `droppedOutput` is still a plain counter written on
the audio thread; 5459's GUI/audio race on prepare/release is still unguarded; 863 and 5458 both
need decisions above this lane.