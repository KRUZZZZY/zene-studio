# ArrangingCatchUpTest audio-worker crash — root cause and fix (parent, 2026-10-05)

## Symptom
Intermittent SIGSEGV on an audio worker in `InstrumentPlayHandle::play` (a call through address 0x150)
during `ArrangingCatchUpTest::newDocumentsCarryTheirTypesTemplate`; Luna reproduced it within ~7 s of
`--repeat until-fail`; here 2 of 5 plain runs crashed.

## Root cause
LB302 queues its own `InstrumentPlayHandle` when it is constructed (`plugins/Lb302/Lb302.cpp:245`) and has
no destructor cleanup; the handle is removed only by `InstrumentTrack::~InstrumentTrack` ->
`silenceAllNotes(true)` -> `AudioEngine::removePlayHandlesOfTypes` (`src/tracks/InstrumentTrack.cpp:642-644`).
That sweep deleted matching handles from the active list but, in the staging list (`m_newPlayHandles`,
merged only at the next period), only NotePlayHandles. A document created and replaced inside one period
(the "live" recipe's LB302 Bass, then `project.new_document`) left the staged handle behind; the next period
merged it and played it through the deleted instrument. The same exclusion left a staged InputMonitorHandle
alive after monitoring was switched off.

The exclusion existed because `InstrumentPlayHandle::isFromTrack` dereferenced its instrument, and
`PluginPortsMigrationTest` deletes instruments before their tracks: sweeping staged instrument handles then
read a freed instrument (recorded at the time as a double free).

## Fix
- `InstrumentPlayHandle` records the track it was created for and `isFromTrack` compares that pointer, the
  same comparison `Instrument::isFromTrack` makes (`src/core/Instrument.cpp:112-115`), without the dereference.
- The staged sweep removes every requested type, as the active sweep does (same ownership: notes back to the
  manager, anything else deleted once).

The header gains a member, so every module that constructs an `InstrumentPlayHandle` must be rebuilt with it
(carlabase, clapinstrument, gigplayer, lb302, lv2instrument, opulenz, sf2player, vestige, vst3instrument,
zynaddsubfx). A run with a stale `lb302` module corrupts the heap (`trace-stale-lb302-module-heap-abort.log.md`):
that, not the fix, was what made the takeover lane's runs "hang".

## Evidence
- Red: `red-baseline-ArrangingCatchUpTest.log.md`: `queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie`
  (beforeRender 0, expected 1) and `switchingMonitoringOffRemovesAHandleQueuedInTheSamePeriod` fail.
- ASan (RelWithDebInfo, `-fsanitize=address`): baseline shows no memory error in 6 runs (timing hides the
  race) and the two regression failures; the sweep change alone passes all (`asan-*-run1.log.md`).
- Green, every module rebuilt: `ctest -R '^ArrangingCatchUpTest$' --repeat until-fail:30` EXIT=0 (30/30);
  AudioBusHandleTest, AudioEngineTeardownTest, InputMonitorTest, PdcMixerTest, SampleClipWarpTest,
  SampleClipWindowTest, PluginPortsMigrationTest, SessionTuningTest, Vst3InstrumentIntegrationTest EXIT=0.
- Gates: `complexity-gate.sh --check` EXIT=0, `file-length-gate.sh --check` EXIT=0.
- Realtime: no audio-thread code changed; the sweep runs under `requestChangeInModel` on the control thread.
