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
- Realtime (reworked in round 2, see below): the audio thread only appends plain records to a fixed lock-free SPSC ring; a sender thread delivers them to the MIDI client. No client call, I/O, logging, allocation, lock or wait on the audio thread. TSan does not run locally: reasoned (one producer, one consumer, release/acquire head/tail; `captureAbort` from a cancel holds the engine).
- Old projects / format: no persisted data added; the take is the same WAV reference as before.
- Portability: no Qt-6-only API, no `min`/`max`/`near`/`far` identifiers, no filesystem use, no POSIX-only call.
- Gates (round 1 wording; round 2 below) (run individually, their output into my logs, because `feature_check.py`/`submit_check.py` try to create `<worktree>-feature-check/` beside the worktree, which the write-jail refuses - PermissionError; the loop runs the check itself): fork-sources 0, all-sources-reproduce 0, no-upstream-regression 0, check-namespace 0, no-tautology 0, doc-figures 0; complexity and file-length exit 1 (red on the train on purpose) with no item listed in a file I changed (first run listed `MidiClock.cpp` 504 lines and a CCN 14 function in the transcript; fixed by a shared `closeCapture`, and by moving the capture checks into the new `tests/control_clock_capture_checks.py`, registered in `tests/fork-sources.txt`).
- Not checked: hosted CI matrix (Qt 5.15 / MSVC / macOS < 10.15); the "virtual MIDI clock" is the master's emission monitor on the Dummy client (and ALSA Midi Through in the socket test) - it proves what the engine handed its MIDI client, not bytes a device received; START is period-granular.
- `submit_check.py` itself: could not run here (see above); not claimed.

## Round 2 (Luna review: two realtime blockers)

Blockers: capture START/STOP/pulses entered the MIDI client's output path from audio-thread callbacks (device I/O on Linux raw
clients, logging + Core MIDI on macOS), and `captureStopped` looped to emit every owed pulse (duration-scaled; unbounded when the
master was turned off mid-capture or the capture had no length).

Fix (`src/core/MidiClockCapture.cpp`, new; the capture code moved out of `MidiClock.cpp`):
- **Bounded handoff.** `captureStarted` / `captureStopped` / `emitCapturePulses` only append `{message, tick}` records to a fixed
  512-slot single-producer/single-consumer ring (`queueOut`: atomics and one store; 8 slots are kept free of pulses so START/STOP
  always fit; a full ring refuses and counts pulses, `captureOutputDropped`). A **sender thread**, created by `captureArm()` on the
  control thread when a synced capture is armed, delivers the ring in order through `emitMessage` (poll 1 ms while a capture is armed,
  open or undelivered; condition-variable sleep otherwise). The audio thread never calls the MIDI client for the capture.
- **No duration-scaled catch-up.** The stop queues at most `MaxTopUpPulses` = 64 pulses (the period it falls in), then STOP.
- **Master turned off mid-capture:** `processAudioPeriod` ends the span there (`captureAbort`): one STOP at the last pulse's tick, no
  catch-up; the take goes on to its length and the cell's later stop finds the span closed. The transport's own START/CONTINUE waits
  until the capture's messages are delivered, so it cannot overtake the STOP.
- New files registered: `tests/fork-sources.txt`, `tests/all-sources.txt` (re-derived by the recipe), `tests/upstream-modifications.txt`
  (`src/core/CMakeLists.txt`), `src/core/CMakeLists.txt`.

Tests (logs in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-277.r2/logs/`; from `build/tests`):

| What | Test | Exit |
|---|---|---|
| audio thread delivers nothing; sender delivers all in order | `SessionAudioRecordTest::theCallbacksOnlyQueueAndTheSenderThreadDelivers` (sender held: a whole capture runs, 0 delivered, then 26 delivered) | 0 |
| bounded callback work: a 6-bar capture (576 pulses) against a held 512-slot ring: refused pulses counted, START and STOP still delivered | `::aFullRingRefusesPulsesButStillDeliversStartAndStop` | 0 |
| master off mid-capture: span ends with one STOP at the last pulse's tick, no catch-up, take reaches its 2 bars | `::theMasterTurnedOffMidCaptureEndsTheSpanWithoutCatchUp` | 0 |
| the round-1 cases (START/STOP boundaries, steps, cancel, refusals, save+reload) now wait for the sender | `SessionAudioRecordTest`, `SessionGridRecordTest`, `ControlClockCommands` | 0 (test-2-final.log) |
| red proof: `queueOut` delivering synchronously on the calling thread and the master-off abort removed | `ctest -R '^SessionAudioRecordTest$'` | **8** (3 new cases fail: `queued`, `captureOutputDropped() > dropped`, `!ended.active`); real code restored: 0 |
| neighbours | `MidiClockTest`, `MidiClockSourceTest`, `SessionAudioRecorderTest`, `SessionAudioCellPlaybackTest`, `ReversibilityContractTest`, `AgentSurfaceReportTest`, `ControlCommandsSnapshot` | 0 |
| flakiness (the sender is a thread) | `ctest -R '^(SessionAudioRecordTest\|SessionGridRecordTest)$' --repeat until-fail:15` | 0 (15 passes each) |

Gates (individually, output in the r2 logs; `feature_check.py` still hits a PermissionError writing `<worktree>-feature-check/fork-sources.log`
from my account, although the directory exists now): fork-sources 0, all-sources-reproduce 0, no-upstream-regression 0, check-namespace 0,
no-tautology 0, doc-figures 0; complexity and file-length exit 1 (red on the train) with no item in any file I changed. Snapshot
unchanged this round (no command or schema change).

Not checked: hosted CI matrix; macOS/Windows MIDI backends were not run (the sender thread calls the client from a thread other than the
audio thread, as a MIDI reader thread already does for input - documented in KNOWN-LIMITATIONS). The sender adds ~1 ms delivery latency
(period-granular START/STOP already).
