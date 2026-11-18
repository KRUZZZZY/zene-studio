# Plugins lane — batch 03 (2026-10-05)

Six findings, all in the CLAP host (`plugins/ClapEffect`). Two fixed with regression tests, two
already fixed on this branch, one duplicate of another finding in this batch, one left as
SKIPPED-SCOPE with evidence.

### Cluster 5549 — Note counters remain stale when reloading a plug-in without note ports

Verdict: FIXED

Evidence: `plugins/ClapEffect/ClapHostNotes.cpp:141` returned `if (!notePortsExt)` *before* the
four counter resets, and `HostedPlugin::unload()` (`plugins/ClapEffect/ClapHost.cpp:287-323`) does
not reset them either — so an instance reloaded from a note-taking plug-in onto one with no
`clap.note-ports` extension kept the previous instance's totals. The four `store(0)` calls now run
before the extension check (`plugins/ClapEffect/ClapHostNotes.cpp:141-148`).

Commit: 4dc1834e4

Test: `ClapHostTest::testReloadingAPluginWithoutNotePortsClearsTheCounters` (new case in the
existing `tests/src/plugins/ClapHostTest.cpp`; uses the in-tree `clap-test-instrument` fixture for
the note-port instance and `clap-test-gain` for the reload)

Red/green:
- red: `cmake --build build --target zene ClapHostTest` EXIT=0; `cd build/tests && ctest -R '^ClapHostTest$' --output-on-failure` EXIT=8 — 23 passed, 2 failed, `FAIL! ...testReloadingAPluginWithoutNotePortsClearsTheCounters() 'after.pushed == 0 && ...' returned FALSE (the reloaded instance kept the old counters: pushed 2, dropped 0, delivered 2, played 1)`
- green: same build EXIT=0; same ctest EXIT=0 — 25 passed, 0 failed
- also green: `ctest -R '^Clap(EffectIntegration|BusMap|LoaderError)Test$' --output-on-failure` EXIT=0
- gates: `tests/complexity-gate.sh --check` EXIT=0; `tests/file-length-gate.sh --check` EXIT=0

Note on the red run: the first attempt failed on `QCOMPARE(busy.delivered, 2)` instead of the
reset, because `delivered` only moves when `process()` drains the queue. The case was corrected to
run a block first, and the red was re-taken against the reverted fix so it fails on the stale
counters themselves.

### Cluster 3746 — NaN parameter values enter the processing event stream

Verdict: FIXED

Evidence: `plugins/ClapEffect/ClapHostParams.cpp:220` was `std::clamp(value, min, max)`, which does
not sanitize NaN (every comparison against it is false), so a NaN set by a caller was stored in the
parameter slot and published to the processor as a `CLAP_EVENT_PARAM_VALUE`
(`plugins/ClapEffect/ClapHost.cpp:386` reads the slot straight into the event). The load-time scan
had the same clamp (`ClapHostParams.cpp:131`) and `mirrorParamValues()`
(`plugins/ClapEffect/ClapHostInternals.h:429`) stored the plug-in's own reported value with no
clamp at all. All three boundaries now go through one `sanitizedValue()`
(`plugins/ClapEffect/ClapHostInternals.h:75-85`), which falls back to the descriptor's default for a
value that is not a number.

Commit: 69669b381

Test: `ClapHostTest::testANonFiniteParamValueNeverReachesThePlugin` (new case in the existing
`tests/src/plugins/ClapHostTest.cpp`, plus a `CLAP_TEST_GAIN_NAN_VALUE` switch added to the
`tests/data/clap-test-plugin/clap-test-gain.c` fixture so the *plug-in* can report a NaN too)

Red/green:
- red: `cmake --build build --target zene ClapHostTest` EXIT=0; `cd build/tests && ctest -R '^ClapHostTest$' --output-on-failure` EXIT=8 — 23 passed, 2 failed, `FAIL! ...testANonFiniteParamValueNeverReachesThePlugin() 'std::isfinite(plugin.paramPlain(GainParamId))' returned FALSE (a NaN the caller set was stored in the parameter slot)`
- green: same build EXIT=0; same ctest EXIT=0 — 25 passed, 0 failed
- also green: `ctest -R '^Clap(EffectIntegration|BusMap|LoaderError)Test$' --output-on-failure` EXIT=0
- gates: `tests/complexity-gate.sh --check` EXIT=0; `tests/file-length-gate.sh --check` EXIT=0

Note on the red run: the first attempt asserted that a rejected NaN leaves the *last good* value
held. The implemented contract is the descriptor's default, so the case now asserts what the defect
is actually about — finite, and inside the parameter's range — and the red was re-taken against the
reverted fix.

### Cluster 4690 — CLAP parameter rescans leave stale models and descriptors

Verdict: ALREADY-FIXED

Evidence: the finding cites revision `e5a603132`; the current tree already does all of it.
`plugins/ClapEffect/ClapHostParams.cpp:176-178` — `serviceRescan()` mirrors values for
VALUES/TEXT/INFO and calls `refreshParamInfo()` for INFO; `refreshParamInfo()`
(`ClapHostParams.cpp:139-169`) re-reads the whole list into `fresh`, and either updates in place the
fields the audio thread never reads (title, module, periodic, hidden, automatable, requiresProcess,
modulatablePerNote, defaults) or, when more than RESCAN_INFO covers changed, escalates to
`paramsChanged` + `needsReprepare` (`:151-152`). `CLAP_PARAM_RESCAN_ALL` is taken where it is asked
for (`plugins/ClapEffect/ClapHostInternals.h:385-392`), and the next `prepare()` re-reads the list
deactivated via `rescanWhileInactive()` (`plugins/ClapEffect/ClapHostLifecycle.cpp:117`) into fresh
descriptors, ids, cookies and value slots (`ClapHostParams.cpp:118-137`), bumping `paramsRevision`
both ways so the owners re-sync their models. `ClapHostTest::testParameterRescansRereadTheList`
covers exactly this and passes.

Commit: none

Test: `ClapHostTest::testParameterRescansRereadTheList` (pre-existing, passes)

Red/green: none — no change was made.

### Cluster 947 — Parameter-list rescans never rebuild the host's parameter state

Verdict: DUPLICATE (of cluster 4690 in this batch)

Evidence: the same defect stated at the older revision `e5a603132`, against the same two lines
(`ClapHostParams.cpp:153-157` today). "RESCAN_ALL only requests re-prepare" is not what the current
tree does — see the `paramsRescan` / `rescanWhileInactive` chain cited for 4690 above. The finding's
second premise is also wrong: `paramsClear()` (`plugins/ClapEffect/ClapHostInternals.h:397`) is the
*host → plug-in* callback in which the host asks the plug-in to drop pending value requests, not a
path through which a plug-in reports a changed list, and a host is free to ignore it. So it cannot
be what left models and slots stale.

Commit: none

Test: none

Red/green: none — no change was made.

### Cluster 5552 — Read-only CLAP parameters are still shown as editable knobs

Verdict: ALREADY-FIXED

Evidence: `plugins/ClapEffect/ClapKnobGrid.h:62` already skips read-only parameters:
`if (descriptor.hidden || descriptor.readOnly) { continue; }`, with the comment recording exactly
the snap-back behaviour the finding describes. `descriptor.readOnly` is decoded from
`CLAP_PARAM_IS_READONLY` in `descriptorOf()` (`plugins/ClapEffect/ClapHostParams.cpp:82`). This is
commit `b85f5f5f8` ("fix(clap): read-only parameters got an editable knob", Luna cluster 1902), which
is already on this branch.

Commit: none (already fixed in b85f5f5f8)

Test: none in this batch

Red/green: none — no change was made.

### Cluster 5544 — MIDI-only CLAP ports receive unsupported CLAP note events

Verdict: SKIPPED-SCOPE

Evidence: the reported behaviour is accurate but is a documented design decision at
`plugins/ClapEffect/ClapHostNotes.cpp:170-179`: the scan prefers a port that declares the CLAP
dialect and otherwise falls back to the first port, on the reasoning that the dialect says what a
plug-in can *additionally* accept. Both halves of the suggested fix do not land here. "Select only a
CLAP-dialect port" is a no-op: when no port matches, `notePortIndex` keeps its value of 0
(`plugins/ClapEffect/ClapHostInternals.h:154`, also reset to 0 in `unload()`), which is the same
first-port choice. "Translate events to a supported dialect" means building
`clap_event_midi`/`midi2`/MPE conversion — a new event dialect and a new conversion path in the
audio-thread drain (`drainNoteQueue()`, `ClapHostNotes.cpp:280-316`) — which is a design change well
past a defect patch. Note the MIDI dialect genuinely is a different event type
(`plugins/CarlaBase/carla/source/includes/clap/ext/note-ports.h:20-29`: `CLAP_NOTE_DIALECT_MIDI`
"Uses clap_event_midi"), so the finding is right about the spec and wrong that it is a one-line
selection bug. Related and also absent: the host never registers `clap_host_note_ports`, so a
plug-in has no `supported_dialects()` to consult at all; adding it is the same design decision.

Commit: none

Test: none

Red/green: none — no change was made.

Leftovers: cluster 5544's real gap (no `clap_host_note_ports::supported_dialects`, and no MIDI/MIDI-2
event dialect in the note drain) is recorded above but not addressed; it needs a design decision on
which dialects this host emits, so it should be filed as its own task rather than a batch bug fix.
