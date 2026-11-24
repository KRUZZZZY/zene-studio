FALSE-POSITIVE

# Cluster new-p14-124-port: a track-settings load and the audio boundary

This is the port of the accepted lane `bugs/qwen-new-p14-124` (test commit
`5c386be4de`, report commit `8849b20ed3`) onto the newest line. The finding is
unchanged; the branch's code still has the shape the finding names. The prior
lane's regression test was re-applied to
`tests/src/core/InstrumentLayerZonePublishTest.cpp` and rerun here.

## The finding

`InstrumentTrack::loadTrackSpecificSettings()` releases the track's own
processing lock and then loads the layer rack, MIDI parts and output-bus routes:

- `src/tracks/InstrumentTrack.cpp:1344-1347` - `updatePitchRange(); unlock();`
  then `loadZeneInstrumentSettings(thisElement, presetMode);`.
- `src/tracks/InstrumentTrackNotes.cpp:434-446` - the loader calls
  `m_midiParts.loadSettings(element)`, `m_layers.loadSettings(element)` and
  `buses->loadSettings(element)`.

The concern is that playback can observe those mutations because they happen
after the track lock is released.

## Verdict and evidence

**FALSE-POSITIVE.** The released lock is the track's `m_processingLock`
(`include/Track.h:468-479`, member at `:616`). The render pass does take it,
but only with `tryLock()` (`src/tracks/InstrumentTrack.cpp:952`), so it is not
the serialization boundary for this state. The boundary that the render period
holds for its whole duration is `AudioEngine`'s model-change guard:

- `src/core/AudioEngine.cpp:1221-1224` - `renderNextPeriod()` takes
  `std::lock_guard{m_changeMutex}` before any render work.
- `src/core/AudioEngine.cpp:1983-1996` - `requestChangeInModel()` locks the
  same `m_changeMutex`; the comment at `:1985` says the rendering thread
  short-circuits, so a control-thread load waits for the period boundary rather
  than racing it.

Every render-visible publication in the three loaders takes that guard, so the
two call sites that hold no outer guard (preset load and preset preview) are
still inside the audio boundary:

| Loader path | Guard evidence |
|---|---|
| MIDI parts | `src/core/InstrumentMidiParts.cpp:104-108` -> one `m_zones.loadSettings(...)`; `src/core/RackZones.cpp:91-101` `RackZones::edit()` takes `requestChangesGuard()` around the change and the changed hook; the load swap is `RackZones.cpp:258-261`. |
| Layer slots and layer zones | `src/core/RackZones.cpp:91-101` (zones); `src/core/InstrumentLayers.cpp:200-216` `adopt()` holds `ChangeGuard` across the slot stores; `:262-274` `removeLayer()` holds it across the slot shift and delete; `:361-379` `loadSettings()` runs `clear()`, `adopt()` and `m_zones.loadSettings()`, all through those guarded paths. |
| Output-bus routes | `src/core/InstrumentOutputBuses.cpp:160-174` `storeRoute()` prepares detached storage before the guard and installs the slot/channel under `ChangeGuard` at `:171-173`; `loadSettings()` reaches it through `clearRoutes()` and `route()` (`:203-240`). |

The layer mix publication (`InstrumentLayers::publishMix()`,
`src/core/InstrumentLayers.cpp:398-409`) writes `AudioBusHandle::setGain`
atomics, not plain shared state, so it needs no model-change guard. Everything
else in the loader is either an atomic or control-thread-only state (`m_mix`).

The project-open path is additionally covered by the outer guard that
`Track::loadTrack()` takes at `src/core/Track.cpp:929`; the preset paths return
before it, which is exactly why the loaders must guard themselves. They do.

## Regression test

`tests/src/core/InstrumentLayerZonePublishTest.cpp`
`settingsLoadPublishesUnderTheModelChangeGuard()` drives the two loaders that
expose a changed hook - layers and MIDI parts - with no caller guard, then
drives the full `restoreState()` path. The hook records
`AudioEngine::isChangeInModelHeld()` (`include/AudioEngine.h:683`, defined
under `LMMS_TESTING` at `src/core/AudioEngine.cpp:479-482`) at every
zone publication. It asserts at least one publication happened in each load
(so the slot cannot pass vacuously) and that none was unguarded.

The test is deterministic: the seam is the engine's thread-local guard nesting
depth; the rack fixture stops its dummy device, so no period thread runs; there
are no sleeps, clocks or timing assertions.

### Non-vacuity proof (no product change is needed for a false positive)

To show the test detects the synchronization the finding is about, I temporarily
removed the `requestChangesGuard()` from `RackZones::edit()` (the publication
path both zone hooks use), rebuilt, and ran the slot. It failed with
`layerUnguarded: 2`. I then restored the product file from the saved patch,
rebuilt and reran: it passed. The saved patch is
`logs/mutation-rackzones.patch`; `git diff -- src/core/RackZones.cpp` was empty
after the restore (the only working-tree change is the test file).

## Commands and exit codes (all observed this session)

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-124-port.r2/logs`.

| # | Command | Log | EXIT |
|---|---|---|---|
| 1 | `cmake --build build --target InstrumentLayerZonePublishTest -j4` | `build-1-test.log` | 0 |
| 2 | `cmake --build build --target audiofileprocessor -j4` (the fixture needs one loadable instrument) | `build-2-audiofileprocessor.log` | 0 |
| 3 | `cd build/tests && ./InstrumentLayerZonePublishTest settingsLoadPublishesUnderTheModelChangeGuard` | `test-1-slot-green.log` | 0 (3 passed, 0 skipped) |
| 4 | `cd build/tests && ctest -R '^InstrumentLayerZonePublishTest$' --output-on-failure` | `test-2-family-green.log` | 0 |
| 5 | `git diff -- src/core/RackZones.cpp > <LOG>/mutation-rackzones.patch` | `mutation-rackzones.patch` | 0 |
| 6 | `cmake --build build --target InstrumentLayerZonePublishTest -j4` (mutation applied) | `build-3-mutation.log` | 0 |
| 7 | `cd build/tests && ./InstrumentLayerZonePublishTest settingsLoadPublishesUnderTheModelChangeGuard` (mutation applied) | `test-3-slot-mutation-red.log` | **1** - `layerUnguarded` Actual 2, Expected 0 |
| 8 | `git apply -R <LOG>/mutation-rackzones.patch` | - | 0 |
| 9 | `cmake --build build --target InstrumentLayerZonePublishTest -j4` (restored) | `build-4-restored.log` | 0 |
| 10 | `cd build/tests && ./InstrumentLayerZonePublishTest settingsLoadPublishesUnderTheModelChangeGuard` (restored) | `test-4-slot-restored-green.log` | 0 |
| 11 | `cmake --build build --target InstrumentLayerZonePublishTest InstrumentLayersCommandsTest InstrumentTrackRenderPathTest RackZonesTest RackZoneRoutingTest RackZoneFeedRunTest TrackPresetCommandsTest -j4` | `build-6-family.log` | 0 |
| 12 | `cd build/tests && ctest -R '^(InstrumentLayerZonePublishTest\|InstrumentLayersCommandsTest\|InstrumentTrackRenderPathTest\|RackZonesTest\|RackZoneRoutingTest\|RackZoneFeedRunTest\|TrackPresetCommandsTest)$' --output-on-failure` | `test-6-family.log` | 0 (7/7) |
| 13 | `FEATURE_CHECK_LOG_DIR=<LOG> python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | `gates-1.log` | 0 - all 8 rows PASS, `RESULT: PASS` |

An earlier family run (before step 11's build) exited 8 with six tests
`Not Run`: the test binaries did not exist in this build tree. After building
them, step 12 passed 7/7; the two runs are `test-5-family.log` and
`test-6-family.log`.

## Self-check

- **Regression test fails with the synchronization removed and passes restored.**
  This is a FALSE-POSITIVE closure, so there is no product change to revert:
  steps 6-7 removed `RackZones::edit()`'s guard and the slot exited 1
  (`layerUnguarded: 2`, `test-3-slot-mutation-red.log`); steps 8-10 restored it
  and the slot exited 0 (`test-4-slot-restored-green.log`). The test also proved
  it ran rather than skipped (`0 skipped`).
- **Deterministic.** The probe is `AudioEngine::isChangeInModelHeld()`, a
  thread-local nesting-depth read of the guard, and the fixture's device is
  stopped by `racktest::initRackFixture`; there are no sleeps, clocks, threads or
  timing assertions in the new slot.
- **Cause and completeness.** No product change was made. I rechecked every
  caller of the loader (`Track::loadTrack` preset branch, project branch and
  `PresetPreviewPlayHandle`) and every render-visible publication in the three
  loaders (table above). The two zone lists use `RackZones::edit`; layer slots
  use `ChangeGuard` in `adopt`/`removeLayer`; routes use `ChangeGuard` in
  `storeRoute`; layer gains are atomics.
- **No collateral damage.** No saved format, loader, command, limit or undo path
  changed. The only tracked change is 82 added lines in the existing test file;
  the seven sibling suites in step 12 still pass.
- **FALSE-POSITIVE evidence.** `include/Track.h:468-479` and `:616` identify the
  released lock; `src/tracks/InstrumentTrack.cpp:952` shows the render pass takes
  it with `tryLock`; `src/core/AudioEngine.cpp:1221-1224` and `:1983-1996` show
  the actual boundary; `src/core/RackZones.cpp:91-101`,
  `src/core/InstrumentLayers.cpp:200-216,262-274,361-379` and
  `src/core/InstrumentOutputBuses.cpp:160-174` show every loader publication
  taking that boundary.
- **Realtime / undo / commands.** No product code changed, so nothing new runs
  on the audio thread, no lock or allocation moved, no command was added and no
  undo step changed.
- **Sibling commands.** No control-command file is changed by this cluster, so
  there are no sibling commands to reconcile.
- **Qt portability.** The test uses Qt 5/6-compatible QtTest and `QDomDocument`
  APIs; no new Qt-6-only call, no platform API.

## Other findings

None investigated or changed here. The grep of this area did show the prior
lane's note about `InstrumentMidiParts::loadSettings` leaving `m_keyChannel`
entries from the previous parts; that is a separate candidate and is not part of
this concurrency finding.
