# Space Bunny — fixer lane `cmds2` — batch 03 (2026-10-05)

Clusters 5718, 5719, 5726, 4868, 2891, 5720 on branch `bugs/bunny-cmds2-2026-10-05`.

### Cluster 5718 — Routing a bus to its current channel adds a no-op undo step

Verdict: FIXED
Evidence: `src/core/ControlCommandsPluginBuses.cpp:182-193` called `buses.route()` and then recorded
an undo step unconditionally. When `previous == channelId` the recorded step is a no-op; consuming it
on undo hides the earlier real edit. The handler now performs the route and returns the current bus
state with no undo step and no `__transaction` when the channel is unchanged.
Commit: c86332d66
Test: InstrumentLayersCommandsTest::multiOutputBusesRouteCreateUndo
Red/green:
- RED: `cmake --build build --target zene InstrumentLayersCommandsTest > build/bunny-b.log 2>&1; echo EXIT=$?` EXIT=0;
  `cd build/tests && ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` EXIT=8
  (FAIL: recorded transaction count 12, expected 11).
- GREEN: same build EXIT=0; same ctest EXIT=0 for the regression case.

### Cluster 5719 — Creating channels with nothing to create claims an undo step that does not exist

Verdict: FIXED
Evidence: `src/core/ControlCommandsPluginBuses.cpp:283-301` skipped `recordCreation` when `created` was
empty but still returned a `__transaction` payload. `ControlRegistry::changedNothing()`
(`src/core/ControlRegistry.cpp:298-303`) treats any `__transaction` as proof of change, so the
response advertised a reversible transaction that was never recorded. `createChannels` now returns
early with an empty `created` and no `__transaction`.
Commit: 40fa861a6
Test: InstrumentLayersCommandsTest::multiOutputBusesRouteCreateUndo
Red/green:
- RED: `cmake --build build --target zene InstrumentLayersCommandsTest > build/bunny-b.log 2>&1; echo EXIT=$?` EXIT=0;
  `cd build/tests && ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` EXIT=8
  (FAIL: a no-op `create_channels` still reported a transaction).
- GREEN: same build EXIT=0; same ctest EXIT=0 for the regression case.

### Cluster 5726 — `plugin.prefer_format` omits transaction data after changing configuration

Verdict: FIXED
Evidence: `src/core/ControlCommandsPluginOrganise.cpp:124` added an undo action when the preferred
format changed but supplied no payload, so `ControlRegistry::recordTransactionOf()`
(`src/core/ControlRegistry.cpp:367-409`) fell back to a generic record with no prior value. The handler
now tracks `changed`, attaches a `transactionPayload` (with `before`/`inverse`) on a real change and
returns bare state on a no-op. Because `finishResult` strips `__transaction`, the regression asserts
through the recorded transaction rather than the invoke result.
Commit: 26a993b6e
Test: DeviceCopyTest::aPreferredFormatIsKeptAndUndone
Red/green:
- RED: `cmake --build build --target zene DeviceCopyTest > build/bunny-b.log 2>&1; echo EXIT=$?` EXIT=0;
  `cd build/tests && ctest -R '^DeviceCopyTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` EXIT=8
  (FAIL: the recorded transaction `before` had no `format` field).
- GREEN: same build EXIT=0; same ctest EXIT=0 for the regression case.

### Cluster 4868 — `plugin.prefer_format` is registered as non-mutating

Verdict: FALSE-POSITIVE
Evidence: `src/core/ControlCommandsPluginOrganise.cpp:157` is the `plugin.organisation` registration,
which correctly keeps `cmd.mutating = false`. Before registering `plugin.prefer_format` the same
command object sets `cmd.mutating = true` at `src/core/ControlCommandsPluginOrganise.cpp:167`, so the
reused object is already re-flagged. Nothing is stale at the registration point named by the report.
Commit: none
Test: none
Red/green: none.

### Cluster 2891 — Setting a parameter to its current value can create a useless journal checkpoint

Verdict: REAL-UNTESTED
Evidence: `src/core/ControlCommandsPluginParams.cpp:240-245` checkpoints and sets the model even when
the requested value equals `previous`, so a no-op `param_set` consumes an undo step. The behaviour is
real, but no instrument or effect plugin with an automatable/adjustable parameter is loadable in this
headless build, so `src/core/ControlCommandsPluginParams.cpp` cannot be driven to a deterministic RED
from the test fixtures. Per the lane rules the product change was reverted rather than shipped
unverified; the tree is at HEAD for that file.
Commit: none
Test: none (a candidate case in ReversibilityUndoTest was removed again)
Red/green: none — `cd build/tests && ctest -R '^ReversibilityUndoTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` EXIT=8 with `firstAutomationPointIsOneUndoableStep` failing for the same environmental reason (pre-existing, no automatable parameter loads).

### Cluster 5720 — Removing a MIDI part reports an unusable inverse

Verdict: FIXED
Evidence: `src/core/ControlCommandsPluginParts.cpp:232-235` reported an inverse invoking
`plugin.midi_part_add` with only `target`, while that command also requires `midi_channel` and the
part's zone ranges — the inverse could not be replayed. The inverse args now carry
`midi_channel` (`zone.chain + 1`, since the zone stores the channel zero-based), `low_key`,
`high_key`, `low_velocity` and `high_velocity`. The regression reads the recorded transaction and
actually replays the inverse.
Commit: 427cc1f30
Test: InstrumentLayersCommandsTest::midiPartRemoveInverseRestoresThePart
Red/green:
- RED: `cmake --build build --target zene InstrumentLayersCommandsTest > build/bunny-b.log 2>&1; echo EXIT=$?` EXIT=0;
  `cd build/tests && ctest -R '^InstrumentLayersCommandsTest$' --output-on-failure > build/bunny-t.log 2>&1; echo EXIT=$?` EXIT=8
  (FAIL: replayed inverse `midi_channel` 0, expected 3).
- GREEN: same build EXIT=0; same ctest EXIT=0 for the regression case.

## Totals

FIXED 4 (5718, 5719, 5726, 5720) · FALSE-POSITIVE 1 (4868) · REAL-UNTESTED 1 (2891).
Both gates (`bash tests/complexity-gate.sh --check`, `bash tests/file-length-gate.sh --check`) returned
EXIT=0 before every one of the four fix commits.

Leftovers: Cluster 2891 is adjudicated real but deliberately unfixed — no loadable instrument/effect
parameter exists in this build to produce a deterministic RED, so it needs a follow-up lane with a
parameter-bearing fixture. Pre-existing/environmental failures remain in the shared test binaries and
are not caused by these fixes: `InstrumentLayersCommandsTest::outputBusesAnswerTyped` (`bus_count >= 1`,
device resolution changes once the `clapinstrument` target is built) and
`DeviceCopyTest::aChainIsBypassedAndCleaned` plus
`ReversibilityUndoTest::firstAutomationPointIsOneUndoableStep` (no effect modules / no automatable
parameter load in this environment; both already failed on the first run before any edit).