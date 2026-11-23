FIXED

# Cluster 3978: Recovery discard can remove a live capture's journal

## Verdict: REAL

`record.recovery_discard` read any journal and removed it without asking whether the journal
still belonged to a capture this process was writing. `recoveryRestore` already refuses a journal
that is not an interrupted capture (`src/core/ControlCommandsRecordingRecovery.cpp:95-110`), but
the live/aborted distinction is not `isRecoverable()`: a live capture's journal has the same
`state=in_progress` and the same existing take as a crashed one, because
`TrackRecorder::writeJournal()` writes exactly those fields
(`src/core/audio/TrackRecorder.cpp:280-290`) and only refreshes them on the disk-writer's next
commit (`src/core/audio/TrackRecorder.cpp:426-432`). Before this fix, `recoveryDiscard`
(`src/core/ControlCommandsRecordingRecovery.cpp:165`) called `remove()` directly. Discarding a
take that was still recording therefore deleted the journal; if the process died before the
writer's next commit, the next start had no offer for material that had already reached disk.

The earlier SKIPPED-SCOPE note said no live-capture state was reachable from the command layer.
That is not true: `Engine::audioEngine()->recorder()` exposes every `TrackRecorder`
(`include/AudioEngine.h:420-421`), and each route reports its journal path and liveness
(`include/TrackRecorder.h:106-150`). The command layer already reads the same object through
`control::routeJson()` (`src/core/ControlRecordingSupport.cpp:196-210`).

## Fix

`src/core/ControlCommandsRecordingRecovery.cpp:143-162` adds `isLiveCaptureTake()`: it walks the
engine's record routes (bounded by `MultiTrackRecorder::MaxRoutes`, 64) and returns true when a
route's non-empty journal path is the take's journal and the route is armed or its writer is
still running. `recoveryDiscard` (`:170-178`) refuses such a journal with a typed `busy` refusal
naming `record.disarm_track` / `record.disarm_all`, before it builds a transaction or removes
anything. Only the control thread runs the handler; the reads are `isArmed()` /
`isWriterRunning()` atomics plus a copy of `m_journalPath`, which is set before the writer starts
and cleared after it joins (`include/TrackRecorder.h:201-213`). No audio-path code changed.

Nothing else changed: command ids, schemas, result keys, transaction class and the WAV-kept
fallback are as they were. A journal left by a crashed process has no live route and is
discarded exactly as before.

## Regression test

`tests/src/gui/RecordingRecoveryMenuTest.cpp:337` (`aLiveCapturesJournalIsNotDiscarded`) arms a
real engine record route on a temp take, verifies its journal is offered by
`record.recovery_get_state`, then calls `record.recovery_discard`. It must be refused `busy`, the
journal must still exist and still read `state=in_progress`. It then disarms (clean stop retires
the live journal), writes an ordinary in-progress journal with `record.journal_begin`, and
verifies discard still succeeds and removes that journal. No sleeps or wall-clock values: the
route is armed and disarmed by method calls and the journal state is read from disk.

## Red / green proof

Product change reverted with the test kept (`fix.patch` holds only
`src/core/ControlCommandsRecordingRecovery.cpp`):

```
git diff -- src/core/ControlCommandsRecordingRecovery.cpp > .../logs/fix.patch   EXIT=0
git apply -R .../logs/fix.patch                                                  EXIT=0
cmake --build build --target zene RecordingRecoveryMenuTest -j4                  EXIT=0   (build-4-red-without-fix.log)
cd build/tests && ctest -R '^RecordingRecoveryMenuTest$' --output-on-failure     EXIT=8   (test-4-red-without-fix.log)
  FAIL!  : aLiveCapturesJournalIsNotDiscarded() '!discarded.ok' returned FALSE.
           (record.recovery_discard removed a live capture's journal)
git apply .../logs/fix.patch                                                     EXIT=0
cmake --build build --target zene RecordingRecoveryMenuTest -j4                  EXIT=0   (build-5-green-with-fix.log)
cd build/tests && ctest -R '^RecordingRecoveryMenuTest$' --output-on-failure     EXIT=0   (test-5-green-with-fix.log)
```

The same test also failed on the first RED run before the product change existed
(`test-2.log`, EXIT=8), and the baseline suite passed before the test was added (`test-1.log`,
EXIT=0).

## Family and gates

```
cd build/tests && ctest -R '^(RecordingRecoveryMenuTest|MultiTrackRecorderTest|ControlRecordingRoutesTest)$' \
    --output-on-failure                                                          EXIT=0   (test-6-family.log, 3/3 passed)
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>           EXIT=0   (gate-1.log, RESULT: PASS)
```

## Test matrix

- Normal path: a journal written by `record.journal_begin` has no live route and is discarded,
  journal gone, WAV kept (covered by the new test's second half and every pre-existing test in
  the file).
- Failure path: a live capture's journal is refused `busy`, the journal and its
  `state=in_progress` survive; the refusal happens before any transaction or file operation.
- Exhaustion: no new limit or unbounded scan. `isLiveCaptureTake` walks at most
  `MultiTrackRecorder::MaxRoutes` (64) routes on the control thread.
- Reconfiguration: after `disarm()` the route's journal path is cleared, so the same take's
  in-progress journal is discardable again (asserted). A route whose input became unavailable is
  still protected while `isWriterRunning()` is true; a failed disarm that leaves the path behind
  with the writer stopped is not live and stays discardable as before.
- Audio thread: unchanged. The handler is control-thread code; `processInput` /
  `processInputInterleaved` / `writerLoop` were not touched.
- Platforms: only `QString`, `std::string`, `Engine::audioEngine()` and existing recorder
  accessors; no POSIX-only path test, no Qt-6-only API. `RecordingRecoveryMenuTest` is already
  registered and runs offscreen.

## Self-check

- Regression test FAILS with the product change reverted and PASSES with it: `test-4` EXIT=8
  (reverted) and `test-5` EXIT=0 (restored), commands above, `fix.patch` contains only the
  product file.
- Deterministic: no timing. The route is armed by `armTrack()`, the refusal is a direct command
  call, and the journal state is read from the file. No sleeps or races are used.
- Fix at the cause and complete; siblings: `recovery_get_state` is read-only;
  `recovery_restore` (`src/core/ControlCommandsRecordingRecovery.cpp:95-110`) and
  `recovery_place` (`src/core/ControlCommandsRecordingPlace.cpp:79-99`) REWRITE the journal to
  `restored` rather than deleting it, and the live writer's next commit
  (`TrackRecorder::commitTake`, `src/core/audio/TrackRecorder.cpp:262-278`) republishes
  `in_progress`; they do not have this finding's permanent-removal defect. The live writer's own
  `writeJournal()` and `disarm()` paths were not changed. The broader fact that
  `record.recovery_get_state` lists a live capture is noted under Other findings and was not
  changed, to keep this fix to the reported removal.
- No collateral damage: no schema, id, result key or transaction class changed. Crashed-process
  journals (no live route) are still discarded; saved data and project loading are untouched;
  no limit was added that another caller could exceed.
- Undo/commands: no control command was added. A refused discard records no transaction and
  leaves the undo stack unchanged; a successful discard is still the documented irreversible
  step with the WAV fallback.
- Realtime: the added code runs only in the control-thread command handler and allocates no
  audio-path work; no lock, wait, log or I/O was added to `TrackRecorder` or any render path.

## Other findings

- `record.recovery_get_state` still offers a live capture (it satisfies `isRecoverable()`), so
  File > Recover Recordings can show a take that is still recording. That is the source of the
  mismatch this fix guards at the destructive verb; changing the scan would change recovery
  listing behaviour beyond this finding and is left for a separate unit.
- `record.recovery_restore` / `record.recovery_place` on a live capture rewrite the journal while
  the writer is still appending. They do not delete it, so the offer returns at the next commit,
  but a crash in that window is a separate defect of the same family.
