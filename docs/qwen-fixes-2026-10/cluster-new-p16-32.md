FIXED

# Cluster new-p16-32: a cancelled take no longer leaves its slot playing

**Verdict:** FIXED. The finding is real. `cancelRecording()` dropped the take in hand and *then* queued
the cell's stop with `SessionScheduler::requestStop()`, ignoring the `bool` it returns. When the
scheduler command queue is full that push is refused (`SessionScheduler::enqueue` drops the command and
bumps `droppedCommands()`), so the take was gone, the cell kept playing, and `session.record_slot`
reported success. The stop is now queued **before** the take is dropped, inside the same
`withEngineHeld` section, and a refusal cancels nothing and reports `Busy`.

**Product change:** `src/core/ControlCommandsSessionAudioRecord.cpp` only (a fork-new file, already listed
in `tests/fork-sources.txt`, so no registration change). The regression test extends the existing
`tests/src/core/SessionAudioRecordTest.cpp`.

## Evidence

| # | Claim | Source | What it shows |
|---|---|---|---|
| E1 | The defect as reported | `src/core/ControlCommandsSessionAudioRecord.cpp:142-146` (before the fix) | `withEngineHeld([&recorder] { recorder.cancel(); });` runs first, then `if (before != Idle && before != Done) { song.sessionScheduler().requestStop(...); }` — the returned `bool` is discarded and `ControlResult::success` follows unconditionally |
| E2 | `requestStop` can be refused | `include/SessionScheduler.h:240-242`, `src/core/SessionScheduler.cpp:235-266` | `requestStop` is `enqueue(Command{...})`; `enqueue` returns false and does `m_dropped.fetch_add(1)` when `m_queue.push` fails |
| E3 | A refused stop leaves the slot playing | `src/core/SessionScheduler.cpp:235-266` (nothing queued), `:456-473` (`trackIsSessionActive`) | With no `Stop` command, the slot stays `Playing`; the take's recorder was already cancelled by E1, so nothing can end the cell on the cancel path |
| E4 | The sibling that *does* check the same push | `src/core/ControlCommandsSessionAudioRecord.cpp:214` (both before and after) | `armAndLaunch` checks the queue push for the launch and refuses with `Busy` + "the launch queue is full; nothing was armed" — the same shape the cancel path now uses |
| E5 | The fix | `src/core/ControlCommandsSessionAudioRecord.cpp:136-162` | `stopQueued = !stopsCell || scheduler.requestStop(...)` and `if (stopQueued) { recorder.cancel(); }` inside one `withEngineHeld`; `if (!stopQueued)` returns `ControlResult::failure(ControlErrorKind::Busy, ...)` naming the cell and that the take is still in hand |
| E6 | Realtime constraint | `src/core/ControlCommandsSessionAudioRecord.cpp:147-152` | The added work is one lock-free queue push (`SessionScheduler::CommandQueue::push`, no allocation, `noexcept`) plus a `bool` on the model thread. Nothing new runs on the audio thread; `cancel()`'s engine hold is unchanged |

Why the order also matters: `withEngineHeld` holds the audio engine's model-change lock, so no period
renders between the push and `recorder.cancel()`. The cancel is therefore all-or-nothing — either the
stop is queued and the take dropped, or (queue full) neither happened and the caller can retry. Doing
the push first *outside* the hold would leave a window in which the audio thread could fire the stop on
a recorder that this call is about to cancel.

## Test matrix

| Case | Driven by | Expected / observed |
|---|---|---|
| Normal path: cancel a live take | `SessionAudioRecordTest::theSurfaceRefusesWhatItCannotRecord` (existing) | `ok`, `cancelled: true`, recorder `idle` — unchanged, passes (E7) |
| Failure / exhaustion: cancel while the command queue is full | new `aCancelWhoseStopCannotBeQueuedKeepsTheTakeInHand` | `ControlErrorKind::Busy`, `droppedCommands()` +1, recorder still `recording`, `trackIsSessionActive(m_column)` still true — fails on the unfixed tree at the error-kind line |
| Reconfiguration / recovery: cancel again after the audio thread drains | same test, second half | `ok`, `cancelled: true`, recorder `idle`, and the cell stops on its own bar line (`!trackIsSessionActive`) |
| No take in hand (`Idle`) or finished (`Done`) | existing `theSurfaceRefusesWhatItCannotRecord`, `aFailedTakeWriteKeepsTheTakeInHandForARetry` | no stop is needed, so no push and no new refusal — behaviour identical to before |
| Audio-thread constraints | design (E6) | one lock-free push on the model thread; no allocation, lock, wait or log added on the audio path |
| Supported-platform build | `zene` + the five session test targets linked on this box (Linux, Qt 6.12) | build logs below; no platform-specific code touched |

Determinism: the test never sleeps or waits on wall-clock time. The fixture stops the dummy device
(`Engine::audioEngine()->audioDev()->stopProcessing()` in `initTestCase`), so `period()` is the only
producer of audio periods; the queue is filled with `requestStop(999, 0, ...)` in a loop bounded by
`commandQueueSpace()`, which is exact while no consumer runs, and exhaustion is asserted through the
deterministic `droppedCommands()` counter, not through timing.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p16-32.r1/logs`.

```
# RED (test first, unfixed product)
cmake --build build --target SessionAudioRecordTest -j4 > logs/build-1.log 2>&1            EXIT=0
cd build/tests && ctest -R '^SessionAudioRecordTest$' --output-on-failure > logs/test-1-red.log 2>&1
                                                                                          EXIT=8
  FAIL!  : ...aCancelWhoseStopCannotBeQueuedKeepsTheTakeInHand() Compared values are not the same
     Actual   (refused.errorKind)     : 0
     Expected (ControlErrorKind::Busy): 4
  Totals: 17 passed, 1 failed
  -> the unfixed cancel answered success (0 = Ok) although its stop was refused.

# GREEN (fix in place)
cmake --build build --target SessionAudioRecordTest zene -j4 > logs/build-2-green.log 2>&1   EXIT=0
cmake --build build --target SessionAudioRecordTest -j4 > logs/build-4-green.log 2>&1        EXIT=0
cd build/tests && ctest -R '^SessionAudioRecordTest$' --output-on-failure > logs/test-4-green.log 2>&1
                                                                                          EXIT=0
  1/1 Test #705: SessionAudioRecordTest ...........   Passed    0.97 sec
  100% tests passed out of 1

# family
cmake --build build --target SessionAudioRecorderTest SessionAudioCellPlaybackTest \
    ControlSessionRecordLandTest SessionGridRecordTest -j4 > logs/build-6-family.log 2>&1   EXIT=0
cd build/tests && ctest -R '^(SessionAudioRecordTest|SessionAudioRecorderTest|SessionAudioCellPlaybackTest|ControlSessionRecordLandTest|SessionGridRecordTest)$' \
    --output-on-failure > logs/test-7-family.log 2>&1                                      EXIT=0
  100% tests passed out of 5

# 4b: prove the test catches the bug
git diff -- src/core/ControlCommandsSessionAudioRecord.cpp > logs/fix.patch                 EXIT=0
git apply -R logs/fix.patch                                                                EXIT=0
cmake --build build --target SessionAudioRecordTest -j4 > logs/build-8-reverted.log 2>&1    EXIT=0
cd build/tests && ctest -R '^SessionAudioRecordTest$' --output-on-failure > logs/test-9-red-without-fix.log 2>&1
                                                                                          EXIT=8
  FAIL!  : ...aCancelWhoseStopCannotBeQueuedKeepsTheTakeInHand() Compared values are not the same
     Actual   (refused.errorKind)     : 0 / Expected (ControlErrorKind::Busy): 4
git apply logs/fix.patch                                                                   EXIT=0
cmake --build build --target SessionAudioRecordTest -j4 > logs/build-10-reapplied.log 2>&1  EXIT=0
cd build/tests && ctest -R '^SessionAudioRecordTest$' --output-on-failure > logs/test-11-green-with-fix.log 2>&1
                                                                                          EXIT=0
cmake --build build --target zene -j4 > logs/build-12-zene.log 2>&1                        EXIT=0

# gates
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > logs/gate-1.log 2>&1
                                                                                          EXIT=0
  PASS fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace /
       no-tautology / doc-figures / complexity / file-length      RESULT: PASS
```

The first GREEN run of the new test (`logs/test-2-green.log`, EXIT=8, and the diagnostic run
`logs/test-3-diag.log`) failed on my own last assertion, not on the fix: it expected the cell to stop on
the next audio period, but `SessionScheduler::applyCommand` applies a `Stop` with the *slot's stored*
quantisation (`src/core/SessionScheduler.cpp:377`), so the cell stops on its next bar line. The test
now drives periods to that line (bounded loop, no wall clock) — see the recovery row of the matrix.

## Self-check

- **Is the defect real?** Yes, E1-E3: the pre-fix code discarded `requestStop`'s answer and reported
  success; `logs/test-1-red.log` shows the unfixed command answering `Ok` for a cancel whose stop was
  dropped.
- **Does the test fail without the fix and pass with it?** Yes, both directions were run this session:
  `logs/test-9-red-without-fix.log` EXIT=8 with the product patch reverted, and
  `logs/test-11-green-with-fix.log` EXIT=0 with it applied.
- **Is a gate or test weakened?** No. `git diff` touches one product file and one existing test file; no
  assertion, baseline or limit changed. `feature_check.py`: RESULT PASS, EXIT=0.
- **Undo / commands:** unchanged — the cancel is not a project edit and still records no transaction; no
  control command was added, so the `ControlCommandsSnapshot` canary is untouched.
- **Realtime:** nothing added on the audio thread (E6); `requestStop` is a lock-free, allocation-free
  push, and it runs on the model thread inside the existing engine hold.
- **Collateral:** `state`, `cancelled`, `quantisation` etc. in the success result are unchanged; only the
  queue-full case, which previously lied, now refuses. `Idle` and `Done` cancels behave exactly as
  before (no push at all).
- **Sibling consistency:** the launcher's own `armAndLaunch` already refuses on a full queue (E4); the
  cancel path now matches it.

## Other findings

- `session.stop_slot` (`src/core/ControlCommandsSessionLaunch.cpp:271-276`) has the same shape of
  defect: it discards `requestStop`'s `bool` and always answers `stop_requested: true`, so a full queue
  makes it report a stop that was dropped. It is a different command and outside this job's finding, so
  it is left unchanged — flagging it for its own ticket. Also note `SessionScheduler::applyCommand`
  applies a `Stop` with the slot's stored quantisation, so `stop_slot`'s `quantisation` argument does not
  decide when the stop lands.
