# Voice and handle pools for render-path sample playback

Date: 2026-10-09  
Lane: `voice-pool`  
Base: `c405233f8`

## Problems confirmed

**5127 — metronome clicks.** `src/core/Metronome.cpp:31-40` calls `metronomeoptions::click()` while scheduling a render period. `src/core/MetronomeOptions.cpp:148-164` selects an already-decoded buffer in the usual case, but still allocates a `Sample` and `SamplePlayHandle` for every click; if options have not loaded it calls `SampleBuffer::fromFile()` there as well. `SamplePlayHandle` also creates its playback resampler and owned `AudioBusHandle` in its constructor. The settings-registration path normally calls `options()` first (`ProjectSectionRegistry.cpp:70`), but that does not remove the per-click allocations or make the fallback safe.

**5414 — frozen takes.** `Track::playFrozenTake()` (`src/core/Track.cpp:1195-1248`) is reached from the arrangement paths in `SampleTrack::play()` and `InstrumentTrack::play()` (`src/tracks/SampleTrack.cpp:129-138`, `src/tracks/InstrumentTrack.cpp:823`). Once per pass it allocates a `Sample` and `SamplePlayHandle`; the handle constructs a `Sample::PlaybackState`/resampler and an owned bus. The pass's `sourceIn` is calculated from the current transport position at the point the render thread detects a pass start, so it cannot be baked into a handle when the take is frozen. Re-arming must keep separate state for concurrent passes and retain the buffer until every voice has stopped using it.

**4467 — SlicerT note state.** `plugins/SlicerT/SlicerT.cpp:78-116` allocates `Sample::PlaybackState` the first time `playNoteImpl()` sees a note. `NotePlayHandle::play()` invokes this plug-in hook while rendering; the state constructor creates an `AudioResampler`, whose `src_new()` call is in `src/core/AudioResampler.cpp:48-56`. The hook has no earlier plug-in initialization phase. `deleteNotePluginData()` at `SlicerT.cpp:140-144` currently frees that state when the note ends.

## Chosen design

Use one fixed-capacity `SamplePlayHandle` voice pool owned by `AudioEngine`, plus a fixed per-instance playback-state pool in SlicerT. The pool is deliberately for the metronome and frozen-take handles only; ordinary clip handles retain their existing construction and lifetime.

The engine voice pool has a hard maximum of **512** stable handle slots: **32 reserved for metronome clicks** and up to **480 for frozen takes**. Each slot contains a `Sample`, a `Sample::PlaybackState`, and a `SamplePlayHandle`; all slots use one engine-owned, no-effects `AudioBusHandle`. This matches the old bus's linear summing order while avoiding hundreds of idle bus jobs. The shared bus reserves space for all 512 handles on the control thread. Construct and grow slots only on the engine/control thread. The main-thread capacity timer services the pool; the audio thread only claims an already-ready slot. The 512-slot limit is shared across all tracks, and each frozen track may have at most **8 active passes**. A metronome or frozen pass that cannot claim a slot is skipped, counted, and exposed by `control.ping` (`audio.sample_voice_overflow`); the metronome's 32 slots cannot be consumed by frozen takes.

Use a fixed SPSC ready queue from the main thread to the render thread and a fixed SPSC retire queue in the reverse direction, each sized for all 512 possible slots. The render side is the sole producer of retirement: finished pooled handles are detached from the engine and bus lists, then enqueued without destruction. The existing main-thread timer drains retired handles, resets their sample/resampler state, releases any old shared sample reference, and makes each slot ready again. Cancellation/removal while the engine change guard is held happens on the control side and returns the slot there. Queue occupancy cannot exceed the number of pool slots: a slot is in exactly one of ready, active/staged, or retired states. Queue-full is an invariant failure counted in `control.ping`, never a render-thread delete fallback.

Each pooled handle is constructed on the control thread with its slot's empty sample and the shared bus. Arming it on the render thread only copies a `shared_ptr` to the source buffer, sets POD window/amplification/offset fields, associates a track for normal removal, and starts from an already-reset playback state. Before a slot re-enters the ready queue, the control thread resets the resampler, clears its buffer view and sample reference, and clears its track association. The track retains its frozen buffer while frozen; if it is cleared while a voice is active, guarded handle removal stops that voice before the track releases its reference. The buffer copy on arm therefore cannot be the last reference or free audio storage on the render thread.

For frozen takes, replace the single in-flight flag with render-thread pass-start detection and an active-pass count. A pass starts on the first render call inside the frozen window, after a stop/seek/mute reset, or after transport wrap/re-entry while an older voice is still active. Each detected pass claims its own voice and snapshots `sourceIn` from that call's `TimePos`; existing voices continue to render to their own source end. The eight-per-track and 480-global frozen limits bound overlap; an excess pass is skipped and included in the same visible overflow counter. Keep the current mute, window, solo, and one-start-per-pass rules.

Each SlicerT instance owns **128** `Sample::PlaybackState` slots created with the instrument on its loader/control thread. A note claims one with a bounded scan and stores the stable slot address in `NotePlayHandle::m_pluginData`. Release resets the state's resampler/frame index/buffer view and marks the slot free; neither claiming nor releasing creates or destroys state on the audio thread. At 128 occupied slots, that note produces silence and increments a global `audio.slicert_state_overflow` counter in `control.ping`. This is the defined polyphony ceiling for this instrument instance; states are never shared between live notes.

## Invariants and realtime argument

| Object / operation | Owner and thread | Allocation and release | Capacity behaviour |
|---|---|---|---|
| Pooled sample handle, sample, resampler, shared bus | `AudioEngine` pool; constructed/refilled/reset/destroyed on engine/control thread | At most 512 handles and one bus, each off render; pooled handles are not deleted when a render finishes | No ready slot: skip one click/pass, increment `audio.sample_voice_overflow`; reserved metronome slots remain available |
| Ready/retire queue entries | Fixed arrays owned by `AudioEngine` | Allocated with engine; render enqueue is a bounded SPSC index/value store; main timer dequeues and resets | Total queued slots cannot exceed 512; a violation increments the queue-invariant counter and leaves the handle owned, never deletes it on render |
| Frozen take buffer | `Track` owns the frozen reference; active voice holds a shared reference | Loaded/frozen/cleared on control thread; pooled handle reference is cleared by main-thread retirement | Clearing a take first removes its associated active handles under the engine change guard |
| SlicerT playback states | One instrument instance owns 128 stable slots | Constructed with SlicerT off render; retained for the instance lifetime; resampler reset on slot release | Exhausted instance renders the new note as silence and increments `audio.slicert_state_overflow` |

The `Metronome::processTick()` and `Track::playFrozenTake()` render paths perform bounded queue operations, pointer checks, shared-reference increments whose backing source remains owned, atomic/POD field updates, and the existing engine handle enqueue. They do not call `new`, `delete`, `fromFile`, a Qt signal, a mutex, or a container growth path. Completion only enqueues a stable pointer; freeing, resampler reset, and shared-reference release happen in the main-thread pool service. SlicerT's play hook scans at most 128 slots and uses a preconstructed state; its release path resets and marks a slot without freeing memory. These claims are to be proved with `AllocationProbe` and the real-time-safety gate when implemented.

At capacities, the only audible difference is a counted skipped click/pass or a counted silent SlicerT note. Below capacity, sample choice, accent, gain, offset, source window, per-note resampler history, overlap, mute/solo and transport rules remain unchanged. Fresh and re-armed voice rendering must compare bit-identically; if `src_reset()` produces a different first block from a newly constructed state, the rearm design must be corrected before merge.

## Considered and rejected

- **Keep `new`/`delete` but warm the allocator:** does not remove render-path allocation or destruction and gives no capacity bound.
- **Load a metronome file as a fallback on first click:** keeps file I/O reachable from rendering; registration already loads all built-in buffers on the control thread.
- **One shared `PlaybackState` for SlicerT:** corrupts frame position, buffer view and resampler history when notes overlap.
- **Unbounded pools or growing vectors when full:** moves unbounded memory growth to the control path and gives no defined overload behaviour; pool growth is capped at 512 handles / 128 states per SlicerT instance.
- **Delete finished handles immediately on render:** destroys the bus/sample/resampler and may free the source buffer on the audio thread. Retirement keeps all reclamation on the control thread.
- **Reuse a frozen handle without resetting its window/state:** carries the old pass's resampler history and `sourceIn` into the next pass, so a mid-take start and overlapping passes render incorrectly.

## Verification required for implementation

Add allocation-probe regressions for click scheduling, frozen-pass scheduling and completion/retirement, and SlicerT first-note/release at the existing test seam. Each regression must fail with the original allocating path restored and pass with the pool. Add bounded-capacity/overflow and overlapping-frozen-pass cases; verify the counters reach `control.ping`. Compare rendered output against the pre-change path bit-for-bit for clicks, frozen takes at nonzero `sourceIn`, and concurrent passes, and compare SlicerT output across note reuse. Run targeted tests and the required real-time-safety scope gate without adding allowances.
