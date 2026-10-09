# M0 blockers - batch B-realtime (audio-thread realtime violations)

Branch `bugs/m0-blk-realtime` (worktree `zene-bunny-midi`). Base: train-2 tip `8eacc41d2`.
Items from `batch-B-realtime.md`; Round 3 corrects the invalid Round 2 red claim for 5313 and closes the reviewed gaps for 5129 and 5467. Red exits describe the old product ordering/behavior when it was deliberately restored. `N/A` means the platform-specific defect could not be red-tested on this Linux host.

## Overall disposition after Round 3

| Cluster | Verdict | Round 3 fix commit | Regression | Round 3 red exit | Green exit | Gate exits (complexity / file-length / fork-sources / no-upstream) |
|---|---|---|---|---|---|---|
| 5415 frozen-take state written without the render lock | FIXED | `437bc1a22` (test-only seam guard; product fix `29d6279ce`) | `FrozenTakeHandoffTest` (freeze and unfreeze) | — (hook guard only) | 0 | 0 / 0 / 0 / 0 |
| 5313 SessionTuning reader spins on a published slot whose claim bit is still set | FIXED | `818834a23` | `SessionTuningPublicationTest::readerReturnsWhileWriterIsPausedAfterPublication` | 8 (old order restored; reader timed out during the post-publication pause) | 0 | 0 / 0 / 0 / 0 |
| 5129 ALSA sequencer never woken for queued output | FIXED | `74fe451db` | `MidiOutQueueTest::processOutEventRequestsTheSequencerWake` | 8 (wake write removed; pipe read returned -1 instead of 1) | 0 | 0 / 0 / 0 / 0 |
| 5467 AudioBusHandle play-handle lock on the audio worker | FIXED | `0396c7624` | `AudioBusPlayHandleQueueTest` (capacity, low-water refill, inline slot, worker subnote) | 8 (growth restored; timer removed; inline slot removed; worker route disabled) | 0 | 0 / 0 / 0 / 0 |
| 4468 SlicerT playhead timer created on the loader thread | FIXED | `unchanged: e54efbc92` | `SlicerTPlayheadTest` through `InstrumentLoaderThread` | 8 (restored constructor-owned timer; GUI view could not find its timer) | 0 | 0 / 0 / 0 / 0 after ledger follow-up in `1ba4a757e`; first post-commit scan returned 1 |
| 4467 SlicerT allocates a PlaybackState on the first play | BLOCKED | - | - | - | - | - |
| 5414 frozen take allocates Sample + SamplePlayHandle per pass | BLOCKED | - | - | - | - | - |
| 5127 metronome click allocates Sample + SamplePlayHandle per click | BLOCKED | - | - | - | - | - |
| 5388 Song automation record: QSet and recordValue on the render thread | BLOCKED | - | - | - | - | - |

## Round 2 verification

| Item | Correction and evidence | Red result | Green result |
|---|---|---|---|
| 5313 | Round 2 placed the seam before publishing the new front slot, so its pause did not cover a reader of the claimed published slot. | Exit 8 was a frequency mismatch, not a reader stall; it did not prove the regression. | Exit 0 with the corrected product order, but the negative assertion was invalid. Round 3 moves the seam after publication. |
| 5129 | Moved `<fcntl.h>` and `<unistd.h>` inside `LMMS_HAVE_ALSA`. `MidiOutQueueTest` was registered without an ALSA dependency. | N/A: no non-POSIX compiler was available. | Exit 0 for queue-helper behavior only. It did not assert that `processOutEvent()` wrote to the wake pipe; Round 3 adds that check. |
| 5467 | Round 2 made note setup and render cleanup list owners and covered the old full-queue wait loop. It did not prevent `QList` growth at capacity or preserve worker-stage subnotes when their queue was full. | Exit 8 with the old yield loop restored covered only the wait-loop regression, not the two gaps identified by review. | Exit 0 for the prior queue case; the Round 2 FIXED verdict was premature. Round 3 adds capacity and worker-stage coverage. |
| 4468 | The poll timer is created and started by `SlicerTView` on the GUI thread. The regression constructs the instrument in `InstrumentLoaderThread`, waits for its move to the GUI thread, then checks the actual instrument view's timer thread and active state. | Exit 8 with the old constructor timer restored; the test could not find a GUI-owned poll timer. Qt also reported timer teardown from another thread. | `SlicerTPlayheadTest` exit 0. |
| 5415 | Replaced the blind pre-completion sleep with a synchronized probe after file work and immediately before the render lock, followed by a condition-variable completion check. | — (probe hardening only; the original product fix was not reversed in this round). | `FrozenTakeHandoffTest` exit 0. The bounded completion wait now starts only after the probe confirms the worker reached the lock boundary. |

The first post-commit no-upstream scan after `e54efbc92` found undeclared changes in `SlicerTView.cpp` and `SlicerTView.h` (exit 1). Their reasons were added to `tests/upstream-modifications.txt` in `1ba4a757e`; all four required gates then passed. Final `python3 tests/rt-safety-sweep.py --check`: exit 0; no allowlist change was needed.

## Round 3 verification

| Item | Round 3 change | Red result | Green result | Gate exits (complexity / file-length / fork-sources / no-upstream) |
|---|---|---|---|---|
| 5415 | Guard the frozen-take handoff probe with `LMMS_TESTING`; the product lock behavior is unchanged. | — (test seam guard only; no product reversal) | `FrozenTakeHandoffTest` exit 0. | 0 / 0 / 0 / 0 |
| 5313 | Move the seam after front-slot publication and compile the hook only in test builds. | Exit 8 with publish-before-clear restored; the reader timed out while the published slot remained claimed. | `SessionTuningPublicationTest` exit 0. | 0 / 0 / 0 / 0 |
| 5129 | Add a test-only pipe constructor and read the wake byte after calling `processOutEvent()`. | Exit 8 when the wake request was removed; non-blocking `read()` returned -1 instead of 1. | `MidiOutQueueTest` exit 0. | 0 / 0 / 0 / 0 |
| 5467 | Reserve bus/engine list capacity on the control thread, refill at low water, keep one inline slot for a bus born in realtime, defer accepted full-list additions, drain per-worker edit buffers, and report overflow through `control.ping`. | Exit 8 when append growth was restored, the low-water timer was removed, the inline slot was removed, and worker-buffer routing was disabled. | `AudioBusPlayHandleQueueTest` exit 0; `ControlRegistryTest` confirms the overflow status fields. | 0 / 0 / 0 / 0 |

## Blocked items - reasons and what was checked

**4467 (SlicerT.cpp, `playNoteImpl`).** The first play of a note creates `new Sample::PlaybackState(...)` (SlicerT.cpp, the `m_pluginData` branch). `PlaybackState`'s constructor builds an `AudioResampler`, whose constructor calls `src_new` (src/core/AudioResampler.cpp:54-55, libsamplerate state allocation), so the allocation is real. The per-note state is owned by the engine's NotePlayHandle plugin-data hook, which runs on the render thread; the plugin cannot create it earlier. Options: (a) engine-level pooled plugin data on NotePlayHandle (an engine change), or (b) a bounded per-instance pool of PlaybackState (about 2 KB of buffer plus resampler state per slot), which silently silences notes above the pool size (an audible behaviour change under heavy polyphony). Neither was taken without a design decision.

**5414 (Track::playFrozenTake, Track.cpp ~1223-1226).** `new Sample(m_frozenBuffer)` and `new SamplePlayHandle(sample)` per pass; the handle's constructor also creates an owned AudioBusHandle, and the engine deletes finished handles on the render thread. Pre-building handles on the control thread needs (1) a disposal path for finished handles that does not delete on the render thread (a PlayHandle retire hook, plus a main-thread refill driver), and (2) a window re-arm on SamplePlayHandle, because the pass start (`sourceIn`) is only known at the audio thread. Not attempted: it is an engine change that needs its own design.

**5127 (MetronomeOptions.cpp `click`, line ~160-162).** Same shape as 5414: `new Sample` and `new SamplePlayHandle(sample, true)` per click, and the engine deletes the handle on the render thread. It can share the disposal path of 5414 once that exists. Not attempted.

**5388 (Song.cpp `processAutomations`, lines ~577, ~648-650).** `QSet<const AutomatableModel*> recordedModels` is inserted into on the render thread, and `p->recordValue(...)` runs there too. `AutomationClip::recordValue` takes `m_clipMutex` (src/core/AutomationClip.cpp:445) and then writes the node. Moving the clip write to the control thread changes when a recorded point appears in the clip (a timing-semantics decision), so it was not done. The set could be made fixed-capacity alone, but that would leave the clip write on the render thread, so the item would not be fixed.

## Caveats

1. **5313 suite isolation.** The round-1 `SessionTuningTest` suite crashes in the unrelated LV2 host when it selects an LV2 instrument (`Lv2Instrument::Lv2Instrument`, `plugins/Lv2Instrument/Lv2Instrument.cpp:84`). `SessionTuningPublicationTest` covers the publication race directly; Round 2's seam produced a value-mismatch red result, while Round 3 moves it after publication and reproduces the reader timeout. This report does not claim the broader suite passes.
2. **5129 platform and integration coverage.** The source and test guards were inspected as described above; an actual non-POSIX build was unavailable. Round 3 calls `processOutEvent()` through a test-only pipe and reads the byte directly, so removing the wake request fails. No ALSA sequencer device was available for an end-to-end run. `processOutEvent()` still makes one non-blocking `write()` to the wake pipe per drain; the pipe is `O_NONBLOCK`, but the syscall has a cost on the audio path.
3. **5467 capacity and overflow.** Track and instrument buses reserve a bounded polyphony batch on the control thread; a standalone bus born in realtime uses one inline handle slot and requests main-thread growth for later additions. The engine list reserves capacity as buses are added. The render owner never grows a `QList`: a full list defers accepted additions to the next period, and a low-water request is serviced by a main-thread timer. Worker-stage edits use per-worker fixed buffers drained after the jobs join. Engine and bus drops, plus worker-buffer overflow, are reported in `control.ping` under `audio.play_handle_overflow`. The allocation probe counts C++ `operator new`; the capacity assertion also catches `QList` growth through Qt's allocator.
4. **4468 loader path.** The round-2 regression uses `InstrumentLoaderThread`, then observes the view created on the GUI thread and checks the running timer belongs to that thread. The test passed with the view-owned timer and failed with the old constructor-owned timer.
5. **5415 test timing.** `FrozenTakeHandoffTest` now waits for a probe after file work and at the lock boundary before checking that the operation remains incomplete while the render lock is held. A bounded condition-variable wait remains as the negative assertion window; it is no longer a blind sleep that can be consumed by file loading before the lock attempt.
6. **Worktree.** The environment note named `zene-m0-train2` as the primary directory. That worktree belongs to another branch (`050/m0-train-2`) and was not touched; all work is in `zene-bunny-midi` on `bugs/m0-blk-realtime`.

## Commits (this branch, oldest first)

- `29d6279ce` fix(freeze): frozen-take state changes wait for the render period (cluster 5415)
- `83d43cd99` fix(tuning): a reader no longer stalls on a table whose claim bit is still set (cluster 5313)
- `cc389e112` fix(midi): the ALSA output thread is woken when audio queues output (cluster 5129)
- `0387dc6d7` fix(audio): play-handle list edits go through a queue, not a lock on the render path (cluster 5467)
- `b8b70371a` fix(slicer): the playhead reaches the GUI from a timer, not a signal emitted on the render thread (cluster 4468)
- `0941a5ef8` docs(m0): batch B-realtime blocker report
- `1f45bc547` fix(tuning): expose claim-release publication boundary (cluster 5313)
- `dce5e1ef0` fix(midi): guard POSIX headers with ALSA support (cluster 5129)
- `b6962e4d8` fix(audio): bypass full edit queue on render-stage owner (cluster 5467)
- `e54efbc92` fix(slicer): move playhead timer to the GUI view (cluster 4468)
- `1ba4a757e` fix(freeze): synchronize handoff test at render lock (cluster 5415)
- `818834a23` fix(tuning): place publication seam after publish (cluster 5313)
- `437bc1a22` fix(freeze): compile lock probe only in test builds (cluster 5415)
- `74fe451db` fix(midi): assert the sequencer wake pipe call (cluster 5129)
- `0396c7624` fix(audio): keep play-handle edits bounded on render paths (cluster 5467)
- this commit: record the Round 3 verification and corrected disposition
