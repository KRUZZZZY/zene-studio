FIXED

# new-p12-118: Raw MIDI output can block the audio thread

Verdict: REAL, fixed. `MidiAlsaRaw::sendByte()` called `snd_rawmidi_write()` on the calling thread,
and that thread is the audio thread for a rendered instrument. The other raw clients already avoid
this (`MidiOss::sendByte`, `src/core/midi/MidiOss.cpp:191`, and `MidiSndio::sendByte`,
`src/core/midi/MidiSndio.cpp:160`, only ever write into a non-blocking pipe that a dedicated writer
drains), so this client was the last one writing the device from the render path.

## Evidence (line numbers after the fix)

The output path really is the render path:

- `src/core/midi/MidiPort.cpp:222` - `m_midiClient->processOutEventAt( outEvent, time, this, offset )`,
  reached from `src/tracks/InstrumentTrack.cpp:720` (`m_midiPort.processOutEvent( event, time, offset )`),
  i.e. from `InstrumentTrackNotes`' note-on/note-off inside a render period.
- `include/MidiClient.h:66` - the base `processOutEventAt()` falls through to `processOutEvent()`, and
  `src/core/midi/MidiClient.cpp:398` (`MidiClientRaw::processOutEvent`) encodes each event by calling
  the client's `sendByte()` two or three times.

The defect (before this change):

- `src/core/midi/MidiAlsaRaw.cpp:196-201` (old) - `sendByte()` did
  `snd_rawmidi_write( m_output, &c, sizeof( c ) )` with `m_output` opened blocking:
  `snd_rawmidi_open(..., 0)` at line 66 (old) and only `snd_rawmidi_nonblock( m_input, 1 )` at line 76
  (old) - the output handle was never made non-blocking. A full or stalled device buffer therefore
  blocked the caller for as long as the device took, inside the audio period. The return value was
  ignored, so a refused byte was silently lost as well.

The fix, at the cause:

- `src/core/midi/MidiAlsaRaw.cpp:242` - new `MidiAlsaRaw::processOutEvent()` is the audio path's output:
  it encodes the event on the calling thread (`MidiClientRaw::processOutEvent`, whose bytes go to
  `sendByte()`) and pushes the whole message into a bounded, lock-free queue
  (`MpscQueue<OutMessage, 1024>`, `include/MidiAlsaRaw.h:139`). A full queue is refused and counted
  (`m_outDropped`, `include/MidiAlsaRaw.h:140`) instead of waiting, and
  `outputQueueOverflowCount()` (`include/MidiAlsaRaw.h:67`) reports it, as the sibling clients do.
- `src/core/midi/MidiAlsaRaw.cpp:232` - `sendByte()` only collects the current thread's message
  (`thread_local t_outBytes`, `src/core/midi/MidiAlsaRaw.cpp:63`); it no longer touches the device.
  This mirrors `MidiJack::sendByte` (`src/core/midi/MidiJack.cpp:245`), so two senders never interleave
  bytes in the stream.
- `src/core/midi/MidiAlsaRaw.cpp:292` `drainOutQueue()` and `:310` `writePending()` - the raw worker
  (the only writer of the device) drains the queue, and a message the device accepts only partly is
  retained and resumed, so no message is ever split by the next one.
- `src/core/midi/MidiAlsaRaw.cpp:107` - the output handle is now non-blocking, so that drain can never
  block the worker either; `-EAGAIN` means "keep the rest", any other error drops the message (the old
  behaviour, which ignored errors entirely).
- `src/core/midi/MidiAlsaRaw.cpp:270` `requestWake()` / `:284` `acknowledgeWake()` - one non-blocking
  wake byte on the existing self-pipe per drain (not per event), the same protocol `MidiAlsaSeq` uses
  (`src/core/midi/MidiAlsaSeq.cpp:374`), so a joined message is written within microseconds and the
  pipe never stays readable.
- `src/core/midi/MidiAlsaRaw.cpp:341` - the worker acknowledges before draining at the top of its loop
  and consumes the wake byte after `poll()`; the poll timeout is 5 ms while a partly written message is
  pending and the old 10 s while idle (`:361`).
- `src/core/midi/MidiAlsaRaw.cpp:154` - the destructor drains what is still queued after the join, so
  output produced during shutdown is delayed, never lost. `stopInput()` still joins the worker; output
  queued after it waits for that final drain (documented in the header at `include/MidiAlsaRaw.h:117`).
- `include/MidiClient.h:258` - the `Q050-277` note no longer claims MidiAlsaRaw writes "one
  `snd_rawmidi_write()` of one byte" on the calling thread; it now names its multi-producer queue.

Callers and siblings checked, kept consistent:

- The other path into the same function: `MidiClientRaw::processNonAudioOutEvent` (the MIDI clock's
  capture sender, `src/core/midi/MidiClient.cpp:68`) calls `processOutEvent()`, so its one-byte System
  Real-Time message now goes through the same queue. Because the queue is multi-producer (`MpscQueue`,
  not the single-producer `MidiOutQueue` that `MidiAlsaSeq` uses with a second queue), that second
  producer is safe here and needs no separate queue.
- The wider type the value flows into: `MidiPort::processOutEvent` is also called from the GUI
  (`sendOutputPatch()`, bank select + program change), so the queue had to be multi-producer; a
  single-producer queue would have raced the audio thread.
- `sendByte` is declared and defined together (`include/MidiAlsaRaw.h:86`,
  `src/core/midi/MidiAlsaRaw.cpp:232`), and the header change was followed by rebuilding every
  affected target: `zene`, `MidiAlsaRawShutdownTest` and the rest of the MIDI family below.
- Undo, commands, project data: untouched. No allocation, free, lock, wait or log was added to the
  audio path: the push is one compare-exchange, the wake is at most one non-blocking `write()` of one
  byte, and the refusal path is a relaxed increment - all asserted by the allocation probe in the test.

## Test matrix

Recorded before editing (scratchpad `matrix.md`).

| Path | Test | Assertion |
| --- | --- | --- |
| normal | `audioPathQueuesOutputInsteadOfWritingIt` | from the audio-path thread: 0 allocations, 0 frees, the device was **not** written by that thread, and the worker then writes `0x90 60 100` in order; `outputQueueOverflowCount() == 0` |
| failure (device buffer full) | `fullDeviceBufferKeepsTheMessageForTheWorker` | the device refuses the first write with `-EAGAIN`; the message is kept and re-offered whole, all three bytes arrive in order |
| exhaustion | `queueOverflowIsCountedAndTheAudioPathNeverBlocks` | with the worker joined, 4096 pushes past the 1024-message bound: refused and counted, the caller never waits |
| reconfiguration / no device | `sendByteStaysAllocationFree`, `failedOpenStartsNothingAndDropsOutput`, `partialStartupFailureReleasesOnce` | a client with no output handle writes nothing and drops the message, no crash, no closed-handle call |
| audio-thread rules | the two above + allocation probe | 0 allocations and 0 frees on the calling thread |
| platform | whole file inside `LMMS_HAVE_ALSA`; registered only for ALSA builds (`tests/CMakeLists.txt:1933`) | builds and runs on this Linux/ALSA box |

Concurrency reasoning (local TSan does not run on this box, so this is reasoned, not measured):
producers are the audio thread (`MidiPort::processOutEvent` from the render period) and the main
thread (`MidiPort::sendOutputPatch`, pitch/pitch-range changes); the queue is `MpscQueue`, which is
multi-producer and single-consumer by construction, and the consumer is the raw worker alone (and the
destructor after the join, when no producer can still be the worker). `m_writing`/`m_written` have the
worker as their only writer. `m_wakeRequested` is exchanged before the wake byte is written and
cleared by the consumer before it drains, so a wake can be lost only if the queue is also empty,
which the drain at the top of the loop covers.

The one existing test whose premise the bug invalidated is
`MidiAlsaRawShutdownTest::sendByteStaysAllocationFree` ("The audio-thread entry point ... still hands
the byte to ALSA directly"): it asserted `g_writeCalls == 2` after two `sendByte()` calls. That is the
defect itself, so the fix changes the expectation to "the audio path wrote nothing" while keeping, not
weakening, its allocation and free checks; the delivery half is now asserted in
`audioPathQueuesOutputInsteadOfWritingIt`. Nothing else in that file changed except the fake device
(recording bytes, the writer's thread, a bounded `-EAGAIN` mode) and its reset.

## Commands and results (all from this session)

| Step | Command | Result |
| --- | --- | --- |
| baseline build | `cmake --build build --target zene MidiOutQueueTest -j4 > .../logs/build-1-baseline-test-target.log 2>&1; echo EXIT=$?` | EXIT=0 |
| baseline run | `ctest -R MidiOutQueue --output-on-failure > .../logs/test-2-baseline.log` (from `build/tests`) | EXIT=0, 1/1 passed |
| RED build (tests only) | `cmake --build build --target MidiAlsaRawShutdownTest -j4 > .../logs/build-2-red.log` | EXIT=0 |
| RED run | `ctest -R MidiAlsaRawShutdown --output-on-failure > .../logs/test-3-red.log` | **EXIT=8**, 11 passed, 4 failed (`sendByteStaysAllocationFree`, `audioPathQueuesOutputInsteadOfWritingIt`, `fullDeviceBufferKeepsTheMessageForTheWorker`, `queueOverflowIsCountedAndTheAudioPathNeverBlocks`) |
| GREEN build | `cmake --build build --target zene MidiAlsaRawShutdownTest -j4 > .../logs/build-3-green.log` | EXIT=0 |
| GREEN run | `ctest -R MidiAlsaRawShutdown --output-on-failure > .../logs/test-4-green.log` | EXIT=0, 1/1 passed |
| GREEN slots | `./MidiAlsaRawShutdownTest > .../logs/test-5-green-slots.log` (from `build/tests`) | EXIT=0, 15 passed, 0 failed |
| family build | `cmake --build build -j4 --target MidiOutQueueTest MidiAlsaSeqShutdownTest MidiClockCaptureOutputTest MidiClockSourceTest MidiClockTest MidiOssTest MidiSndioTest MidiJackOutScheduleTest MidiReconnectTest RetroMidiCaptureCommandsTest ControlMidiReconnectSharedTest > .../logs/build-4-family.log` | EXIT=0 |
| family run | `ctest -R "Midi\|RetroMidi" --output-on-failure > .../logs/test-6-family.log` | EXIT=8 - 16 built members passed, 34 matched targets are **Not Run** because they are not built in this tree (they were never built here; none of them failed) |
| more of the family built | `cmake --build build -j4 --target ControllerScriptTest ExternalMidiDeviceTest > .../logs/build-5-family2.log` | EXIT=0 |
| more of the family run | `ctest -R "ControllerScriptTest\|ExternalMidiDeviceTest" --output-on-failure > .../logs/test-7-family2.log` | EXIT=0, 2/2 passed |
| 4b revert | `git diff -- include/MidiAlsaRaw.h src/core/midi/MidiAlsaRaw.cpp include/MidiClient.h > .../logs/fix.patch; git apply -R .../logs/fix.patch` | both EXIT=0 |
| 4b rebuild (product reverted) | `cmake --build build -j4 --target zene MidiAlsaRawShutdownTest > .../logs/build-6-reverted.log` | EXIT=0 |
| 4b run (product reverted) | `ctest -R MidiAlsaRawShutdown --output-on-failure > .../logs/test-8-reverted-red.log` | **EXIT=8**, the same 4 tests fail |
| 4b restore | `git apply .../logs/fix.patch` | EXIT=0 |
| 4b rebuild (fix back) | `cmake --build build -j4 --target zene MidiAlsaRawShutdownTest > .../logs/build-7-reapplied.log` | EXIT=0 |
| 4b run (fix back) | `ctest -R MidiAlsaRawShutdown --output-on-failure > .../logs/test-9-reapplied-green.log` | EXIT=0, 1/1 passed |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-1.log 2>&1` | EXIT=0, `RESULT: PASS` (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length) |

The first baseline attempt (`.../logs/test-1-baseline.log`, EXIT=8) is recorded too: the test binary did
not exist yet in this build tree ("Unable to find executable"), which is why the target was built first.

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-118.r1/logs`.

## Files changed

- `src/core/midi/MidiAlsaRaw.cpp`, `include/MidiAlsaRaw.h` - the fix.
- `include/MidiClient.h` - one comment corrected (no code change).
- `tests/src/core/MidiAlsaRawShutdownTest.cpp` - the regression tests and the fake device they need.
- `tests/upstream-modifications.txt` - reasons for the three inherited files (all three were already
  registered; the entries for this change were added). No new product file, so `tests/fork-sources.txt`
  is unchanged.

## Other findings (not fixed here)

- `MidiAlsaSeq`'s audio-path queue is a single-producer `MidiOutQueue` (`include/MidiAlsaSeq.h:222`),
  but `MidiPort::processOutEvent` is also called from the main thread (`sendOutputPatch()`, bank select
  and program change), so that queue can have two producers. My fix used the multi-producer
  `MpscQueue` for exactly this reason; the sibling is left alone because it is a separate finding.
- `MidiOss`/`MidiSndio` are retired (Q050-479) and still stop only in their destructors, though their
  output path is already non-blocking.

## Self-check

Answers to the completion check's ASK items, each with the evidence it asks for.

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Proven
  in step 4b, twice: `git diff -- include/MidiAlsaRaw.h src/core/midi/MidiAlsaRaw.cpp include/MidiClient.h
  > .../logs/fix.patch` (EXIT=0) then `git apply -R .../logs/fix.patch` (EXIT=0), rebuild (EXIT=0,
  `.../logs/build-6-reverted.log`), run -> **EXIT=8** with the four tests failing
  (`.../logs/test-8-reverted-red.log`); then `git apply .../logs/fix.patch` (EXIT=0), rebuild (EXIT=0,
  `.../logs/build-7-reapplied.log`), run -> **EXIT=0** (`.../logs/test-9-reapplied-green.log`). The same
  four tests also failed in the first, tests-only RED run (`.../logs/test-3-red.log`, EXIT=8) before any
  product edit existed.
- **The test is deterministic: no wall-clock timing, sleeps or races.** No `sleep`, no `QTest::qWait`
  and no elapsed-time assertion is used by the four tests. The seam is the fake ALSA layer that already
  exists in `tests/src/core/MidiAlsaRawShutdownTest.cpp` (the executable's `extern "C"`
  `snd_rawmidi_*` definitions win over libasound, the technique of `MidiOutQueueTest` and
  `AudioAlsaPollDescriptorTest`): the "device" is a pipe, its write stub records the bytes, the thread
  that wrote them, and can refuse a bounded number of writes with `-EAGAIN`. Delivery is awaited through
  `g_writeSignal.tryAcquire(1, 5000)` - a `QSemaphore` released by the write stub, the same
  synchronisation hook the file already uses for its gated port - so the timeout only bounds a failure
  and no assertion depends on how long anything took. The RED direction is a synchronous fact, not a
  race: the unfixed `sendByte()` writes before `processOutEvent()` returns.
- **The fix is at the cause and complete; every caller and sibling checked.** Callers: the audio path
  (`MidiPort.cpp:222` <- `InstrumentTrack.cpp:720`), the GUI patch path (`MidiPort::sendOutputPatch`),
  the base `MidiClient::processOutEventAt` fall-through (`include/MidiClient.h:66`), and the second
  thread's `MidiClientRaw::processNonAudioOutEvent` (`src/core/midi/MidiClient.cpp:68`) - all of them
  now reach the queue, which is multi-producer for exactly that reason. Siblings kept consistent:
  `MidiJack::sendByte`/`processOutEventAt` (`src/core/midi/MidiJack.cpp:226-249`, the thread-local
  assembly + `MpscQueue` + dropped counter idiom copied here), `MidiAlsaSeq::enqueueOutEvent`
  (`src/core/midi/MidiAlsaSeq.cpp:374`, the wake-pipe protocol copied here), `MidiOss`/`MidiSndio`
  (already non-blocking pipes with a writer). `outputQueueOverflowCount()` is overridden as the sibling
  does, and `include/MidiClient.h:258` no longer describes the removed direct write. Declaration and
  definition changed together, and every target that includes the headers was rebuilt (`zene` and the
  MIDI family in the table above).
- **No collateral damage.** No file format, project field, model or default changed, so saved projects
  load exactly as before; the only persistence-adjacent member is a queue that lives and dies with the
  client. The limits added are on the new hand-off only: `OutputQueueCapacity` (1024 messages) refuses
  and counts past-full pushes, so no other caller is affected by it, and no existing check or limit was
  changed or re-anchored. Behaviour outside the bug is preserved: the same bytes in the same order, the
  same encoding (`MidiClientRaw::processOutEvent` untouched), a missing device still drops output
  without a crash, and shutdown still joins the worker within the old bound (destructor plus one drain).
  The one deliberate behaviour change is the added latency of at most one 5 ms worker pacing sleep, the
  price of not writing from the render path; it is documented in the file header comment.
- **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not claimed: the verdict is FIXED, and the defect was
  reproduced before the change (the four failing tests above show bytes reaching the device from the
  audio-path thread, a lost `-EAGAIN` byte and no overflow counter).
- **Sibling commands in the command files changed.** No command file was changed: this fix adds no
  control command and touches nothing under `src/core/Control*` or `src/gui`, so the shell/command
  siblings listed (`app.*`, `arrangement.*`) and the `ControlCommandsSnapshot` canary are untouched; the
  completion check's `commits on the branch`, `working tree clean`, `gates` and
  `new test files registered` rows all PASS (`.../logs/submit-1.log`).

