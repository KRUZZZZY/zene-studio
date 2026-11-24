FIXED

# Cluster new-p16-32: a cancelled take no longer leaves its slot playing

**Verdict:** FIXED. The finding is real. `cancelRecording()` used to drop the take with
`recorder.cancel()` and only then queue the cell's stop with `SessionScheduler::requestStop()`,
discarding the `bool` it returns. `requestStop()` is `enqueue()`; when the scheduler queue is full
that push fails, increments `droppedCommands()`, and queues nothing. The take was gone, the cell
kept playing, and `session.record_slot` reported success.

The stop is now queued **before** the take is dropped, inside the same `withEngineHeld` section.
The recorder state is read under that hold. A stop is required only for `Armed` and `Recording`
takes; a `Finishing` take has already had its cell stopped, so it can still be cancelled when the
queue is full. A refused stop cancels nothing and reports `Busy` with the take left in hand.

**Product change:** `src/core/ControlCommandsSessionAudioRecord.cpp` only. It is a fork-new file
already listed in `tests/fork-sources.txt`, so no new registration was needed. The regression tests
extend the existing `tests/src/core/SessionAudioRecordTest.cpp`, whose cases are recorded in
`tests/upstream-modifications.txt`.

## Evidence

| # | Claim | Source | What it shows |
|---|---|---|---|
| E1 | The original defect and the reversed patch | `src/core/ControlCommandsSessionAudioRecord.cpp:136-165`; `logs/fix.patch` | Reversing `fix.patch` restores the old order: `recorder.cancel()` first, then `requestStop(...)` whose `bool` is ignored. The red run `logs/test-2-red.log` shows the command answered `Ok` (kind 0) where `Busy` (kind 4) is required. |
| E2 | A stop push can be refused | `src/core/SessionScheduler.cpp:262-266`, `:235-245` | `requestStop()` is `enqueue(Command{..., Stop, ...})`; `enqueue()` returns false and adds one to `m_dropped` when `m_queue.push()` fails. |
| E3 | With no queued Stop the cell keeps playing | `src/core/SessionScheduler.cpp:456-473` | No Stop command leaves the slot in `Playing`, which `trackIsSessionActive()` reports. Nothing else on the cancel path ends the cell. |
| E4 | The sibling path checks the same push | `src/core/ControlCommandsSessionAudioRecord.cpp:227-231` | `armAndLaunch()` refuses a full launch queue with `Busy` and "the launch queue is full; nothing was armed" instead of lying. |
| E5 | The fix | `src/core/ControlCommandsSessionAudioRecord.cpp:136-165` | State is read under `withEngineHeld`; `stopsCell` is true only for `Armed`/`Recording`; the stop is queued before `recorder.cancel()`; a refusal returns `Busy` and leaves the take in hand. |
| E6 | A finishing take needs no stop | `src/core/SessionAudioRecorder.cpp:141-153` | `slotStopped()` sets `Done` or `Finishing` after the cell's stop. The round-1 handler treated anything other than `Idle`/`Done` as needing a stop, which made `Finishing` refuse with `Busy`. `logs/test-4-qa-red.log` reproduces exactly that; `logs/test-5-qa-green.log` passes after restoring the state check. |
| E7 | The retry test's take is open-ended | `src/core/SessionFollow.cpp:231-238`; `tests/src/core/SessionAudioRecordTest.cpp:571-573` | A fixed-length take gets `SlotPhase::StopPending` when it starts. The retry test arms no `bars`/`steps`, so its length is 0, no automatic stop is scheduled, and only the retry's own stop can take the cell down. |

## Round-2 review response

1. **Finishing take must not return Busy on a full queue.** The state is checked while the engine
   is held, and `stopsCell` covers only `Armed` and `Recording` (`ControlCommandsSessionAudioRecord.cpp:147-153`).
   `aFinishingTakeCanBeCancelledWithAFullQueue` drives a one-bar take into `Finishing`, confirms the
   cell is already inactive, fills the command queue, and requires `ok`, `cancelled: true`,
   `state: idle`, and no new dropped command (`SessionAudioRecordTest.cpp:606-629`).
2. **Retry must prove that the retry stopped the cell.** The retry test now arms an open-ended take
   and verifies the cell is still active after the refused cancel, then drives periods until only
   the retry-queued stop could have ended it (`SessionAudioRecordTest.cpp:569-604`).

## Test matrix

| Case | Driven by | Expected / observed |
|---|---|---|
| Normal path: cancel a live take | existing `theSurfaceRefusesWhatItCannotRecord` | `ok`, `cancelled: true`, recorder `idle`; unchanged and passing |
| Failure / exhaustion: cancel while the queue is full | `aCancelWhoseStopCannotBeQueuedKeepsTheTakeInHand` | `Busy`, `droppedCommands()` +1, recorder still `recording`, cell still active |
| Finishing take with a full queue | `aFinishingTakeCanBeCancelledWithAFullQueue` | `ok`, `cancelled: true`, recorder `idle`, no dropped command |
| Recovery / reconfiguration: cancel again after the audio thread drains | same retry test, second half | `ok`, `cancelled: true`, `idle`, and the retry's stop ends the cell on its bar line |
| No take in hand (`Idle`) or finished (`Done`) | existing tests | no stop is required, so no push and no new refusal |
| Audio thread | design | the changed code runs on the control thread; one lock-free push, no allocation, lock, wait or log added to the audio path |
| Supported-platform build | `zene` plus the five session test targets on this box (Linux, Qt 6.12) | build logs below; no platform-specific code touched |

Determinism: the tests never sleep or read wall-clock time. `initTestCase` stops the dummy device,
so the test's `period()`/`renderNextPeriod()` calls are the only producers of audio periods. The
queue is filled with `requestStop(999, 0, ...)` until `commandQueueSpace()` is 0, which is exact
while no consumer runs, and refusal is observed through the deterministic `droppedCommands()`
counter. All clock advancement is a bounded count of rendered periods.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p16-32.r2/logs`. The product changes
were already committed, so `fix.patch` is taken from the branch base (`6e277efd30`) rather than
from an uncommitted worktree diff.

```
# GREEN on the final tree
cmake --build build --target zene SessionAudioRecordTest SessionAudioRecorderTest \
    SessionAudioCellPlaybackTest ControlSessionRecordLandTest SessionGridRecordTest -j4 \
    > logs/build-1-green.log 2>&1                                              EXIT=0
cd build/tests && ctest -R '^SessionAudioRecordTest$' --output-on-failure \
    > logs/test-1-green.log 2>&1                                             EXIT=0
  1/1 Test #705: SessionAudioRecordTest ...........   Passed    1.37 sec

# 4b RED: reverse the product-only patch, keep the current tests
git diff 6e277efd30..HEAD -- src/core/ControlCommandsSessionAudioRecord.cpp > logs/fix.patch
                                                                             EXIT=0
git apply -R logs/fix.patch                                                  EXIT=0
cmake --build build --target SessionAudioRecordTest -j4 > logs/build-2-red.log 2>&1
                                                                             EXIT=0
cd build/tests && ctest -R '^SessionAudioRecordTest$' --output-on-failure \
    > logs/test-2-red.log 2>&1                                               EXIT=8
  FAIL!  : aCancelWhoseStopCannotBeQueuedKeepsTheTakeInHand()
     Actual (refused.errorKind): 0 / Expected (ControlErrorKind::Busy): 4
  FAIL!  : aFinishingTakeCanBeCancelledWithAFullQueue()
     Actual (scheduler.droppedCommands()): 2 / Expected (dropped): 1
  Totals: 17 passed, 2 failed

# restore the full product patch
git apply logs/fix.patch                                                     EXIT=0
cmake --build build --target SessionAudioRecordTest -j4 > logs/build-3-restored.log 2>&1
                                                                             EXIT=0
cd build/tests && ctest -R '^SessionAudioRecordTest$' --output-on-failure \
    > logs/test-3-green-restored.log 2>&1                                    EXIT=0
  100% tests passed out of 1

# isolated round-1 regression: reverse only the QA product change
git diff 335b2f2da8..d765b52a31 -- src/core/ControlCommandsSessionAudioRecord.cpp \
    > logs/qa-finishing-fix.patch                                            EXIT=0
git apply -R logs/qa-finishing-fix.patch                                     EXIT=0
cmake --build build --target SessionAudioRecordTest -j4 > logs/build-4-qa-reverted.log 2>&1
                                                                             EXIT=0
cd build/tests && ./SessionAudioRecordTest aFinishingTakeCanBeCancelledWithAFullQueue \
    > logs/test-4-qa-red.log 2>&1                                            EXIT=1
  FAIL!  : 'cancelled.ok' returned FALSE. (the stop queue is full: cell (track 1,
  scene 0) would keep playing, so nothing was cancelled and the take (still
  finishing) is in hand ...)
  Totals: 2 passed, 1 failed

# restore the QA change and rerun the same case
git apply logs/qa-finishing-fix.patch                                        EXIT=0
cmake --build build --target SessionAudioRecordTest -j4 > logs/build-5-qa-restored.log 2>&1
                                                                             EXIT=0
cd build/tests && ./SessionAudioRecordTest aFinishingTakeCanBeCancelledWithAFullQueue \
    > logs/test-5-qa-green.log 2>&1                                          EXIT=0
  Totals: 3 passed, 0 failed

# final rebuild and the focused family
cmake --build build --target zene SessionAudioRecordTest SessionAudioRecorderTest \
    SessionAudioCellPlaybackTest ControlSessionRecordLandTest SessionGridRecordTest -j4 \
    > logs/build-6-final.log 2>&1                                            EXIT=0
cd build/tests && ctest -R '^(SessionAudioRecordTest|SessionAudioRecorderTest|SessionAudioCellPlaybackTest|ControlSessionRecordLandTest|SessionGridRecordTest)$' \
    --output-on-failure > logs/test-6-family.log 2>&1                        EXIT=0
  100% tests passed out of 5

# gates
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> \
    > logs/gate-1.log 2>&1                                                   EXIT=0
  PASS fork-sources / all-sources-reproduce / no-upstream-regression /
       check-namespace / no-tautology / doc-figures / complexity / file-length
  RESULT: PASS
```

After the last test/restore cycle `git status --short` was empty and `git rev-parse HEAD` was
`d765b52a31a8441f036a23c379595a683fbeafa9` (the product commit tested above). The report commit
that follows changes documentation only.

## Self-check

**A1. The regression tests FAIL with the product change reverted and PASS with it (exit codes).**
On this branch and in this session, the product-only patch was reversed with `git apply -R`
(EXIT=0), `SessionAudioRecordTest` was rebuilt (EXIT=0), and CTest exited **8** on
`logs/test-2-red.log`: both `aCancelWhoseStopCannotBeQueuedKeepsTheTakeInHand` and
`aFinishingTakeCanBeCancelledWithAFullQueue` failed (17 passed, 2 failed). The patch was re-applied
(EXIT=0), the target rebuilt (EXIT=0), and the same CTest command exited **0** on
`logs/test-3-green-restored.log` (100% passed). The final tree's `logs/test-1-green.log` also
exited 0. The round-1-specific regression is isolated separately: reversing only the QA product
change made `aFinishingTakeCanBeCancelledWithAFullQueue` exit **1** with the Busy refusal
(`logs/test-4-qa-red.log`), and restoring it made the same case exit **0**
(`logs/test-5-qa-green.log`).

**A2. The tests are deterministic: no wall-clock timing, sleeps or races.**
The seam is the engine's own period pump. `initTestCase` stops the dummy device, so the test's
`period()` / `renderNextPeriod()` is the only producer of audio periods and the scheduler has no
second consumer. Exhaustion is produced by pushing `requestStop(999, 0, ...)` until
`commandQueueSpace()` reports 0 (exact on the producer side while no consumer runs,
`include/SessionScheduler.h:441-448`), and refusal is asserted through `droppedCommands()`. The
clock is advanced by a bounded period count, never by sleeping or reading elapsed time. `MidiClock`'s
sender thread is not exercised (no `clock_sync`).

**A3. The fix is at the cause and complete: every caller and sibling was checked.**
`cancelRecording()` has one caller, `recordSlot()` (`ControlCommandsSessionAudioRecord.cpp:249`).
The only other cancels of a session audio recorder are:
- `src/core/Song.cpp:1819` in `clearProject`, where every slot is going away, so no stop is needed;
- `src/core/ControlCommandsSamplerTake.cpp:174`, which cancels `SamplerTakeRecorder` (no launcher slot);
- `ControlCommandsSessionAudioRecord.cpp:229` in `armAndLaunch`, after a refused launch, so no cell was launched.
All three remain consistent: a stop is queued only where a cell was launched.
`SessionScheduler::requestStop` itself is unchanged, so its other callers
(`src/core/SessionTransitions.cpp:109`, `src/core/ControlCommandsSessionLaunch.cpp:271`) keep their
behaviour.

**A4. No collateral damage.**
For every cancel that can queue its stop the result is unchanged: `recorderState()` plus
`cancelled`, with no fields added or removed. Only the case that previously lied changes, and there
the take is kept and the cell keeps playing. No new limit was added and the existing queue
capacity (`CommandQueueCapacity = 256`) is untouched. The change is in a command handler, not in a
writer or reader, so saved projects load as before; `aSyncedTakeKeepsItsLengthAndAlignmentAfterSaveAndReload`
still saves and reloads, and the five focused session tests pass (`logs/test-6-family.log`, EXIT=0).
Gates: RESULT PASS, EXIT=0 (`logs/gate-1.log`).

**A5. Verdict evidence.** The verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE. The defect is
the pre-fix ordering and ignored stop result shown by `logs/fix.patch`, and the red run
`logs/test-2-red.log` (EXIT=8) shows the old handler answering `Ok` where `Busy` is required.

**A6. Sibling commands in the changed command file.**
`ControlCommandsSessionAudioRecord.cpp` registers exactly three commands:
- `session.record_slot` is the one fixed; its cancel branch now refuses on a full queue and its arm
  branch already refused there (`:227-231`), so both paths agree;
- `session.record_slot_status` is read-only and never touches the queue or the recorder state;
- `session.record_slot_land` refuses unless the recorder is `Done` and queues nothing, so a full
  queue cannot make it lie.
No command in the `app.*` or `arrangement.*` groups is in this file or was touched. Outside this
file, `session.stop_slot` has its own pre-existing behaviour; see Other findings.

## Other findings

- `session.stop_slot` (`src/core/ControlCommandsSessionLaunch.cpp:271-276`) has the same shape of
  defect: it discards `requestStop`'s `bool` and always answers `stop_requested: true`, so a full
  queue makes it report a stop that was dropped. It is a different command and outside this job's
  finding, so it is left unchanged and flagged for its own ticket. `SessionScheduler::applyCommand`
  also applies a `Stop` with the slot's stored quantisation, so `stop_slot`'s `quantisation`
  argument does not decide when the stop lands.
