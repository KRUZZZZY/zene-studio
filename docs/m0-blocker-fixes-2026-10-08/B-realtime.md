# M0 blockers - batch B-realtime (audio-thread realtime violations)

Branch `bugs/m0-blk-realtime` (worktree `zene-bunny-midi`). Base: train-2 tip `8eacc41d2`.
Items from `batch-B-realtime.md`; the table reflects the corrected round-2 findings. Red exits describe the old product ordering/behavior when it was deliberately restored. `N/A` means the platform-specific defect could not be red-tested on this Linux host.

## Overall disposition

| Cluster | Verdict | Round 2 fix commit(s) | Regression | Round 2 red exit | Green exit | Gate exits (complexity / file-length / fork-sources / no-upstream) |
|---|---|---|---|---|---|---|
| 5415 frozen-take state written without the render lock | FIXED | `1ba4a757e` (synchronized test; product fix `29d6279ce`) | `FrozenTakeHandoffTest` (freeze and unfreeze) | — (test hardening only) | 0 | 0 / 0 / 0 / 0 |
| 5313 SessionTuning reader spins on a published slot whose claim bit is still set | FIXED | `1f45bc547` | `SessionTuningPublicationTest` | 8 (restored old publication order; reader failed to complete during the claim-release window) | 0 | 0 / 0 / 0 / 0 |
| 5129 ALSA sequencer never woken for queued output | FIXED | `dce5e1ef0` (guard; behavior change `cc389e112`) | `MidiOutQueueTest` queue-helper cases; non-ALSA guards audited in source | N/A (no non-POSIX compiler available) | 0 | 0 / 0 / 0 / 0 |
| 5467 AudioBusHandle play-handle lock on the audio worker | FIXED | `b6962e4d8` (full-queue render-stage handling; initial queue change `0387dc6d7`) | `AudioBusPlayHandleQueueTest::audioThreadAddsDuringOwnedRenderStageWhenQueueIsFull` | 8 (restored old yield loop; producer failed to return before drain) | 0 | 0 / 0 / 0 / 0 |
| 4468 SlicerT playhead timer created on the loader thread | FIXED | `e54efbc92` | `SlicerTPlayheadTest` through `InstrumentLoaderThread` | 8 (restored constructor-owned timer; GUI view could not find its timer) | 0 | 0 / 0 / 0 / 0 after ledger follow-up in `1ba4a757e`; first post-commit scan returned 1 |
| 4467 SlicerT allocates a PlaybackState on the first play | BLOCKED | - | - | - | - | - |
| 5414 frozen take allocates Sample + SamplePlayHandle per pass | BLOCKED | - | - | - | - | - |
| 5127 metronome click allocates Sample + SamplePlayHandle per click | BLOCKED | - | - | - | - | - |
| 5388 Song automation record: QSet and recordValue on the render thread | BLOCKED | - | - | - | - | - |

## Round 2 verification

| Item | Correction and evidence | Red result | Green result |
|---|---|---|---|
| 5313 | Moved the test seam between clearing `WriterBit` and publishing the new front slot; the isolated test pauses the writer in that exact window. | Exit 8 with the old publish-before-clear ordering; the reader timed out while the writer was paused. | Exit 0 with the corrected ordering. |
| 5129 | Moved `<fcntl.h>` and `<unistd.h>` inside `LMMS_HAVE_ALSA`. Read the build/test guards: `MidiAlsaSeq.cpp` is always in `src/core/CMakeLists.txt`; ALSA declarations in `MidiAlsaSeq.h` are guarded; `MidiOutQueueTest.cpp` has no ALSA dependency and its registration in `tests/CMakeLists.txt` is unconditional. | N/A: this include failure is non-POSIX-specific, and no Windows compiler was available. | `MidiOutQueueTest` exit 0. The test covers the queue helper; it does not independently assert the `processOutEvent()`-to-wake-pipe call wiring, and the ALSA device path was not exercised. |
| 5467 | Note setup and render cleanup now own the list during their render stages, so realtime producers append directly there. Full-queue realtime calls outside that owner return and increment the refusal counter; control-thread overflow drains prior edits under the change lock before applying the new edit. The normal played-note setup path bypasses the queue. | Exit 8 with the old yield loop restored: the full-queue audio-thread producer did not return before consumer drain. | `AudioBusPlayHandleQueueTest` exit 0, including the saturated queue on an audio-thread producer. |
| 4468 | The poll timer is created and started by `SlicerTView` on the GUI thread. The regression constructs the instrument in `InstrumentLoaderThread`, waits for its move to the GUI thread, then checks the actual instrument view's timer thread and active state. | Exit 8 with the old constructor timer restored; the test could not find a GUI-owned poll timer. Qt also reported timer teardown from another thread. | `SlicerTPlayheadTest` exit 0. |
| 5415 | Replaced the blind pre-completion sleep with a synchronized probe after file work and immediately before the render lock, followed by a condition-variable completion check. | — (probe hardening only; the original product fix was not reversed in this round). | `FrozenTakeHandoffTest` exit 0. The bounded completion wait now starts only after the probe confirms the worker reached the lock boundary. |

The first post-commit no-upstream scan after `e54efbc92` found undeclared changes in `SlicerTView.cpp` and `SlicerTView.h` (exit 1). Their reasons were added to `tests/upstream-modifications.txt` in `1ba4a757e`; all four required gates then passed. Final `python3 tests/rt-safety-sweep.py --check`: exit 0; no allowlist change was needed.

## Blocked items - reasons and what was checked

**4467 (SlicerT.cpp, `playNoteImpl`).** The first play of a note creates `new Sample::PlaybackState(...)` (SlicerT.cpp, the `m_pluginData` branch). `PlaybackState`'s constructor builds an `AudioResampler`, whose constructor calls `src_new` (src/core/AudioResampler.cpp:54-55, libsamplerate state allocation), so the allocation is real. The per-note state is owned by the engine's NotePlayHandle plugin-data hook, which runs on the render thread; the plugin cannot create it earlier. Options: (a) engine-level pooled plugin data on NotePlayHandle (an engine change), or (b) a bounded per-instance pool of PlaybackState (about 2 KB of buffer plus resampler state per slot), which silently silences notes above the pool size (an audible behaviour change under heavy polyphony). Neither was taken without a design decision.

**5414 (Track::playFrozenTake, Track.cpp ~1223-1226).** `new Sample(m_frozenBuffer)` and `new SamplePlayHandle(sample)` per pass; the handle's constructor also creates an owned AudioBusHandle, and the engine deletes finished handles on the render thread. Pre-building handles on the control thread needs (1) a disposal path for finished handles that does not delete on the render thread (a PlayHandle retire hook, plus a main-thread refill driver), and (2) a window re-arm on SamplePlayHandle, because the pass start (`sourceIn`) is only known at the audio thread. Not attempted: it is an engine change that needs its own design.

**5127 (MetronomeOptions.cpp `click`, line ~160-162).** Same shape as 5414: `new Sample` and `new SamplePlayHandle(sample, true)` per click, and the engine deletes the handle on the render thread. It can share the disposal path of 5414 once that exists. Not attempted.

**5388 (Song.cpp `processAutomations`, lines ~577, ~648-650).** `QSet<const AutomatableModel*> recordedModels` is inserted into on the render thread, and `p->recordValue(...)` runs there too. `AutomationClip::recordValue` takes `m_clipMutex` (src/core/AutomationClip.cpp:445) and then writes the node. Moving the clip write to the control thread changes when a recorded point appears in the clip (a timing-semantics decision), so it was not done. The set could be made fixed-capacity alone, but that would leave the clip write on the render thread, so the item would not be fixed.

## Caveats

1. **5313 suite isolation.** The round-1 `SessionTuningTest` suite crashes in the unrelated LV2 host when it selects an LV2 instrument (`Lv2Instrument::Lv2Instrument`, `plugins/Lv2Instrument/Lv2Instrument.cpp:84`). Round 2 adds `SessionTuningPublicationTest` and verifies that case directly; this report does not claim the broader suite passes.
2. **5129 platform and integration coverage.** The source and test guards were inspected as described above; an actual non-POSIX build was unavailable. `MidiOutQueueTest` exercises the queue helper but does not independently catch a regression that removes its call from `processOutEvent()`. No ALSA sequencer device was available for an end-to-end run. `processOutEvent()` still makes one non-blocking `write()` to the wake pipe per drain; the pipe is `O_NONBLOCK`, but the syscall has a cost on the audio path.
3. **5467 full queue.** Round 1's claim that a full queue could not occur during rendering was incorrect. Audio-thread note setup can enqueue before the consumer stage. Round 2 marks note setup and cleanup as render-stage owners, where list edits append directly and do not touch the queue. A realtime producer outside an owning stage gets a counted, non-blocking refusal; normal played-note setup uses the owner path so queue saturation does not silence those notes.
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
- this commit: correct round-2 B-realtime findings and verification table
