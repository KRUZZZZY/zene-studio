IMPLEMENTED

# Q050-054.1 - Record and comp MIDI takes on instrument tracks, stage 1/2: capture armed MIDI tracks into arrangement takes

Branch `feat/050-Q050-054.1-oc`, cut from `050/m0-train-19` (`5c1c6715d9`). Feature record (design, commands, realtime
reasoning, format, known limits): `docs/050-features/Q050-054.1.md`. Logs: `~/.local/share/zene-bunny-fix/qwen/runs/Q050-054.1.r1/logs/`
(file names in brackets). Environment: the brief's, plus `CCACHE_TEMPDIR=/tmp/ccache-tmp-q0541` (see Self-check).

**Change.** `track.set_midi_arm` arms an instrument track's `MidiTakeCapture` (a bounded lock-free MPSC queue fed by
`InstrumentTrack::processInEvent`, stamped with the period's song tick and loop pass that `Song::processNextBuffer`
publishes). `transport.record` opens a session on every armed track (`MidiTakeRecorder`, main thread); Stop - the
header's button, the space bar or `transport.stop` - runs `record.midi_capture_commit`: a plain pass becomes one MIDI
clip, a loop recording one take clip per pass on its own take lane of one take group, the newest active unless a take
was chosen (a later loop recording over the region joins the group and keeps the choice). One undo step. An inactive
MIDI take is silent (`InstrumentTrack::takePlays`). `record.midi_capture_status` reads it back. GUI: the track's
Record menu - Record MIDI Takes On/Off, MIDI Take Status, Keep the Captured MIDI.

## Acceptance

"Reverted" = the nine modified product files written back from `5c1c6715d9` and the five new product files moved out,
tests unchanged, `zene` rebuilt; restored from the commit afterwards (`git diff HEAD -- src include` empty) and rebuilt.
"No capture" = only the one capture line in `InstrumentTrack::processInEvent` removed (everything else built), so the
QtTests compile and their behaviour is measured.

| # | Criterion | Test (command) | Reverted / no capture | With the change |
| --- | --- | --- | --- | --- |
| 1 | A control-socket transcript records a known MIDI phrase alongside an audio track, then reads the MIDI take and notes back | `ctest -R "^ControlMidiTakeRecord$"` (`tests/control-midi-take-record.py`: 3-note phrase by `aplaymidi` into the track's own ALSA port while an armed sample clip records; `note.list` keys / velocities / gaps and lengths within 4 ticks; measured `[(23,24,60,78),(71,24,64,62),(119,48,67,100)]` for a phrase at gaps 48 and lengths 24/24/48) | reverted: EXIT=8, `no command 'track.set_midi_arm'` [test-neg-01-transcript]; no capture: EXIT=8, "recorded alongside audio ... FAILED" [test-neg-02-nocapture] | EXIT=0, 4/4 checks [test-03-transcript, test-08-transcript, test-11-final] |
| 2 | Loop passes become lane-tagged takes under the active-take policy; the capture command schema and one-step undo/redo cover the persistent MIDI take | transcript checks "one control.undo / control.redo covers the take" and "loop passes become take lanes, the newest active"; `ctest -R "^MidiTakeRecordTest$"` (11 slots: plain pass, nothing unless armed+recording, one undo step, loop takes newest active and only it plays, chosen take survives a later loop recording, save/load round trip, Replace erases notes only, queue bound + zero allocations/frees, two producers); `ReversibilityContractTest`, `ControlCommandsSnapshot` | reverted: MidiTakeRecordTest build EXIT=2 (`MidiTakeCapture.h: No such file`) [build-neg-02-core]; transcript EXIT=8; no capture: MidiTakeRecordTest EXIT=8 - 6 behaviour slots FAIL, the 2 queue-only slots pass [test-neg-02-nocapture]; transcript undo + loop checks FAILED | EXIT=0 each, 11/11 [test-11-final] |
| GUI | Definition of done 2: the GUI control drives the same commands | `ctest -R "^MidiTakeMenuTest$"` (real app: Record > Record MIDI Takes > On, header Record button, MIDI into the running engine, header Stop button -> take + transaction `record.midi_capture_commit`; Off) | reverted: build EXIT=2 [build-neg-03-gui]; no capture: EXIT=8 [test-neg-02-nocapture] | EXIT=0 [test-07-gui, test-11-final] |

Regression neighbours on the final build, one ctest run EXIT=0, 11/11 [test-11-final]: `RecordingRealtimeTest`,
`ReversibilityContractTest`, `TakeLaneCompTest`, `TakeLaneTest`, `LanesUpconvertTest`, `TakeLanesPanelTest`,
`TakeLaneCompPlaybackTest`, `ControlCommandsSnapshot` plus the three new tests.

## Self-check

- **Each new test fails with the product change reverted, passes restored.** Yes - table above: reverted build of
  `zene` EXIT=0 [build-neg-01-zene], transcript EXIT=8, QtTests do not build (EXIT=2); the narrower "no capture"
  build (EXIT=0 [build-neg-04-nocapture]) makes all three ctests fail (EXIT=8) with the QtTests compiled; restored
  build EXIT=0 [build-09-final], ctest EXIT=0.
- **Every acceptance criterion proven by a test I ran.** Criterion 1: ControlMidiTakeRecord EXIT=0. Criterion 2:
  ControlMidiTakeRecord + MidiTakeRecordTest + ReversibilityContractTest + ControlCommandsSnapshot EXIT=0. GUI:
  MidiTakeMenuTest EXIT=0. The transcript needs real ALSA MIDI (`aplaymidi`); on a host without it it reports
  Skipped (77), never Passed - on this box it ran and passed.
- **One undo step; honest classes.** `record.midi_capture_commit` `true_inverse`: one multi-object
  `ProjectJournal` checkpoint of every changed track before the first clip, so one `control.undo` (undone_command
  `record.midi_capture_commit`) removes a whole plain or loop recording and one `control.redo` restores it
  (`oneUndoStepCoversTheTake`, `loopPassesBecomeTakesNewestActive`, transcript). `track.set_midi_arm`
  `not_mutating` (mode state, the `midi.retro_capture_arm` precedent); `record.midi_capture_status`
  `not_mutating`; `transport.record` unchanged `not_mutating` (result gains `midi_tracks`). The commit refuses
  before any checkpoint when nothing is pending. A16 histogram re-taken in `docs/RELEASE-NOTES-v0.3.0-alpha.md`
  (880 = 433/42/25/380); snapshot regenerated live with `snapshot_live.py` (EXIT=0, 871 ids / 76 groups), README
  canary updated, `doc-figures-gate.py` EXIT=0.
- **Realtime.** Audio/MIDI-thread additions: `MidiTakeCapture::capture` (called from
  `InstrumentTrack::processInEvent`, which runs on the ALSA sequencer thread or, for JACK MIDI, the audio thread):
  atomic loads, a bounded CAS loop, a 12-byte copy, a release store - no allocation, free, lock, wait, I/O or
  signal; measured zero allocations and frees over 4106 pushes (`theCaptureQueueIsBoundedAndAllocationFree`).
  `MidiTakeCapture::publishPosition` in `Song::processNextBuffer`: one relaxed store. `InstrumentTrack::takePlays`
  in `InstrumentTrack::play`: atomic loads plus the `segments()`/`auditionLane()` reads `compPlays` already makes.
  Storage is allocated on the main thread at first arm and freed only with the track (declared before the MIDI
  port, so it outlives it). Clips, lanes, journal steps and the 50 ms drain timer are main-thread only. TSan does
  not run on this box: the race reasoning is in the feature record (Vyukov's ordering; storage published before
  the release-store of `m_capturing`); `twoProducersLoseNothing` exercises concurrent producers and consumer.
- **Old projects / round trip.** Only clips with a take group are gated; before this unit only sample loop
  recording wrote `takegroup`, so an old project's MIDI clips play as before (no bundled `.mmp` contains
  `takegroup`; `.mmpz` not inspected; no render comparison was run). No format change: the takes use the existing
  `lane`/`takegroup` attributes and `<z:lanes>` take-group state; `takesRoundTripThroughSaveAndLoad` saves and
  reloads the takes, lanes, group, active take and notes; `LanesUpconvertTest` passes.
- **Portability.** No Qt-6-only API (QPointer, QTimer, QJsonObject, std::atomic, std::array/map/vector only); no
  `small/near/far/interface/min/max` identifiers (a test helper first named `near` was renamed `withinATick`
  before commit 2 and the test re-run, EXIT=0 [test-12-rename]); no std::filesystem; no POSIX call in product code.
- **Gates.** `feature_check.py` could not run here: it writes its logs to `<worktree>/../...-feature-check/`,
  outside this lane's write area (PermissionError). I ran its gates directly: fork-sources 0, all-sources-reproduce 0
  (after re-deriving `tests/all-sources.txt` with its recipe), no-upstream-regression 0, check-namespace 0,
  no-tautology 0, doc-figures 0; complexity and file-length `--check` exit 1 on the train (red on purpose) with no
  REGRESSION line naming any file this branch changed (two over-CCN functions in `MidiTakeRecorder.cpp` were split
  until none remained).
- **ccache.** The configured `temporary_dir` is outside the write area; builds used `CCACHE_TEMPDIR=/tmp/ccache-tmp-q0541`.
- **Not done / limits (stage 1).** Controller/pitch-bend/aftertouch input is not captured; punch regions do not gate
  MIDI; the MIDI comp lane strip and its GUI proof are stage 2 (Q050-054.2).
