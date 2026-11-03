IMPLEMENTED

# Q050-277 - Capture an exact number of externally clocked bars

Branch `feat/050-Q050-277-oc` (base train 24, `2a7fd9f9c7`). Design, limits and the per-criterion table also in
`docs/050-features/Q050-277.md`.

`session.record_slot` gained `steps` (sixteenth notes; one of `bars`/`steps`) and `clock_sync`. With `clock_sync` the MIDI clock
master sends START when the take starts, exactly `length/2` pulses, and STOP at the take's end (`MidiClock::captureStarted/
Stopped`, driven by `SessionAudioRecorder` from the scheduler's launch/stop events). GUI: three new empty-cell menu entries
(Record Audio for Steps..., Record Bars / Steps with MIDI Clock Start/Stop...), each the `session.record_slot` form.

## Acceptance

All from `build/tests` (`QT_QPA_PLATFORM=offscreen`, no `LMMS_PLUGIN_DIR`); logs in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-277.r1/logs/`.

| Criterion | Test / command | What it proves | Exit |
|---|---|---|---|
| Virtual MIDI clock fixture sees START and STOP at the chosen boundaries | `ctest -R '^SessionAudioRecordTest$'` (`aClockSyncedBarTakeSendsStartAndStopAtItsBoundaries`, `aClockSyncedStepTakeIsExactlyThatManyStepsAndTheDeviceSeesTheOrder`; fixture = master emission monitor + span report) | START tick = take's start tick, STOP exactly 1 bar / 4 steps later, 96 / 24 pulses, order start-pulses-stop, silence before and after | 0 (test-8-final.log) |
| same, from the GUI | `ctest -R '^SessionGridRecordTest$'` (`theClockSyncedStepsEntryStartsAndStopsTheClockAndTheGridLandsTheTake`) | menu entry -> form (`clock_sync` on) -> Run -> START/24 pulses/STOP, take = 4 steps of frames, grid lands it | 0 |
| same, from the agent socket | `ctest -R '^ControlClockCommands$'` (check 13, real `zene`, Midi Through) | refusals (both units; master off), START once, STOP exactly 4 steps later, 24 pulses | 0 |
| Take has the requested musical length and stays aligned after save+reload | `SessionAudioRecordTest::aSyncedTakeKeepsItsLengthAndAlignmentAfterSaveAndReload` | 8 steps -> land -> `project.save` -> `project.open`: cell still names the WAV; WAV is exactly 8x12 ticks of frames; every frame still the input one round trip after the start | 0 |
| New tests fail without the product change | restored base `ControlCommandsSessionAudioRecord.cpp`, `SessionAudioRecorder.{h,cpp}`, `SessionGridMenus.cpp`, rebuilt, `ctest -R '^(SessionAudioRecordTest\|SessionGridRecordTest)$'` | 6 new cases fail (`unexpected property 'clock_sync'`, missing menu entry) | **8** (test-2-reverted.log); with the change restored: 0 |
| Neighbours | `MidiClockTest`, `MidiClockSourceTest`, `SessionAudioRecorderTest`, `SessionAudioCellPlaybackTest`, `ReversibilityContractTest`, `AgentSurfaceReportTest`, `ControlCommandsSnapshot` | still pass | 0 (test-8-final.log, test-9.log) |

## Self-check

- Agent + GUI same command: yes, all entries run `session.record_slot` through the registry (`controlCommand` property asserted in the GUI test).
- Undo / reversibility: arming stays `not_mutating`, landing `true_inverse` (one undo step, existing `landAndCompare` checks undo/redo on the clocked takes). No new ids; `ReversibilityContractTest` passes, A16 histogram unchanged.
- Snapshot: regenerated live with `snapshot_live.py` (exit 0; 878 ids / 77 groups, schema-only change); `tests/doc-figures-gate.py` exit 0; `ControlCommandsSnapshot` exit 0.
- Docs: manual paragraph "Capturing an external synth for an exact length" in `data/manual/index.html`; `docs/050-features/Q050-277.md`; `docs/SESSION-AUDIO-RECORD.md`; `docs/KNOWN-LIMITATIONS.md`.
- Realtime: audio-thread additions are atomics + the existing `emitMessage` MIDI-client path; no allocation, lock, wait, I/O or Qt signal. `captureAbort` only under the engine-held cancel. TSan does not run locally: reasoned instead (the span state is written by the audio thread only; control thread reads atomics).
- Old projects / format: no persisted data added; the take is the same WAV reference as before.
- Portability: no Qt-6-only API, no `min`/`max`/`near`/`far` identifiers, no filesystem use, no POSIX-only call.
- Gates (run individually, their output into my logs, because `feature_check.py`/`submit_check.py` try to create `<worktree>-feature-check/` beside the worktree, which the write-jail refuses - PermissionError; the loop runs the check itself): fork-sources 0, all-sources-reproduce 0, no-upstream-regression 0, check-namespace 0, no-tautology 0, doc-figures 0; complexity and file-length exit 1 (red on the train on purpose) with no item listed in a file I changed (first run listed `MidiClock.cpp` 504 lines and a CCN 14 function in the transcript; fixed by a shared `closeCapture`, and by moving the capture checks into the new `tests/control_clock_capture_checks.py`, registered in `tests/fork-sources.txt`).
- Not checked: hosted CI matrix (Qt 5.15 / MSVC / macOS < 10.15); the "virtual MIDI clock" is the master's emission monitor on the Dummy client (and ALSA Midi Through in the socket test) - it proves what the engine handed its MIDI client, not bytes a device received; START is period-granular.
- `submit_check.py` itself: could not run here (see above); not claimed.
