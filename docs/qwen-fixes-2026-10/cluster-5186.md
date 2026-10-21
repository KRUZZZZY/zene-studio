Verdict: FIXED

5186: ALREADY-FIXED
new-p12-159: FIXED

## 5186 - ALREADY-FIXED

Evidence (the model-lifetime half of the cluster was already fixed on this branch, before this round):

- `include/ModulationLayer.h:370-396` - `targetAlive()` / `beginTargetWrite()` / `endTargetWrite()`: the
  audio thread pins the target it writes (an atomic writer count per slot) and the pin is only taken
  while the slot's token is still the live one.
- `src/core/ModulationLayerPublisher.cpp:130-159` - `retireTarget()`: closes the slot's generation,
  waits for the in-flight writer count to drain, and only then clears the alive token. The teardown is
  scoped to the token, so a stale callback cannot mute a rebound target.
- `src/core/ModulationLayerPublisher.cpp:225-241` - `publishView()` closes the slot before the model
  tears down its value state and connects `AutomatableModel::aboutToBeDestroyed` to `retireTarget()`.
- `src/core/Song.cpp:1389-1413` - `Song::processModulation()` is the audio thread's only caller: it
  copies the plain-value view and calls `applyModulationBlock()`; a slot whose token is dead is skipped
  (`drivingSource()`, `src/core/ModulationLayerPublisher.cpp:270-278`).
- Regression already present and passing on this branch:
  `ModulationAudioViewTest::targetDestructionWaitsForAnInFlightAudioWrite` (the write that is in flight
  when the target dies is waited for) and
  `ModulationAudioViewTest::aDestroyedTargetIsSkippedByAnOlderView`.

## new-p12-159 - FIXED

The bug: the per-voice modulation target points at a raw `Instrument*`
(`include/ModulationLayer.h:276` and `:304`), and the audio thread calls it under a pin that keeps the
TARGET alive, not the instrument (`src/core/ModulationLayerPublisher.cpp:263`). The instrument was
retired by a notification that came from `Instrument::~Instrument()`
(`src/core/Instrument.cpp:78`) - a BASE destructor, which runs AFTER the derived destructor, so the
derived teardown had already begun while the target was still open. Worse, only the track destructor
notified at all: the two replacement branches of `InstrumentTrack::loadTrackSpecificSettings()` and
`InstrumentTrack::loadInstrument()` deleted the instrument with no notification at all.

The fix, at the one place every instrument of the product is destroyed:

- `include/InstrumentTrack.h:407` + `src/tracks/InstrumentTrack.cpp:246-253` - the new
  `InstrumentTrack::destroyInstrument()`: it emits `Instrument::aboutToBeDestroyed()` (the publisher's
  per-slot watch, `src/core/ModulationLayerPublisher.cpp:236-239`, closes the target and waits for the
  in-flight audio write to drain) and only then deletes the instrument. The notification cannot come
  from the instrument's own destructor: the owner is the only one that knows the teardown is starting.
- Every deletion site now goes through that one helper: `src/tracks/InstrumentTrack.cpp:230` (the
  track goes), `:1237` (the file names another instrument - the `instrument` node branch of
  `loadTrackSpecificSettings`), `:1260` (the same loader's legacy plug-in node branch) and `:1357`
  (`loadInstrument` - the user picks another instrument).
- Siblings checked: `Instrument::instantiate()` is called only from those three sites in
  `src/tracks/InstrumentTrack.cpp` (1237/1239, 1260/1262, 1357/1361), and an instrument layer's
  instrument is hosted in a private `InstrumentTrack` (`include/InstrumentLayers.h:60-74`), so every
  instrument in the product is owned by a track and destroyed through `destroyInstrument()`.
  `Instrument::~Instrument()` keeps its late notification as the fallback for an instrument destroyed
  outside a track (the existing `ModulationAudioViewTest::anInstrumentTargetIsRetiredWhenDestroyed`
  covers that path).

Regressions (tests/src/core/ModulationAudioViewTest.cpp):

- `aLoadedInstrumentReplacementClosesTheTargetBeforeTheInstrumentTearsDown` - the track's own load
  path (`InstrumentTrack::loadTrackSpecificSettings`, the `instrument` node branch) replaces the
  instrument; a probe instrument reports from its OWN destructor whether the target was already
  closed when the teardown began.
- `aLegacyInstrumentReplacementClosesTheTargetBeforeTheInstrumentTearsDown` - the same, through the
  loader's legacy plug-in node branch.

Both are deterministic: the probe reads the publisher's atomic token from its own destructor - no
sleep, no timer, no race, no thread.

## Commands (all run in this worktree; exit codes copied from the run)

- RED (the fix reverted with `git apply -R <logs>/fix.patch`, the product diff of
  include/InstrumentTrack.h + src/tracks/InstrumentTrack.cpp):
  `cmake --build build --target zene ModulationAudioViewTest -j12` -> EXIT=0
  (`logs/build-red-1.log`); `cd build/tests && ctest -R '^ModulationAudioViewTest$' --output-on-failure`
  -> EXIT=8, "Totals: 12 passed, 2 failed" (`logs/test-red-1.log`), the two failures being
  `'g_probeGateWasClosed' returned FALSE. (the modulation target was still open when the instrument's
  own teardown began)`.
- GREEN (the fix restored with `git apply <logs>/fix.patch`):
  `cmake --build build --target zene ModulationAudioViewTest -j12` -> EXIT=0 (`logs/build-green-1.log`);
  `cd build/tests && ctest -R '^ModulationAudioViewTest$' --output-on-failure` -> EXIT=0,
  "100% tests passed out of 1" (`logs/test-green-1.log`); the test binary itself:
  `./ModulationAudioViewTest` -> EXIT=0, "Totals: 14 passed, 0 failed" (`logs/test-green-1b.log`).
- Affected family: `cmake --build build --target ModulationLayerValueTest ModulationLayerTest
  ModulationLayerProjectRoundTripTest ModulationRateTest -j12` -> EXIT=0 (`logs/build-family-1.log`);
  `cd build/tests && ctest -R 'Modulation' --output-on-failure` -> EXIT=0, "100% tests passed out of 5"
  (`logs/test-family-modulation-2.log`).
- Instrument family: `ctest -R 'Mpe|InstrumentRouting|InstrumentLayers|InstrumentLayerZone|
  InstrumentLatency'` -> EXIT=8, 9 of 10 passed (`logs/test-family-instrument.log`). The one failure,
  `InstrumentLayersCommandsTest` (SEGFAULT in `lilv_plugin_get_name`, an LV2 plug-in load in this
  sandbox), is NOT caused by this change: with the fix reverted (`git apply -R fix.patch`, rebuilt)
  the same test segfaults the same way (`logs/test-layers-revert.log`, EXIT=8).
- Gates: `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` -> EXIT=0
  (see the table in this round's log).

## Self-check

- The regression test FAILS with the product change reverted and PASSES with it: RED `ctest -R
  '^ModulationAudioViewTest$'` EXIT=8 (12 passed, 2 failed - the two new tests, with the recorded
  assertion 'the modulation target was still open when the instrument's own teardown began'); GREEN
  the same ctest EXIT=0 (14 passed, 0 failed). The revert was `git apply -R` of the product diff only
  (include/InstrumentTrack.h, src/tracks/InstrumentTrack.cpp) - the test file was not touched, so the
  failure is the behaviour and not a build error. The friend declaration the test needs
  (include/InstrumentTrack.h:513) is a compile-time seam committed on its own (commit 4729c4868), so
  the revert leaves the test compiling.
- The test is deterministic: the probe instrument reads the publisher's atomic liveness token from its
  own destructor (a direct, single-threaded read of `m_alive`/`m_targetWriters`); the track's load
  path runs on the test thread, no audio thread is involved, and there is no sleep, timer or race.
- The fix is at the cause and complete: every instrument deletion site in the product goes through
  `InstrumentTrack::destroyInstrument()` (src/tracks/InstrumentTrack.cpp:230, 1237, 1260, 1357);
  `Instrument::instantiate()` is called only from those paths (1239, 1262, 1361); an instrument
  layer's instrument lives in its own private `InstrumentTrack` (include/InstrumentLayers.h:60-74),
  so its teardown is covered too; the publisher's watch arrays are disconnected in both its destructor
  (src/core/ModulationLayerPublisher.cpp:74) and `publishView()` (:201-202).
- No collateral damage: the change moves only WHEN the existing notification is emitted - the
  instrument is still deleted at exactly the same moment, no saved data, project format, control
  command or user-visible behaviour changes. The gate closes only for the slot that names the dying
  instrument (the token is scoped, `retireTarget()`), so no other target is muted.
- Realtime, undo and commands: nothing was added to the audio thread - `destroyInstrument()` and the
  notification run on the control thread, and the audio thread's only change is that it now skips an
  entry whose slot is closed (it already did, through `targetAlive()`). No allocation, no lock, no
  logging was added to the audio path; no control command was added, so the undo stack and the
  command snapshot are untouched. The control thread waits for the in-flight audio write inside
  `retireTarget()` - the same wait the model's own teardown already did.
- If the verdict is ALREADY-FIXED (5186): the code that shows it is
  include/ModulationLayer.h:370-404 (the writer pin) and src/core/ModulationLayerPublisher.cpp:130-159
  (`retireTarget()` closes the slot, waits for the in-flight writers, then clears the token), with the
  generation-scoped teardown that keeps a stale callback from muting a rebound target
  (src/core/ModulationLayerPublisher.cpp:225-241) - both covered by the regressions named above.
