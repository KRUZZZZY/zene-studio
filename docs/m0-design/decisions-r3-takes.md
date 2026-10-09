# Active take selection design — decisions-r3-takes

Base: `888934e1b` (`bugs/m0-decisions-r3-takes`). Owner decision: 2026-10-09.

## Problem

`SampleClip::addRecordedLane` (`src/core/SampleClip.cpp:313-340`) creates one lane clip for each loop pass, then mutes only `m_lastPassTake` when that prior pass is still unmuted. `m_lastPassTake` is transient arming state (`include/SampleClip.h:250-255`), so audibility is encoded by clip mute flags instead of a remembered take choice. The track scheduler (`src/tracks/SampleTrack.cpp:243-246`) reads those flags, while `SamplePlayHandle::snapshotComp` (`src/core/SamplePlayHandle.cpp:286-320`) snapshots only composite/audition spans. This means the engine has no active-take state, no remembered user choice, and no play-all switch. `TakeLaneModel` already belongs to `Track` and is saved in both track checkpoints and the project's `<z:lanes>` section (`include/TakeLane.h:144-153`, `src/core/Track.cpp:556-560`, `src/core/Song.cpp:1952-1986`); `docs/COMPING.md` currently says clip mute is take audibility. `TakeLanesPanel` exposes lane audition and comp selection only (`src/gui/TakeLanesPanel.cpp:89-168, 242-274`).

## Chosen design

Each armed source clip's loop passes form one take group, identified by that source clip's stable clip ID. Each recorded pass keeps its existing lane index and gets the group ID on its clip; a pass identity is its lane, so split pieces on that lane stay one take. `TakeLaneModel` owns a main-thread vector of `{groupId, activeLane, userChosen}` records. On the first pass the active lane is the new lane. Each later pass becomes active while `userChosen` is false; selecting a lane sets `userChosen`, and recording later passes preserves the selected lane. The model publishes the chosen lane to each member clip's `std::atomic<int>` runtime mirror. The mirror is derived state: its source of truth is the model's serialized group record.

The model also owns an atomic track-wide `playAllTakes` flag. `comp.take_select` selects the take lane of a group member clip. `comp.play_all_takes` sets the track-wide switch. Both are true-inverse commands using one Track checkpoint, which already contains the take-lane model and clip attributes. The Take Lanes panel adds a take selector and a “Play all takes” checkbox, driven by `comp.get_state` and those commands.

For an un-comped track, `SampleTrack::play` checks each grouped clip's atomic active lane before creating a play handle when play-all is off. `SamplePlayHandle` also checks that same atomic at render time, so an already-running handle follows a take change; if play-all is on, every unmuted grouped clip renders and its frames sum through the existing track bus. Both reads are lock-free. If a composite exists, its span gate remains authoritative: the active-take and play-all gates are bypassed for comped playback, and the existing `SamplePlayHandle::applyComp` spans still decide what sounds.

The take group ID is written with the clip's other non-default take metadata. The active lane and `userChosen` flag are written in the take-lane model's `<takelanes>` checkpoint payload and in the existing top-level `<z:lanes>` project section. Loading a model resets absent values to the default (`playAllTakes = false`, no groups) and republishes group choices to clips after all clips and lanes are loaded. Undo/redo therefore restores both the model state and its render mirror through the same Track checkpoint.

## Invariants and ownership

- `Track` owns `TakeLaneModel`; its group vector and serialized values are written only by the main/control thread under the engine's existing model-change guard. Group count has no fixed cap; storage grows on the main thread with recorded groups. No group-vector traversal occurs on the audio thread.
- `Clip` owns its stable take-group ID and lock-free active-lane mirror. The model publishes the mirror after group creation, take selection, model load, and checkpoint restore. The ID is stable while the clip is attached; a split preserves the lane/pass identity. The mirror is read with a relaxed atomic load and written on the main thread while model changes are guarded.
- `TakeLaneModel` owns `playAllTakes`; it is an atomic boolean with no buffer or pool. Its value is changed and serialized on the main thread, and read lock-free by the render path. It defaults off.
- No audio-thread allocation, lock, wait, I/O, signal emission, map/vector lookup, or shared-pointer refcount operation is introduced. A handle uses only the clip's lane/group scalar values and atomic state loads; the per-frame gate is bounded by the current buffer length. The group record is never looked up from `SamplePlayHandle::play`.
- There is no fixed group/take capacity and therefore no bounded-pool overflow case. Track checkpoint or project serialization remains proportional to the already-existing number of clips, lanes, and groups and allocates only off the audio thread.
- A manual take choice stays selected across later passes until another manual choice. A newly recorded pass still receives a lane and sample buffer even when it does not become active.
- The composite continues to mask active-take and play-all state. Clip mute remains an explicit mute; recorded passes are not muted as a side effect of recording. Play-all sums every unmuted pass in each group.
- The previously committed record accumulator, prepared/spare take handoff, drain/reaper, overflow accounting, and bounded audio-thread record queues are unchanged.

## Behaviour and alternatives

This intentionally changes only loop-take audibility: with no manual choice, the newest pass is audible; a manual choice survives additional passes; play-all sums passes. First-pass non-loop recording, recording timing/length, take-lane placement, clip mute, composite playback, and comp undo remain unchanged. Selection, user-choice state, and play-all survive project save/reopen and are undoable. No Qt-6-only API is needed.

Rejected: continue using mute flags, because a later pass can overwrite user mute choices and mute is not a stable selection model; store one active lane for the entire track, because independently armed source clips need independent groups; derive the active take by scanning clips in `SamplePlayHandle::play`, because that would put an unbounded container lookup on the audio thread; and snapshot selection only when constructing a play handle, because a manual choice would not affect already-running audio until another handle starts. A fixed take/group pool was also rejected: no realtime path needs to traverse or allocate the persistent group vector, so a new capacity and overflow policy would add a limit without protecting the audio thread.

## Proof plan

Add recording assertions that N passes select the newest lane, and that a user-selected lane and flag survive later passes. Add render comparisons for one-active-take output and play-all summed output, plus an AllocationProbe/RealtimeScope assertion around rendering. Exercise save/reopen, user-choice persistence, and one-step undo/redo of `comp.take_select`. Run the recording suites, the active-take render and control tests, the commands snapshot/canary/A16 histogram refresh, and the requested gates and rt-safety sweep.
