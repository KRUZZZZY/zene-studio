# M0 blockers - batch B-realtime (audio-thread realtime violations)

Branch `bugs/m0-blk-realtime` (worktree `zene-bunny-midi`). Base: train-2 tip `8eacc41d2`.
Items from `batch-B-realtime.md`; every verdict below was re-checked against this tree.

| Cluster | Verdict | Fix commit | Regression test | Red exit | Green exit | Gate exits (pre-commit unless noted) |
|---|---|---|---|---|---|---|
| 5415 frozen-take state written without the render lock | FIXED | `29d6279ce` | `FrozenTakeHandoffTest` (2 cases) | 8 (both cases fail: freeze/unfreeze finished while the render lock was held) | 0 | complexity 0, file-length 0, no-upstream 0; fork-sources 1 pre-commit (the new test is not in HEAD yet, the recipe derives from HEAD), 0 post-commit; all-sources 0 |
| 5313 SessionTuning reader spins on a published slot whose claim bit is still set | FIXED | `83d43cd99` | `SessionTuningTest::aReaderArrivingAtAPublishedTableDoesNotStallOnTheWriter` (test seam `setPublishedHookForTest`) | 8 (the case fails: reader still stalled after the 2 s window) | the case passes (see caveat 2) | complexity 0, file-length 0, no-upstream 0 |
| 5129 ALSA sequencer never woken for queued output | FIXED | `cc389e112` | `MidiOutQueueTest` (`FirstPushAfterAnAcknowledgedDrainRequestsOneWake`, `FullQueue_RequestsNoWake`) | 2 (build fails: `pushAndRequestWake` absent) | 0 | complexity 0, file-length 0, no-upstream 0, fork-sources 0 |
| 5467 AudioBusHandle play-handle lock on the audio worker | FIXED | `0387dc6d7` | `AudioBusPlayHandleQueueTest` (2 cases); `MixerConcurrencyTest` (D6 stress) and render-path tests stay green | 8 (`postingAnEditDoesNotTouchTheListUntilTheBusProcesses` fails on the old code) | 0 (both cases; MixerConcurrencyTest 0) | complexity 0, file-length 0, no-upstream 0; fork-sources 1 pre-commit, 0 post-commit; all-sources 0 |
| 4468 SlicerT emits isPlaying from the render thread | FIXED | `b8b70371a` | `SlicerTPlayheadTest` | 8 (render thread emitted: count != 0) | 0 (also checks the GUI timer delivers the playhead) | complexity 0, file-length 0, no-upstream 0; fork-sources 0 and all-sources 0 post-commit |
| 4467 SlicerT allocates a PlaybackState on the first play | BLOCKED | - | - | - | - | - |
| 5414 frozen take allocates Sample + SamplePlayHandle per pass | BLOCKED | - | - | - | - | - |
| 5127 metronome click allocates Sample + SamplePlayHandle per click | BLOCKED | - | - | - | - | - |
| 5388 Song automation record: QSet and recordValue on the render thread | BLOCKED | - | - | - | - | - |

rt-safety sweep (`python3 tests/rt-safety-sweep.py --check`) on the final tree: exit 0, no allowlist change needed (none of the touched files is a declared scope symbol).

## Blocked items - reasons and what was checked

**4467 (SlicerT.cpp, `playNoteImpl`).** The first play of a note creates `new Sample::PlaybackState(...)` (SlicerT.cpp, the `m_pluginData` branch). `PlaybackState`'s constructor builds an `AudioResampler`, whose constructor calls `src_new` (src/core/AudioResampler.cpp:54-55, libsamplerate state allocation), so the allocation is real. The per-note state is owned by the engine's NotePlayHandle plugin-data hook, which runs on the render thread; the plugin cannot create it earlier. Options: (a) engine-level pooled plugin data on NotePlayHandle (an engine change), or (b) a bounded per-instance pool of PlaybackState (about 2 KB of buffer plus resampler state per slot), which silently silences notes above the pool size (an audible behaviour change under heavy polyphony). Neither was taken without a design decision.

**5414 (Track::playFrozenTake, Track.cpp ~1223-1226).** `new Sample(m_frozenBuffer)` and `new SamplePlayHandle(sample)` per pass; the handle's constructor also creates an owned AudioBusHandle, and the engine deletes finished handles on the render thread. Pre-building handles on the control thread needs (1) a disposal path for finished handles that does not delete on the render thread (a PlayHandle retire hook, plus a main-thread refill driver), and (2) a window re-arm on SamplePlayHandle, because the pass start (`sourceIn`) is only known at the audio thread. Not attempted: it is an engine change that needs its own design.

**5127 (MetronomeOptions.cpp `click`, line ~160-162).** Same shape as 5414: `new Sample` and `new SamplePlayHandle(sample, true)` per click, and the engine deletes the handle on the render thread. It can share the disposal path of 5414 once that exists. Not attempted.

**5388 (Song.cpp `processAutomations`, lines ~577, ~648-650).** `QSet<const AutomatableModel*> recordedModels` is inserted into on the render thread, and `p->recordValue(...)` runs there too. `AutomationClip::recordValue` takes `m_clipMutex` (src/core/AutomationClip.cpp:445) and then writes the node. Moving the clip write to the control thread changes when a recorded point appears in the clip (a timing-semantics decision), so it was not done. The set could be made fixed-capacity alone, but that would leave the clip write on the render thread, so the item would not be fixed.

## Caveats

1. **5467 red.** The lock is removed by construction, so no deterministic lock-contention red exists through the public API. The red is the white-box contract case: on the old code the posting thread writes the list directly. The queue's full-queue behaviour is a yield loop (bounded at `PlayHandle::MaxNumber` edits per bus per period); the comment in code says why it cannot be reached while the engine renders.
2. **5313 green.** The SessionTuningTest suite's green run (before `libslicert.so` existed in this build directory) ran every case to the end except `loadScaleWhileTransportPlaysRetunesSoundingNotes`, which failed because no instrument plug-in was loadable (`plugin-scan: 0 plugin(s)`). The new case passed in that run. **Since the SlicerT module was built here, the suite crashes earlier, inside `loadScaleWhileTransportPlaysRetunesSoundingNotes`:** `revtest::addInstrumentTrack()` now picks an LV2 instrument and `Lv2Instrument::Lv2Instrument` dereferences a null key (plugins/Lv2Instrument/Lv2Instrument.cpp:84). None of the frames is in changed code. It is pre-existing in the LV2 host and was not checked at HEAD; the 5313 case therefore has not been re-run since the crash.
3. **5129 realtime.** `processOutEvent` now issues one non-blocking `write()` to the wake pipe per drain (not per event). That is a syscall on the audio path. It cannot block (the write end is `O_NONBLOCK`), but a syscall is not zero-cost. The ALSA path itself was not exercised end to end (no sequencer device in this run).
4. **Build tree state.** `build/plugins` had no plugin shared objects before this batch; only `libslicert.so` was built here, for 4468. The other instrument plug-ins were not built, so the instrument-dependent cases of the suites were not exercised beyond what the caveats above say.
5. **Worktree.** The environment note named `zene-m0-train2` as the primary directory. That worktree belongs to another branch (`050/m0-train-2`) and was not touched; all work is in `zene-bunny-midi` on `bugs/m0-blk-realtime`.

## Commits (this branch, oldest first)

- `29d6279ce` fix(freeze): frozen-take state changes wait for the render period (cluster 5415)
- `83d43cd99` fix(tuning): a reader no longer stalls on a table whose claim bit is still set (cluster 5313)
- `cc389e112` fix(midi): the ALSA output thread is woken when audio queues output (cluster 5129)
- `0387dc6d7` fix(audio): play-handle list edits go through a queue, not a lock on the render path (cluster 5467)
- `b8b70371a` fix(slicer): the playhead reaches the GUI from a timer, not a signal emitted on the render thread (cluster 4468)
- this commit: docs(m0): batch B-realtime blocker report
