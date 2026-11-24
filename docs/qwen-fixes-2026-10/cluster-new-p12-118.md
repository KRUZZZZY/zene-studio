FIXED

# new-p12-118: Raw MIDI output can block the audio thread (round 2)

Verdict: FIXED. Round 1 was sent back with four points; all four are addressed below, each with the
test that now holds it. The defect itself is unchanged from round 1 and remains real:
`MidiAlsaRaw::sendByte()` used to call `snd_rawmidi_write()` on the caller, which for a rendered
instrument is inside a render period (`src/core/midi/MidiPort.cpp:222` <- `src/tracks/InstrumentTrack.cpp:720`
<- `src/core/midi/MidiClient.cpp:398`, the raw encoder). Its siblings already avoid it
(`MidiOss::sendByte`, `src/core/midi/MidiOss.cpp:191`, and `MidiSndio::sendByte`,
`src/core/midi/MidiSndio.cpp:160`, only write into a non-blocking pipe that a writer of their own
drains).

## What the review asked for, and where it is

**1. [realtime] No producer-side I/O. A successful render-thread enqueue executed a pipe write.**
The wake protocol of round 1 is deleted. `MidiAlsaRaw::processOutEvent()`
(`src/core/midi/MidiAlsaRaw.cpp:256`, declared at `include/MidiAlsaRaw.h:97`) now does exactly two
things: it runs the encoder (`MidiClientRaw::processOutEvent`, whose bytes land in the sender's own
`thread_local` buffer at `:246`) and pushes one whole message into the bounded queue
(`MpscQueue<OutMessage, 1024>`, `include/MidiAlsaRaw.h:116`, one compare-exchange). On refusal it
increments a relaxed counter. No `write()`, no fd, no allocation, no lock, no wait, no log. Nothing
wakes anybody: the writer paces itself.

**2. [regression] An input read/descriptor error stopped the only output consumer.**
Output now has a thread of its own: `MidiAlsaRaw::OutputWriter` (`include/MidiAlsaRaw.h:122`, body at
`src/core/midi/MidiAlsaRaw.cpp:283`), started next to the input worker (`:152`) and stopped and joined
separately (`stopOutput()`, `:302`). The input loop (`:392` onwards) is input only, so its `break` paths
- poll error, descriptor error, `POLLERR/POLLHUP`, read error - end input and nothing else.
`stopInput()` likewise stops input only, so output keeps being delivered after it, which is what
`MidiClient.h` promises.

**3. [regression] Shutdown discarded queued MIDI on transient backpressure.**
`finishOutput()` (`src/core/midi/MidiAlsaRaw.cpp:314`) is a bounded flush: it retries a refused message
until the retry deadline (`OutputCloseRetryMs`, 100 ms, at `:66`) and only then breaks out, counting what
the device never took in `m_outUndelivered` (`include/MidiAlsaRaw.h:160`), which
`outputQueueOverflowCount()` reports together with the queue refusals (`include/MidiAlsaRaw.h:70`). The
writer runs that flush as it leaves (`:296`), and the destructor runs it again after the join (`:168`)
so a client whose writer never started is covered too; only then is the device closed (`:170`). A
message the device takes partly is still completed first (`drainOutQueue()`, `:340`, `writePending()`,
`:358`), so no message is split by the next one.

**4. [weakened-check] The failed-open and partial-startup checks called the byte collector directly.**
Both now drive the production entry point and keep their assertions:
`failedOpenStartsNothingAndDropsOutput()` calls `processOutEvent()` and asserts `g_writeCalls == 0`,
`g_closeCalls == 0`, `g_callsOnClosedHandle == 0`; `partialStartupFailureReleasesOnce()` calls
`processOutEvent()` and asserts `g_writeCalls == 0` plus the released-once counts
(`tests/src/core/MidiAlsaRawShutdownTest.cpp:320-360`). `sendByteStaysAllocationFree()` drives both the
encoder's byte sink and `processOutEvent()` under the allocation probe.

**Deterministic coverage added (review's closing list).**

- producer-side I/O: `audioPathQueuesOutputInsteadOfWritingIt()` counts *every* `write()` the
  executable makes on the audio-path thread. The test interposes `write()` in its own translation unit
  (`tests/src/core/MidiAlsaRawShutdownTest.cpp:172`), which works exactly as this file's `snd_rawmidi_*`
  fakes do (the client is built into the test executable, so its `::write` resolves there) and forwards
  to the real syscall, so nothing is disturbed. It asserts `0` writes on the render thread, `0`
  allocations, `0` frees, and that the message is still delivered - by the other thread, without any
  wake.
- input failure with usable output: `outputKeepsFlowingAfterAnInputFailure()` makes the device's read
  return `-EIO`, joins the input worker, and then requires a message queued afterwards to reach the
  device.
- transient backpressure during shutdown: `shutdownStillDeliversWhenTheDeviceAcceptsAgain()` refuses the
  first two writes and requires all three bytes to arrive with nothing counted as lost; and
  `shutdownCountsWhatTheDeviceNeverTook()` refuses everything, requires destruction to return, and
  requires the message to be counted - read through `outputQueueOverflowCount()` at the moment the
  device is closed, the last instant the client is still alive (the fake `snd_rawmidi_close` records it).

## Test matrix

| Path | Test | Assertion |
| --- | --- | --- |
| normal | `audioPathQueuesOutputInsteadOfWritingIt` | render thread: no `write()` at all, no allocation, no free, the device was not written from it; the message then arrives as `0x90 60 100` in order, overflow count 0 |
| failure (device full) | `fullDeviceBufferKeepsTheMessageForTheWorker` | device refuses the first write with `-EAGAIN`; nothing lost, whole message written in order |
| failure (input) | `outputKeepsFlowingAfterAnInputFailure` | after the input worker dies on `-EIO`, output queued afterwards is still delivered; device not closed |
| exhaustion | `queueOverflowIsCountedAndTheAudioPathNeverBlocks` | a device that never takes anything: 4096 pushes past the 1024 bound are refused and counted, the caller never waits |
| shutdown, transient | `shutdownStillDeliversWhenTheDeviceAcceptsAgain` | two refusals then acceptance: all 3 bytes delivered, `outputQueueOverflowCount() == 0` |
| shutdown, stuck | `shutdownCountsWhatTheDeviceNeverTook` | destruction returns within the bounded budget, nothing delivered, the message is counted |
| reconfiguration | `failedOpenStartsNothingAndDropsOutput`, `partialStartupFailureReleasesOnce` | no device: through `processOutEvent`, nothing written, nothing closed twice, no call on a closed handle |
| audio-thread rules | the two entry-point tests + allocation probe | 0 allocations, 0 frees, 0 syscalls on the calling thread |
| platform | whole suite inside `LMMS_HAVE_ALSA`; target registered only for ALSA builds (`tests/CMakeLists.txt:1933`) | builds and runs on this Linux/ALSA box |

Concurrency (local TSan does not run on this box; reasoned, not measured): producers are the audio
thread and the main thread (`MidiPort::sendOutputPatch`), consumer is the writer thread alone -
`MpscQueue` is multi-producer/single-consumer by construction, and the destructor consumes only after
the join. `m_writing`/`m_written` have the writer as their only writer. `m_outDropped`,
`m_outUndelivered`, `m_outputQuit` are atomics; `m_output` is nulled by `closeDevice()` only after both
threads are joined. `stopWorker()`'s wake byte is control-thread only.

## Commands and results (all from this session)

| Step | Command | Result |
| --- | --- | --- |
| build (round-2 code) | `cmake --build build -j4 --target zene MidiAlsaRawShutdownTest > .../logs/build-1-compile.log` | EXIT=0 |
| all slots, round-2 code | `./MidiAlsaRawShutdownTest > .../logs/test-1-first-green.log` (from `build/tests`) | EXIT=0, 18 passed |
| 4b: patch + revert | `git diff -- include/MidiAlsaRaw.h src/core/midi/MidiAlsaRaw.cpp > .../logs/fix.patch; git apply -R .../logs/fix.patch` | diff EXIT=0, revert EXIT=0 |
| 4b: rebuild (product reverted) | `cmake --build build -j4 --target zene MidiAlsaRawShutdownTest > .../logs/build-2-reverted.log` | BUILD_EXIT=0 |
| 4b: run (product reverted) | `ctest -R MidiAlsaRawShutdown --output-on-failure > .../logs/test-2-red.log` (from `build/tests`) | **TEST_EXIT=8**, 14 passed, 4 failed: `outputKeepsFlowingAfterAnInputFailure`, `shutdownCountsWhatTheDeviceNeverTook`, `sendByteStaysAllocationFree` and `audioPathQueuesOutputInsteadOfWritingIt` (the last two on the `write()` counter: 1 vs 0) |
| 4b: restore | `git apply .../logs/fix.patch` | EXIT=0 |
| 4b: rebuild (fix back) | `cmake --build build -j4 --target zene MidiAlsaRawShutdownTest > .../logs/build-3-restored.log` | BUILD_EXIT=0 |
| 4b: run (fix back) | `ctest -R MidiAlsaRawShutdown --output-on-failure > .../logs/test-3-green.log` | **TEST_EXIT=0**, 1/1 passed |
| family build | `cmake --build build -j4 --target MidiOutQueueTest MidiAlsaSeqShutdownTest MidiClockCaptureOutputTest MidiClockSourceTest MidiClockTest MidiOssTest MidiSndioTest MidiJackOutScheduleTest MidiReconnectTest RetroMidiCaptureCommandsTest ControlMidiReconnectSharedTest ControllerScriptTest ExternalMidiDeviceTest > .../logs/build-4-family.log` | EXIT=0 |
| family run | `ctest -R "MidiAlsaRawShutdown\|MidiOutQueue\|MidiAlsaSeqShutdown\|MidiClock\|MidiOss\|MidiSndio\|MidiJackOutSchedule\|MidiReconnect\|RetroMidiCaptureCommands\|ControlMidiReconnectShared\|ControllerScriptTest\|ExternalMidiDeviceTest" --output-on-failure > .../logs/test-4-family.log` | EXIT=8 from two targets that were not built: `MidiClockMenuTest`, `MidiReconnectActionTest` ("Not Run") |
| those two built | `cmake --build build -j4 --target MidiClockMenuTest MidiReconnectActionTest > .../logs/build-5-family2.log` | EXIT=0 |
| family re-run | same as family run, `> .../logs/test-6-family-rerun.log` | 17/19 passed, the same two "Not Run" |
| the two, run | `ctest -R "MidiClockMenuTest\|MidiReconnectActionTest" --output-on-failure > .../logs/test-7-family2.log` | EXIT=0, 2/2 passed - so every built member of the family passes |
| pre-existing flake, isolated | `./MidiOutQueueTest ProducerConsumer_PreservesOrder > .../logs/test-5-producerconsumer-{1,2,3}.log` | EXIT=0 three times (see Other findings) |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates-1.log 2>&1` | EXIT=0, `RESULT: PASS` |

Log folder: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p12-118.r2/logs`.

## Files changed in round 2

- `src/core/midi/MidiAlsaRaw.cpp`, `include/MidiAlsaRaw.h` - the rework above (writer thread, bounded
  flush, no producer I/O).
- `tests/src/core/MidiAlsaRawShutdownTest.cpp` - the `write()` interposition, the device-side
  read-failure and close-time accounting hooks, the four new/updated tests.
- `tests/upstream-modifications.txt` - reasons for both inherited files (already registered; no new
  product file, so `tests/fork-sources.txt` is unchanged).
- `docs/qwen-fixes-2026-10/cluster-new-p12-118.md` - this report.

## Other findings (not fixed here)

- `MidiOutQueueTest::ProducerConsumer_PreservesOrder` failed once inside the family run (`.../logs/test-4-family.log`),
  then passed three times in isolation and again in the family re-run. It is a pre-existing 100 000-event
  `MidiOutQueue` stress on `std::this_thread::yield()`, untouched by this change, so it looks like a
  load-sensitive flake rather than a regression; it is reported, not fixed.
- `MidiAlsaSeq`'s audio-path queue is a single-producer `MidiOutQueue` (`include/MidiAlsaSeq.h:222`)
  although `MidiPort::processOutEvent` is also called from the main thread (`sendOutputPatch()`); this
  fix used the multi-producer `MpscQueue` for that reason, the sibling is left alone.
- `MidiOss`/`MidiSndio` are retired (Q050-479) and still stop only in their destructors, and their
  output path is a non-blocking pipe write - still I/O on the render thread by the standard applied
  here, but out of scope for this finding.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** 4b
  above: revert (`git apply -R .../logs/fix.patch`, EXIT=0) -> rebuild (EXIT=0) -> run
  `.../logs/test-2-red.log` **EXIT=8** with four failures; restore (`git apply`, EXIT=0) -> rebuild
  (EXIT=0) -> run `.../logs/test-3-green.log` **EXIT=0**. The round-1 code those four failures pin is
  the branch state the reviewer rejected, and the first build/run with the round-2 code is
  `.../logs/build-1-compile.log` / `.../logs/test-1-first-green.log`.
- **The test is deterministic: no wall-clock timing, sleeps or races.** No test sleeps and none asserts
  an elapsed time. The seams are: the file's fake ALSA device (bytes, writing thread, a bounded
  `-EAGAIN` counter, a settable read result, a close-time read of the client's counter), the interposed
  `write()` (a syscall counter for one designated thread), `QSemaphore::tryAcquire` released by the
  write stub (a synchronisation hook, where the timeout only bounds a failure), `QThread::wait` for the
  input worker, and the allocation probe. The shutdown-stuck test asserts counts and termination, not
  durations: the flush is bounded in the product, so the assertion holds however long the device refuses.
- **The fix is at the cause and complete; every caller and sibling checked.** Callers into the encoder
  are all covered: the render path (`MidiPort.cpp:222`), the GUI patch path (`MidiPort::sendOutputPatch`),
  the base `processOutEventAt` fall-through (`include/MidiClient.h:66`) and the off-audio
  `MidiClientRaw::processNonAudioOutEvent` (`src/core/midi/MidiClient.cpp:68`) - all reach the one
  multi-producer queue. Siblings: `MidiJack`'s thread-local assembly + queue idiom
  (`src/core/midi/MidiJack.cpp:226-249`) is what the encoder side copies; `MidiOss`/`MidiSndio` already
  have their own writer threads; `MidiAlsaSeq`'s wake pipe remains its own design (single-producer
  queue, documented above). Declaration and definition changed together, and every target that includes
  the headers was rebuilt (`zene` plus the 15 family targets in the table).
- **No collateral damage.** No project data, model, default or file format changed; the queue, the
  counters and the writer are per-client runtime state. The only limit added is the 1024-message bound
  on the new hand-off (refuse-and-count, nobody else can be affected by it), and no existing check or
  limit was changed or re-anchored. Behaviour outside the bug is preserved: same bytes, same order, same
  encoder, a client without a device still drops output without crashing or touching a closed handle,
  and shutdown still joins within the old bound (the bounded flush only runs when something is actually
  pending; the strictest existing bound test, `closingAWorkerBlockedOnInputIsBounded`, still passes).
  Deliberate trade-offs: up to one writer pace (2 ms) of added output latency, and a bounded 100 ms
  flush at close only when the device refuses; both are documented in the file header comment.
- **ALREADY-FIXED / FALSE-POSITIVE evidence.** Not claimed; the verdict is FIXED and the defect was
  reproduced (round 1's four failing tests, and this round's `test-2-red.log`).
- **Sibling commands in the command files changed.** No command file changed: no control command was
  added, and nothing under `src/core/Control*` or `src/gui` was touched, so `app.*`, `arrangement.*` and
  the `ControlCommandsSnapshot` canary are unaffected.
