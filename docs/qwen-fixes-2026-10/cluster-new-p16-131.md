VERDICT: FIXED
new-p16-131: FIXED
new-p9-270: ALREADY-FIXED
new-p9-281: FIXED

Round 2 (Luna's REWORK): the clock and sndio paths were already queued off the caller
thread; the OSS writer could outlive its client, and the destructor then closed the
descriptor the writer was still using. That descriptor ownership defect is what this
round fixes, once, in the place every raw MIDI output goes through.

THE SHARED CAUSE (one defect, three reports)
MIDI output device I/O belongs to the output worker, never to the caller (the audio
thread). The raw clients that had been fixed (ALSA-sequencer, sndio) hand the bytes to
a worker that OWNS the device; the OSS client handed the bytes to a worker that did NOT
own the device - it kept only the descriptor NUMBER, while the destructor kept the
ownership and closed it. A teardown that abandoned a stalled write therefore closed a
descriptor the writer was still writing to: the queued bytes could be lost, or land on a
descriptor the operating system had already reused. The fix gives the writer the
descriptor for as long as it may write, and closes it only after the writer has drained
or returned - exactly the shape the sndio writer already had
(src/core/midi/MidiSndio.cpp:175-203: the writer owns the handle and closes it).

## new-p16-131: FIXED
Evidence:
- src/core/midi/MidiOss.cpp:45-57 - OutputWriter now OWNS the device descriptor; the
  comment states the ownership rule.
- src/core/midi/MidiOss.cpp:127-160 (destructor) - the writer branch no longer closes
  the descriptor: it closes the queue's write end, waits for the writer, joins it when
  it finished and detaches it when it is stalled, and then hands the descriptor over
  (m_fd = -1). The descriptor is closed by the destructor only in the else-branch, where
  no writer ever took it.
- src/core/midi/MidiOss.cpp:236-241 (runWriter) - the writer closes the descriptor
  itself, after it has written everything it was given, and only then sets its promise.
- src/core/midi/MidiOss.cpp:170-178 - sendByte() still only queues into the non-blocking
  pipe: the caller thread never touches the device (the original report).
- Regression: tests/src/core/MidiOssTest.cpp
  stalledWriteTeardownKeepsTheDescriptorUntilTheWriterFinishes - a deterministic
  stalled-write teardown: the fake device write blocks on a QSemaphore gate, the
  teardown gives up on the stalled writer after the grace period, and the test then
  proves the descriptor was NOT closed under the write and IS closed exactly once, after
  the writer delivered the bytes queued behind it. No sleeps, no wall-clock timing.
Commands (logs in .../runs/new-p16-131.r2/logs):
- RED (product unchanged, test added):
  `cmake --build build --target MidiOssTest` EXIT=0 (build-red-1.log)
  `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^MidiOssTest$' --output-on-failure` EXIT=8 (test-red-1.log)
  -> FAIL stalledWriteTeardownKeepsTheDescriptorUntilTheWriterFinishes: Actual (closeCalls) 1, Expected 0
- GREEN:
  `cmake --build build --target zene MidiOssTest` EXIT=0 (build-green-1.log)
  `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^MidiOssTest$' --output-on-failure` EXIT=0 (test-green-1.log)
- 4b REVERSAL PROOF:
  `git diff -- src/core/midi/MidiOss.cpp > .../logs/fix.patch` (60 lines)
  `git apply -R .../logs/fix.patch` EXIT=0
  `cmake --build build --target zene MidiOssTest` EXIT=0 (build-rev-1.log)
  `ctest -R '^MidiOssTest$'` EXIT=8 (test-rev-1.log) -> same FAIL (closeCalls 1 vs 0)
  `git apply .../logs/fix.patch` EXIT=0
  `cmake --build build --target zene MidiOssTest` EXIT=0 (build-reapply-1.log)
  `ctest -R '^MidiOssTest$'` EXIT=0 (test-reapply-1.log)

## new-p9-270: ALREADY-FIXED
Evidence (the named failure mode - a contended sequencer mutex on the audio thread - is
already fixed in this tree, by upstream commit d259e28ae "move ALSA output off realtime
path" and the cluster-5129 wake-pipe follow-up; this round only locks it in with a
regression and corrects a stale comment):
- src/core/midi/MidiAlsaSeq.cpp:298-311 - the audio-thread processOutEvent() pushes onto
  the bounded lock-free MidiOutQueue and writes ONE byte to the non-blocking wake pipe.
  It takes no mutex and makes no ALSA call.
- src/core/midi/MidiAlsaSeq.cpp:314-327 - the sequencer mutex (QMutexLocker) is taken
  only in processQueuedOutEvent(), which runs on the sequencer worker thread.
- src/core/MidiClock.cpp:462-488 - emitMessage() hands the clock event to
  MidiClient::processOutEvent; for the sequencer client that is the queue above, and for
  the raw clients on this branch it is the OSS/sndio writer (below). The audio period
  never reaches a device.
- src/core/MidiClock.cpp:21-25 - the comment claimed the sequencer sends the clock
  family "DIRECT (not on the tick queue the note path uses)"; that was false and is the
  sentence the finding quoted. It now states that the family goes through the
  sequencer's own output queue, so the audio thread touches neither the mutex nor ALSA.
- Regression: tests/src/core/MidiOutQueueTest.cpp
  processOutEventDoesNotTakeTheSequencerMutex - the sequencer mutex is held while the
  audio-thread output path runs; the path must still return. Test seam:
  include/MidiAlsaSeq.h seqMutexForTesting() (test-only, LMMS_TESTING).
Commands:
- RED (the audio path temporarily takes the mutex again, i.e. the pre-cluster-5129
  shape; the probe was NOT committed and was reverted with
  `git checkout -- src/core/midi/MidiAlsaSeq.cpp`):
  `cmake --build build --target MidiOutQueueTest` EXIT=0 (build-queue-red.log)
  `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^MidiOutQueueTest$' --output-on-failure` EXIT=8 (test-queue-red.log)
  -> FAIL processOutEventDoesNotTakeTheSequencerMutex: 'finished' returned FALSE
- GREEN (product restored):
  `cmake --build build --target MidiOutQueueTest` EXIT=0 (build-queue-green.log)
  `ctest -R '^MidiOutQueueTest$'` EXIT=0 (test-queue-green.log)
- Residual, out of scope, reported not hidden: MidiAlsaRaw::sendByte
  (src/core/midi/MidiAlsaRaw.cpp:108-111) still writes to the raw ALSA device on the
  caller thread. That is a different client from the one the finding names, it affects
  every MIDI output path and not only the clock's, and it has its own bug id; it is not
  fixed here.

## new-p9-281: FIXED
Evidence (both halves of the report - OSS and sndio - queue the bytes to an output
worker that owns the device):
- OSS: src/core/midi/MidiOss.cpp:170-178 (sendByte queues into the non-blocking pipe)
  and 194-241 (runWriter is the only thread that writes to the device, and it owns and
  closes the descriptor). The descriptor-ownership defect of the round-1 state is fixed
  as described under new-p16-131.
- sndio: src/core/midi/MidiSndio.cpp:160-170 (sendByte queues into the non-blocking
  pipe) and 175-203 (the writer owns the handle and closes it; the destructor abandons
  a stalled writer WITHOUT touching the handle - the shape the OSS client now has).
- Regressions: tests/src/core/MidiOssTest.cpp
  stalledWriteTeardownKeepsTheDescriptorUntilTheWriterFinishes (this round),
  deviceWriteRunsOffTheCallerThread and queuedBytesReachTheDeviceInSendOrder; and
  tests/src/core/MidiSndioTest.cpp deviceWriteRunsOffTheCallerThread,
  queuedBytesReachTheDeviceInSendOrder, queuedBytesAreDeliveredBeforeTeardownCloses and
  stalledWriteTeardownDoesNotCloseTheHandleUnderTheWrite.
Commands:
- `cmake --build build --target zene MidiOssTest MidiSndioTest MidiOutQueueTest MidiClockTest MidiClockSourceTest MidiReconnectTest` EXIT=0 (build-full-2.log)
- `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R 'Midi' --output-on-failure` EXIT=0 (test-midi-family-4.log) - 29 of 29 MIDI tests passed, 0 failed, 0 skipped

## Gates
- `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` EXIT=0 (gate-2.log): fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS (no new item in my files), file-length PASS (no new item in my files).
- The first run of this round was EXIT=1 (gate-1.log): check-namespace wanted the
  closing #endif of the LMMS_TESTING block in include/MidiAlsaSeq.h to name its macro
  (the nested #ifdef made the block no longer exempt from the end comment). Fixed by
  writing `#endif // LMMS_TESTING`; no gate limit or baseline was changed.

## Files changed (this round)
- src/core/midi/MidiOss.cpp - the writer owns and closes the device descriptor; the
  destructor no longer closes a descriptor a stalled writer may still be using.
- include/MidiAlsaSeq.h - the test-only seam seqMutexForTesting().
- src/core/MidiClock.cpp - the stale "DIRECT" comment about the sequencer path.
- tests/src/core/MidiOssTest.cpp - the stalled-write teardown regression.
- tests/src/core/MidiOutQueueTest.cpp - the sequencer-mutex regression.
- tests/upstream-modifications.txt - the two inherited files' entries.
- docs/qwen-fixes-2026-10/cluster-new-p16-131.md - this report.

## Self-check
1) "The regression test FAILS with your product change reverted and PASSES with it
   (exit codes)."
   - new-p16-131 / new-p9-281 (MidiOssTest): RED EXIT=8 (test-red-1.log) before the
     product change; GREEN EXIT=0 (test-green-1.log). The 4b reversal on the product
     file alone: `git apply -R .../logs/fix.patch` EXIT=0 -> build EXIT=0 ->
     `ctest -R '^MidiOssTest$'` EXIT=8 (test-rev-1.log, FAIL "Actual (closeCalls): 1,
     Expected (0)"); `git apply .../logs/fix.patch` EXIT=0 -> build EXIT=0 ->
     `ctest -R '^MidiOssTest$'` EXIT=0 (test-reapply-1.log).
   - new-p9-270 (MidiOutQueueTest): RED EXIT=8 (test-queue-red.log) with the audio path
     taking the sequencer mutex again ("'finished' returned FALSE"), GREEN EXIT=0
     (test-queue-green.log) with the product restored (`git checkout --
     src/core/midi/MidiAlsaSeq.cpp`).
2) "The test is deterministic: no wall-clock timing, sleeps or races (name the seam or
   injected values it uses)."
   - MidiOssTest: the device hooks zeneOssDeviceWrite / zeneOssDeviceClose are the
     injected seams (tests/src/core/MidiOssTest.cpp:53-74). The stall is a QSemaphore
     gate, not a timer: writeStarted is released when the fake write is entered and the
     write blocks on writeGate until the test releases it (MidiOssTest.cpp:23-31).
     The teardown grace (1000 ms) is the PRODUCT's own constant
     (src/core/midi/MidiOss.cpp:44), not a test timing assumption: the test only
     observes that the descriptor was not closed while the write is blocked. The
     device itself is a real temporary file opened through MIDIDEV.
   - MidiOutQueueTest: the seam is MidiAlsaSeq::seqMutexForTesting()
     (include/MidiAlsaSeq.h:68-75, test-only under LMMS_TESTING). The test holds the
     mutex and waits on a QSemaphore with a 5 s ceiling; the queued path returns in
     microseconds, and a mutex-taking path would block until the mutex is released -
     so the outcome does not depend on how fast the machine is.
3) "The fix is at the cause and complete: every caller and sibling of what you changed
   was checked and stays consistent."
   - The descriptor ownership lives in the one place every OSS output byte passes
     through: the writer (src/core/midi/MidiOss.cpp:194-241). Callers checked:
     the constructor's two failure paths (open failure leaves m_fd = -1, MidiOss.cpp:88-91;
     pipe failure closes the descriptor it opened and sets m_fd = -1, MidiOss.cpp:93-98);
     sendByte() (MidiOss.cpp:170-178) still never touches the device; the reader
     thread run() is stopped before the writer can close the descriptor, so the two
     never race on it.
   - Sibling checked: the sndio writer already owned its handle the same way
     (src/core/midi/MidiSndio.cpp:175-203) and its destructor never closes it
     (MidiSndio.cpp:130-148) - the OSS client now has that exact shape.
   - The clock path (new-p9-270) was traced to the same three clients: the sequencer
     client (MidiAlsaSeq.cpp:298-311, queued), OSS and sndio (queued). The stale
     "DIRECT" comment in src/core/MidiClock.cpp:21-25 was corrected.
4) "No collateral damage: old projects and saved data still load as before, and any
   limit or check you added does not hit other callers that legitimately exceed it."
   - Nothing about projects, save files, undo or the control surface was touched: no
     new control command, no save-format change, no project data touched. The only
     product behaviour change is WHO closes the OSS device descriptor, and only in the
     stalled-teardown case: a normal teardown still closes it before the destructor
     returns (the writer closes it just before it signals, the destructor joins after).
   - The new test seam is compiled only under LMMS_TESTING and is read by one test;
     it adds no limit and no check to any other caller.
5) "If your verdict is ALREADY-FIXED or FALSE-POSITIVE: the code (file:line) or the
   intended contract that shows it."
   - new-p9-270 is ALREADY-FIXED in this tree, and the code that shows it is
     src/core/midi/MidiAlsaSeq.cpp:298-311: the audio-thread processOutEvent() pushes
     onto the bounded lock-free MidiOutQueue and writes one byte to the non-blocking
     wake pipe; the sequencer mutex is taken only in processQueuedOutEvent()
     (MidiAlsaSeq.cpp:314-327), which runs on the sequencer worker thread. The
     upstream commits that made it so are d259e28ae ("move ALSA output off realtime
     path") and the cluster-5129 wake-pipe follow-up cc389e112. The intended contract
     is the same one the raw clients now follow: the audio thread never touches the
     device or a device lock. This round added the regression that proves it, rather
     than a second fix.
   - The residual that is NOT covered by that verdict is named in the report above:
     MidiAlsaRaw::sendByte (src/core/midi/MidiAlsaRaw.cpp:108-111) still writes to the
     raw ALSA device on the caller thread. That is a different client from the one the
     finding names, it affects every MIDI output path rather than the clock's alone,
     and it carries its own bug id - it is reported here, not silently folded in.

