IMPLEMENTED

# Q050-277 - Capture an exact number of externally clocked bars (round 3)

Branch `feat/050-Q050-277-oc` (base train 24, `2a7fd9f9c7`). Design, limits and the per-criterion table also in
`docs/050-features/Q050-277.md`. Round-3 logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/Q050-277.r3/logs/`.

## Round 3: what the review asked, what changed

1. **The sender was a second producer of ALSA's SPSC queue.** The sender now delivers through a new
   `MidiClient::sendFromNonAudioThread()` (include/MidiClient.h, src/core/midi/MidiClient.cpp): serialized by a mutex the audio
   thread never takes, then the virtual `processNonAudioOutEvent()`. `MidiAlsaSeq` overrides it with its OWN `MidiOutQueue`
   (`m_nonAudioQueue`, one producer = the mutex holder) and its sequencer thread drains it right after `m_outQueue` (one consumer
   merges track MIDI and capture clock); `m_outQueue` keeps the audio thread as its only producer. Raw clients accept only
   one-byte System Real-Time messages from it (legal anywhere in a MIDI stream; one pipe write / `snd_rawmidi_write` /
   MPSC JACK push). Core MIDI / WinMM go through their thread-safe system calls via the base. `MidiClock::deliverOffAudio()`
   leaves a refused record at the ring's tail and retries it, in order. Audio-path code is unchanged.
2. **A backed-up ring could accept a START and drop its STOP.** `captureStarted()` now admits a span only when START and its
   STOP both fit (`ringFree() >= 2`); every pulse (and START) keeps `ReservedSlots` = 1 free, which only the open span's STOP
   takes. One producer, so the STOP's slot is held from START to STOP. A span that does not fit is refused BEFORE START:
   nothing is queued, the take records unclocked, `session.record_slot_status` `clock.refused` counts it.

## Acceptance

All from `build/tests` (`QT_QPA_PLATFORM=offscreen`, no `LMMS_PLUGIN_DIR`), this round, on the final tree.

| Criterion | Test / command | What it proves | Exit |
|---|---|---|---|
| Virtual MIDI clock fixture sees START and STOP at the chosen boundaries | `SessionAudioRecordTest` (`aClockSyncedBarTakeSendsStartAndStopAtItsBoundaries`, `aClockSyncedStepTakeIsExactlyThatManyStepsAndTheDeviceSeesTheOrder`), `SessionGridRecordTest` (GUI menu entry driven), `ControlClockCommands` (agent socket, real `zene`) | START at the take's start tick, STOP exactly 1 bar / 4 steps later, 96 / 24 pulses, order start-pulses-stop | 0 (test-10-final.log) |
| Take has the requested length and stays aligned after save + reload | `SessionAudioRecordTest::aSyncedTakeKeepsItsLengthAndAlignmentAfterSaveAndReload` | 8 steps, land, `project.save`, `project.open`: cell names the WAV, exact frames, frame-aligned | 0 (test-10-final.log) |
| Review r2 #1: capture clock concurrent with ordinary MIDI, no second producer | `MidiClockCaptureOutputTest::captureClockLeavesThroughTheOffAudioPathBesideTrackMidi`; `MidiOutQueueTest::offAudioOutputNeverSharesTheAudioThreadsQueue` (real `MidiAlsaSeq`, 1 audio producer + 2 off-audio senders x 60000 events, concurrent consumer) | every `processOutEvent()` from the audio thread; START/24 pulses/STOP once, in order, only via the off-audio path; every note once; on ALSA none misrouted, lost, duplicated or reordered | 0 (test-10-final.log) |
| Review r2 #2: ring filled by pending output, then successive captures | `MidiClockCaptureOutputTest::everyAcceptedStartHasItsStopWhenEarlierOutputFillsTheRing` | sender held; 1 span with a pulse + 300 empty spans: 255 admitted, 46 refused before START; after delivery STARTs == STOPs; a span after draining is admitted | 0 (test-10-final.log) |
| Realtime/failure paths kept (held sender, saturated ring, master off mid-capture, cancel) | `SessionAudioRecordTest` round-2 cases | unchanged contracts still hold | 0 |
| Neighbours | `MidiClockTest`, `MidiClockSourceTest`, `MidiOutTimingTest`, `SessionAudioRecorderTest`, `SessionAudioCellPlaybackTest`, `ReversibilityContractTest`, `AgentSurfaceReportTest`, `ControlCommandsSnapshot` | still pass | 0 (13/13, test-10-final.log) |
| Flakiness (threads) | `ctest -R '^(MidiClockCaptureOutputTest\|MidiOutQueueTest\|SessionAudioRecordTest)$' --repeat until-fail:10`, and again for the first two after the refactor | 10/10 each | 0 (test-9-repeat.log, test-11-repeat.log) |

## Self-check

- **New tests fail with the product change reverted, pass restored.** Reverted the three behaviour hunks (`deliverOffAudio` back
  to `emitMessage`/`processOutEvent`; `MidiAlsaSeq::processNonAudioOutEvent` back to the base, i.e. into `m_outQueue`; the
  round-2 ring admission: START/STOP on the whole ring, pulses keep 8), keeping the API and test seams so the tests compile;
  rebuild exit 0 (build-7/8/10-revert.log); `ctest -R '^(MidiOutQueueTest|MidiClockCaptureOutputTest|SessionAudioRecordTest)$'`
  **exit 8** (test-7-revert.log): `offThreadAudioPathCalls` 26 (expected 0); `stopsDelivered` 255 vs `startsDelivered` 256 (the
  review's exact scenario); `misrouted` 104399 (expected 0). After refactoring the ALSA test, re-reverted the ALSA hunk alone:
  exit **8**, `misrouted` 607190784 (the corrupted SPSC queue spun its consumer; the test's 20 s give-up and bounded drain turn
  that into a failure, not a hang) (test-12-revert.log). Restored (`git checkout` of the three files): build exit 0, the same
  tests exit 0 (test-8.log, test-10-final.log, test-13.log). Round-1/2 feature tests' red/green proofs are recorded below.
- **Every acceptance criterion proven by a test run this round:** see the table (exit 0, test-10-final.log).
- **Undo / reversibility:** no new command id; `session.record_slot` / `_status` stay `not_mutating`, `session.record_slot_land`
  `true_inverse` (one undo step, `landAndCompare` checks undo/redo). Only the status description changed (`clock.refused`);
  `ReversibilityContractTest` exit 0, A16 histogram unchanged.
- **Snapshot:** regenerated live (`snapshot_live.py`, exit 0, 878 ids / 77 groups, snapshot-1.log); `tests/doc-figures-gate.py`
  exit 0 (docfig-1.log); `ControlCommandsSnapshot` exit 0. The submit check's canary WARN lists existing ids and file names
  (`clock.get_state`, `clocked.mmp`, ...) that appear in my docs/tests; there are no new command ids.
- **Realtime:** the audio-thread additions are `ringFree()`/`queueOut()` (atomic loads, one store into a fixed array) and the
  admission check: no allocation, free, lock, wait, I/O or Qt signal. The audio path `processOutEvent()` is unchanged; the new
  mutex is taken only by `sendFromNonAudioThread()`, called only by the sender thread. TSan does not run on this box; reasoned:
  the capture ring has one producer (audio thread; `captureAbort` from a cancel holds the engine) and one consumer (sender);
  `m_outQueue` has one producer (audio thread) and one consumer (sequencer thread); `m_nonAudioQueue` has one producer (mutex
  holder) and the same consumer. The MidiOutQueueTest stress case exercises the ALSA half concurrently.
- **Old projects / format:** nothing persisted was added or changed; the take is the same WAV reference; save+reload case exit 0.
- **Portability:** `std::mutex`/`<mutex>` (already used by `MidiClock.h`), no Qt-6-only API, no `min`/`max`/`near`/`far`/`small`/
  `interface` identifiers, no std::filesystem, the only POSIX call (`::write` to the wake pipe) is in `MidiAlsaSeq.cpp` (ALSA-only).
  Core MIDI / WinMM inherit the base `processNonAudioOutEvent` (no change to their files). Not compiled here: Qt 5.15, MSVC,
  macOS, Windows ARM - no toolchain on this host; unverified.
- **Gates:** `feature_check.py` / `submit_check.py`'s gate step exit 1 with a PermissionError: they write
  `<worktree>-feature-check/fork-sources.log` beside the worktree, outside this lane's write-jail (submit-check-1.log,
  feature-check-1.log). Ran its eight gates individually from the worktree (gate-*.log): fork-sources 0, all-sources-reproduce 0,
  no-upstream-regression 0, check-namespace 0, no-tautology 0, doc-figures 0; complexity 1 and file-length 1 (red on the train on
  purpose) with **no REGRESSION item in any file this branch changed** (gate-complexity-2.log, gate-file-length-2.log; the first
  run flagged my new ALSA test at CCN 15, fixed by moving it into a `TwoQueueStress` helper).
- **Registration:** `tests/src/core/MidiClockCaptureOutputTest.cpp` in `tests/CMakeLists.txt`, `tests/fork-sources.txt` and
  `tests/all-sources.txt` (all-sources-reproduce exit 0); `tests/upstream-modifications.txt` has reasons for `include/MidiClient.h`,
  `src/core/midi/MidiClient.cpp`, `include/MidiAlsaSeq.h`, `src/core/midi/MidiAlsaSeq.cpp`, `tests/CMakeLists.txt`.
- **Not checked:** hosted CI matrix; macOS/Windows MIDI backends; bytes received by a hardware device (the fixtures are what the
  engine handed its MIDI client, and the ALSA client's own queues).

## Earlier rounds (history)


Branch `feat/050-Q050-277-oc` (base train 24, `2a7fd9f9c7`). Design, limits and the per-criterion table also in
`docs/050-features/Q050-277.md`.

`session.record_slot` gained `steps` (sixteenth notes; one of `bars`/`steps`) and `clock_sync`. With `clock_sync` the MIDI clock
master sends START when the take starts, exactly `length/2` pulses, and STOP at the take's end (`MidiClock::captureStarted/
Stopped`, driven by `SessionAudioRecorder` from the scheduler's launch/stop events). GUI: three new empty-cell menu entries
(Record Audio for Steps..., Record Bars / Steps with MIDI Clock Start/Stop...), each the `session.record_slot` form.

### Acceptance

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

### Self-check

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

### Round 2 (Luna review: two realtime blockers)

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
