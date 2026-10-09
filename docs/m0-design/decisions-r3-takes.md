# Loop recording active-take design

## Problem

`SampleClip::addRecordedLane()` in `src/core/SampleClip.cpp` creates a take clip for each loop
pass, then auto-mutes the immediately previous take through `m_lastPassTake`. That encodes
audibility in mutable clip mute flags, cannot remember an explicit choice when another pass is
recorded, and has no play-all mode. The owner ruling for 2026-10-09 replaces that behavior with an
active-take model: newest pass is active by default, a deliberate choice survives later passes,
and the user can choose to play every pass. Existing comp playback remains authoritative.

## Chosen design

Each recorded take clip carries a persistent `takeGroupId`, the stable id of the source clip that
was armed. `TakeLaneModel` owns a main-thread vector of `{groupId, activeLane, userChosen}` records
and an atomic track-wide Play all takes flag. On pass finalization, `recordTake()` selects the new
lane only when no user choice has been recorded. `selectTake()` sets both the lane and the user
choice flag. The clip stores an atomic mirror of its group's active lane. The model republishes
that mirror when a pass is added or a choice is made, and after project or checkpoint loading.

`SampleTrack::play()` filters inactive pass clips before starting handles. A handle also reads the
clip mirror and Play all takes flag at render time, so a handle already running observes selection
changes. When a composite exists, its existing span gate continues to decide which takes play;
active-take and Play all takes selection do not override the composite. Play all takes bypasses
active-lane selection and allows every unmuted grouped take to sum through the existing track bus.

`comp.take_select` and `comp.play_all_takes` are reversible commands backed by the existing Track
checkpoint. The take group, active lane, user-choice flag, and Play all takes flag are saved in the
existing lane section; each clip saves its group id. The Take Lanes panel exposes both controls.

## Invariants and ownership

- `Track` owns its clips and `TakeLaneModel`. Each take clip owns its immutable sample buffer,
  take-group id, lane index, and atomic active-lane mirror. The model owns the dynamic group vector
  and the Play all takes atomic.
- `SampleRecordHandle` hands completed passes to `SampleClip::acceptRecordedTake()` on the main
  thread. Lane creation, group-vector growth, project serialization, and destruction happen on
  the main thread under the existing audio change guard. Render handles retain no pointer into
  the group vector; their scalar-state pointers are valid for the clip/track lifetime protected by
  that same guard.
- There is no fixed group or take pool and no bounded group count. Group storage grows with the
  number of recorded source clips on the main thread. No overflow counter is needed for a bound
  that does not exist; inability to allocate follows the existing main-thread allocation failure
  behavior. Existing bounded render buffers and clip-start limits are unchanged.
- Active lane and Play all takes are read and written through atomic scalar mirrors. Render code
  does not search the model, allocate, lock, wait, perform I/O, emit Qt signals, or update
  reference-counted pointers. The active-take gate only compares scalars and, when inactive,
  zeros the current fixed render block in time proportional to its bounded frame count.
- Project load clears absent group state to defaults, loads clip tags and model state, then
  republishes active lanes. Checkpoint restore uses the same Track serialization and publication
  path, so undo/redo restores the user-choice bit as well as the audible lane.
- Comped playback retains the existing 64-frame span ramp and resolution rules. Ordinary clips,
  non-recorded clips, explicit mute state, and the loop recording buffer-preparation lifecycle
  keep their existing behavior.

## Alternatives considered

- **Keep muting the previous pass:** rejected because it stores selection as per-clip mute edits,
  cannot preserve a selected take through later passes, and makes Play all takes an incidental
  unmute operation.
- **Search the track's group vector while rendering:** rejected because it adds model lookup and
  synchronization to an audio-thread path. Per-clip atomic mirrors keep rendering independent of
  dynamic model storage.
- **Snapshot selection only when a play handle is created:** rejected because an already-running
  handle would not observe a take choice or Play all takes toggle until it was restarted.
- **Replace composite playback with active-lane selection:** rejected because user comping already
  expresses a finer-grained playback choice and must remain authoritative.

## Proof plan

The recording test covers three passes, newest-active default selection, a user choice retained
through a later pass, unchanged clip mute flags, and a render comparison where Play all takes sums
the passes. Project save/reopen covers active lane, user-choice state, and runtime mirror
publication. Control tests cover undo/redo for take choice and Play all takes. An allocation probe
and realtime scope cover an existing handle observing an active-take change without allocation.
Existing recording suites, the command snapshot and canary, A16 histogram, repository gates, and
the real-time safety sweep are run for the lane.
