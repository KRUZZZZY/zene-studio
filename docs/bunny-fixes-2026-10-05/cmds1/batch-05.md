# Space Bunny lane `cmds1` — batch 05

Six findings triaged on branch `bugs/bunny-cmds1-2026-10-05`: 4 FIXED (5 defects, two commits
shared), 1 SKIPPED-SCOPE.

### Cluster 5618 — Scaling a MIDI clip leaves its controller points at their old ticks

Verdict: FIXED

Evidence: `src/core/ControlCommandsClipArrange.cpp` — `timeScale` called `scaleNotes` (which moved
note positions by `std::lround(pos * factor)`) but never touched `clip->controllers()`, whose
points carry their own clip-time ticks (`include/MidiClip.h:79-82`). A point at tick 100 stayed at
100 while its note moved to 200, so the lane no longer lined up with the audio. Fixed with a
`scaleControllers(MidiClip*, double)` helper using the same rounding, called right after
`scaleNotes`.

Commit: d589f2288

Test: ClipJoinRippleTest::scalingAMidiClipScalesItsControllerPointsToo

Red/green: `cmake --build build --target ClipJoinRippleTest zene` EXIT=0.
Red: `ctest -R '^ClipJoinRippleTest$'` EXIT=8 — the controller point stayed at 100 while the note
moved to 200. Green: same command EXIT=0.

### Cluster 5617 — Joining MIDI clips discards controller lanes from the removed clips

Verdict: FIXED

Evidence: `src/core/ControlCommandsClipArrange.cpp` — the join loop copied notes from each later
clip by the song-time delta and then deleted the clip, so its controller lanes died with it: CC,
pitch bend, aftertouch and program data were lost. Added `mergeControllers(MidiClip*, const
MidiClip*, tick_t delta)`, which offsets the removed clip's points by the same `from - origin` used
for its notes and merges them into an existing lane of the same kind.

Commit: d589f2288 (shares the commit with 5618 — both defects are in the same file and the two
changes were verified green together, so splitting them would have required intermediate
broken states)

Test: ClipJoinRippleTest::joiningKeepsTheOtherClipsControllerLanes

Red/green: same build/ctest commands as 5618. Red: EXIT=8 — the merged lane pointer was null
after the join. Green: EXIT=0.

### Cluster 5599 — `end_frames` can be silently ignored

Verdict: FIXED

Evidence: `src/core/ControlCommandsClipFrames.cpp` — `wanted()` (line 119-120) routes a trim
carrying `end_frames` to the frame path, but `trimFrames()` set `hasTail` only from `end`
(line 108). With no `end`, both `hasHead` and `hasTail` stayed false and `trim()` returned the
"nothing to move" success at line 148 with the clip untouched — a success reply for a trim that
never ran. `end_frames` is a frame *offset from* the `end` tick (`frameFor` → `frameAtTimeline`,
`src/core/SampleFrameEdits.cpp:175-181`), so with no `end` there is nothing to offset from; `trim()`
now refuses that combination instead of reporting a phantom trim.

Commit: fa3858b28

Test: ArrangingCatchUpTest::anEndFramesTrimWithoutAnEndIsRefused

Red/green: `cmake --build build --target ArrangingCatchUpTest zene` EXIT=0.
Red: `ctest -R '^ArrangingCatchUpTest$'` EXIT=8 — the call answered success, so the
`InvalidArgs` comparison failed. Green: same command EXIT=8 overall but the new case passes
(`Totals: 17 passed, 1 failed`). The one remaining failure,
`queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie`, is pre-existing and environmental: this
build has no `tripleoscillator` plugin, so `loadInstrument("tripleoscillator")` falls back to
`dummyinstrument` and the node-name comparison fails. It is unrelated to `clip.trim` and was
already failing before this batch's change.

### Cluster 5602 — `clip.process` appends a tail that the clip cannot play

Verdict: SKIPPED-SCOPE

Evidence: the observation is accurate. `prepared()` appends `tail * rate` frames of silence
(`src/core/ControlCommandsClipProcess.cpp:68`), the effects render into them (line 128), but
`kept` is clamped to the *scaled original* `window.sourceOut` (lines 133-134), and `replaceSample`
keeps that window and calls `updateLength()` (`src/core/SampleClip.cpp:168-181`) — so the
rendered tail sits outside the playable window and the clip's length is unchanged. Both suggested
remedies are design changes, not minimal fixes: extending the window and clip duration changes what
`clip.process` does to clip length (and therefore the meaning of every later frame operation on that
clip), while removing `tail` removes a command parameter, which rule 8 puts out of bounds. The
parameter is also documented as render headroom ("processed too so a reverb or a delay can ring
out", line 148-149), so it may well be deliberate. No product code changed; a parent decision is
needed on whether `tail` should extend playback or stay render-only.

Commit: none

Test: none

Red/green: none

### Cluster 1046 — `clock.master_set` redo can lose the current port

Verdict: FIXED

Evidence: `src/core/ControlCommandsClock.cpp` — an empty `port` means "keep the current
subscription", but redo captured the raw empty argument and could call
`restoreMaster(enabled, "")`, which unsubscribes when the argument is empty while `m_masterPort` is
not (`src/core/MidiClock.cpp:199-209`). Redo now captures `const QString appliedPort =
clock->masterPort();` after the apply and re-uses that value.

Commit: ed62a4c46

Test: MidiClockTest::masterRedoKeepsThePortTheCallKept

Red/green: `cmake --build build --target MidiClockTest zene` EXIT=0.
Red: `ctest -R '^MidiClockTest$'` EXIT=8 — "redo dropped the port subscription: ''".
Green: same command EXIT=0.

### Cluster 5609 — Disabling the MIDI clock slave clears its follow preference

Verdict: FIXED

Evidence: `src/core/ControlCommandsClock.cpp` — the handler computed `follow` from the requested
value or the current setting and then passed `enabled && follow` to `restoreSlaveState`, so
disabling the slave persisted `follow_tempo: false` and a later plain re-enable left following off.
`slaveSet` now passes `follow` unchanged and the helper uses `enabled` only to decide whether to
run the timer.

Commit: ed62a4c46 (shares the commit with 1046 — same file, both verified green together)

Test: MidiClockTest::disablingTheSlaveKeepsItsFollowPreference

Red/green: `ctest -R '^MidiClockTest$'` — Red: EXIT=8, `slaveFollowTempo()` false after the
disable. Green: EXIT=0.

## Gates

Both gates returned EXIT=0 before every commit in this batch:
`bash tests/complexity-gate.sh --check` and `bash tests/file-length-gate.sh --check`.

## Leftovers

- Cluster 5602 needs a product decision (make `tail` extend the playable window and clip length,
  or drop the parameter) — deliberately left unfixed as a design change.
- `ArrangingCatchUpTest::queuedInstrumentHandlesAreRemovedBeforeTheirTrackCanDie` fails in this
  build because the `tripleoscillator` plugin is not installed; pre-existing and unrelated to this
  batch, but it will keep that executable red until the plugin is present.
- Clusters 5618 and 5617 (commit `d589f2288`) and 1046 and 5609 (commit `ed62a4c46`) each pair two
  defects in one commit; both members were verified green together, so the batch has 3 fix commits
  for 5 defects.