# cmds2 batch-09 — 2026-10-05

Six findings. Three fixed with regression tests, three false positives. The two
`record.*` no-engine claims and the `plugin.record` classification claim were
checked against the current tree and each is refused by an existing guard, a
reversibility-table row, or both.

### Cluster 5746 — Removing a rack zone publishes an unusable inverse

Verdict: FIXED

Evidence: `src/core/ControlCommandsRackZones.cpp` `registerZoneRemove` named the
inverse transaction `rack.zone_add` but passed only `{channel, zone}` — and
`rack.zone_add`'s args schema requires `chain` and is closed
(`additionalProperties: false`), so the advertised inverse could not be replayed
and carried none of the removed zone's range or sample values. The inverse args
now carry `channel`, `chain`, `low_key`, `high_key`, `low_velocity`,
`high_velocity`, plus `sample` when the zone had one, which is the full zone the
recorded undo step restores.

Commit: e2bbacec3

Test: RackZonesTest::zoneRemoveAdvertisesAReplayableInverse

Red/green:
- Red: `cmake --build build --target zene RackZonesTest` EXIT=0;
  `cd build/tests && ctest -R '^RackZonesTest$' --output-on-failure` EXIT=8 —
  `inverseArgs.value("chain") was 0, expected 1`
- Green: same build EXIT=0, same ctest EXIT=0; neighbours
  `ctest -R '^(RackMacrosTest|RackTest|RackZonesTest|RackZoneRoutingTest)$'` EXIT=0 (4/4)
- Gates: `complexity-gate.sh --check` EXIT=0; `file-length-gate.sh --check` EXIT=0

### Cluster 3949 — `transport.punch_here` treats a missing edge as "in"

Verdict: FIXED

Evidence: `src/core/ControlCommandsRecordTools.cpp:87-95` — the handler treated
every `edge` value other than `"out"` as punch-in, so a call with `{}` silently
armed a punch region at the play position. It now refuses a missing or
unrecognised `edge` with `InvalidArgs` before reading the play position or the
punch state. The args schema was deliberately left alone: making `edge` a
required schema property would move the ControlCommands snapshot canary (rule 8),
and the handler-side refusal is the behaviour the finding asks for.

Commit: c4583b849

Test: RecordToolsTest::punchHereRefusesAMissingEdge

Red/green:
- Red (handler temporarily reverted): build EXIT=0;
  `ctest -R '^RecordToolsTest$' --output-on-failure` EXIT=8, 2 failed —
  `missing.errorKind` 0 vs `InvalidArgs` 3, and the stray punch broke
  `punchAtThePlayPosition`
- Green: build EXIT=0, `ctest -R '^RecordToolsTest$'` EXIT=0
- Gates: `complexity-gate.sh --check` EXIT=0; `file-length-gate.sh --check` EXIT=0

### Cluster 4900 — `record.remaining` checks free space on the wrong filesystem

Verdict: FIXED

Evidence: `src/core/ControlCommandsRecordTools.cpp:114-125` measured
`QFileInfo(ConfigManager::inst()->recoveryFile()).absolutePath()` while
`record.arm_track` arms a route with a caller-chosen `file` (absolute by default
beside the recovery file), so an armed route writing to a nearly full volume on
another filesystem read as plenty of time. `remaining()` now counts armed routes
per directory they were armed to write into (`TrackRecorder::filePath()`,
`include/TrackRecorder.h:128`) and reports the limiting place — the one with the
least room left for what its routes write — through the unchanged result keys
`seconds`, `armed_routes`, `free_bytes`, `directory`. With nothing armed it
still reports the recovery file's directory.

Commit: e39984ffb

Test: RecordToolsTest::theRemainingTimeMeasuresWhereTheArmedRoutesWrite

Red/green:
- Red: `cmake --build build --target zene RecordToolsTest` EXIT=0;
  `ctest -R '^RecordToolsTest$' --output-on-failure` EXIT=8, 1 failed —
  actual `directory` `/home/kruzzzzy/Documents/Zene Studio` (recovery dir) vs
  expected `/tmp/RecordToolsTest-TZxvnZ` (the armed take dir)
- Green: build EXIT=0, `ctest -R '^RecordToolsTest$'` EXIT=0;
  `ctest -R '^(RecordToolsTest|RecordToolsNoEngineTest|RecordingRoutesTest)$'` EXIT=0 (2 registered, 2 passed)
- Gates: `complexity-gate.sh --check` EXIT=0; `file-length-gate.sh --check` EXIT=0

### Cluster 3946 — `record.remaining` can crash when there is no audio engine

Verdict: FALSE-POSITIVE

Evidence: the dereference is unreachable. `ControlCommandDef::defineCommandWithSchema`
defaults `requiresEngine` to true (`include/ControlCommandDef.h:51-53`) and the
file's local `add()` helper (`src/core/ControlCommandsRecordTools.cpp:172-176`)
passes no such argument, so both commands keep the default;
`ControlRegistry::invoke` checks the engine before the handler runs and returns
the typed `Busy` ("the engine is not addressable yet …")
(`src/core/ControlRegistry.cpp:257-265`), and `isEngineReady()`
(`:179-182`) requires a Song and a Mixer — which `Engine::init` creates after the
audio engine (`src/core/Engine.cpp:71-73`) and `Engine::destroy` deletes before
it (`:110-111`), so the gate implies a non-null `audioEngine()`. No defensive
handler guard was added; the gate is the contract.

Commit: 41be6d4bc (test only, no product change)

Test: RecordToolsNoEngineTest::theGuardedSiblingRefusesTypedAndAlive,
RecordToolsNoEngineTest::remainingRefusesTypedAndAlive

Red/green:
- Red (first draft pinning `Refused`): build EXIT=0,
  `ctest -R '^RecordToolsNoEngineTest$'` EXIT=8 — both cases got `Busy` (4)
  where `Refused` (5) was expected, which is the proof that the registry gate
  answers first
- Green: rewritten to assert only what must hold (typed failure, names the
  engine, no crash), build EXIT=0, ctest EXIT=0
- New test file registered in `tests/CMakeLists.txt` and `tests/fork-sources.txt`
- Gates: `complexity-gate.sh --check` EXIT=0; `file-length-gate.sh --check` EXIT=0

### Cluster 3950 — `record.route_source` can crash when there is no audio engine

Verdict: FALSE-POSITIVE

Evidence: same gate as 3946 — `record.route_source` is registered through the
same local `add()` (`src/core/ControlCommandsRecordTools.cpp:172-176`), so
`requiresEngine` stays true and `src/core/ControlRegistry.cpp:257-265` returns
the typed `Busy` before `routeSource`'s
`Engine::audioEngine()->recorder().trackCount()` at `:150` can run. The
`record.*` commands' sibling convention for this is explicit anyway:
`src/core/ControlCommandsRecordingRoutes.cpp:71-82` pairs a `liveRecorder()`
lookup with a `noEngine()` refusal.

Commit: 41be6d4bc (test only, no product change)

Test: RecordToolsNoEngineTest::routeSourceRefusesTypedAndAlive

Red/green: same run as cluster 3946 (one commit, one new executable) —
build EXIT=0, `ctest -R '^RecordToolsNoEngineTest$'` EXIT=0

### Cluster 1200 — `plugin.record` changes saved project state but is registered non-mutating

Verdict: FALSE-POSITIVE

Evidence: the `false` at `src/core/ControlCommandsRecorder.cpp:128` is the
`mutating` argument (mapped to `cmd.mutating` at
`src/core/ControlCommandsDeviceTools.cpp:79`), and `mutating` decides whether the
command records a reversibility transaction — `if (mutating && changed) recordTransactionOf(...)`,
`src/core/ControlRegistry.cpp:357-360` — not whether it may touch a read-only
surface. The canonical reversibility table deliberately classifies the command
as `RC::NotMutating, false` with the justification written out: the take file is
written and the Recorder's last-take path saved with the project "is not put
back" (`src/core/ControlReversibilityTableW3Devices.cpp:61-63`), matching the
command's own description "It writes the file and nothing else: no undo step"
(`ControlCommandsRecorder.cpp:125-128`). That is an irreversible action, not a
reversible mutation, and `plugin.delta` (`:59-60`) is classified the same way. The
claimed mutation-filter bypass has no effect here either: `EditLocks::refuses`
(`src/core/EditLocks.cpp:165-167`) only refuses commands `editsContent()` lists
(`:47-56`), which contains no `plugin.*` entry, and this call carries no track or
clip ids — flipping the flag would refuse nothing.

Commit: none

Test: none

Red/green: none (no product change; the table row is the design record)

Leftovers: none. No half-done finding; the working tree is clean apart from this
report's own commit.