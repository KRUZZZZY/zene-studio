# Cluster new-p17-85 — `src/core/LinkAudioStream.cpp` (three findings in one file)

FIXED

new-p17-85: FIXED
new-p17-84: FIXED
new-p14-145: FIXED

All three findings are in `src/core/LinkAudioStream.cpp` — the PCM stream between two Zene
instances (`include/LinkAudioStream.h`). Each one is fixed in its own commit on
`bugs/qwen-new-p17-85`, each with its own regression in `tests/src/core/LinkAudioStreamTest.cpp`:

| commit | finding |
| --- | --- |
| `8d2f9205a` | new-p17-85: the gap silence that does not fit into the RX ring was not counted |
| `ed70fbe51` | new-p17-84: the master's `SampleFrame` array was read as a flat `float` array |
| `3a0610724` | new-p14-145: a sample-rate change left the old rate's frames in both rings |
| `8d716bc83` | new-p14-145: the I/O-thread seam the rate-change regressions need |
| `365490232` | new-p14-145: an old-rate packet must not reach the mix at the new rate |
| `b82ccca98` | new-p14-145: the socket half of the stream in its own file (the file-length gate) |
| `ec768513f` | new-p14-145: the rate-change scenarios in the shared test support |

The shared loopback helpers the regressions use (`sendAudioPacket`, `waitForPackets`,
`drainPeerPackets`, `waitForSendHolds`, `waitForPublishHolds`, `waitForPacketHandled`) and the
three rate-change scenarios live in `tests/src/core/LinkAudioStreamTestSupport.h`; the test
slots that call them stay in `tests/src/core/LinkAudioStreamTest.cpp`, so the family still runs
them under the same names. `tests/CMakeLists.txt` now defines `ZENE_SOURCE_DIR` for
`LinkAudioStreamTest` (the source guard of new-p17-84 reads `src/core/LinkAudioStream.cpp`).

Every command and every exit code below was run in this session; the logs are in
`/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p17-85.r3/logs` under the names given.

### The review of round 3 and what changed because of it

Luna's round-3 review sent the cluster back with two blocking points against new-p14-145, and
both are addressed here:

1. **[not-fixed] The RX publication race.** "An old-rate packet can be validated by the I/O
   thread before the rate change, then published to the RX ring after the audio thread flushes
   it. The audio thread can consume that packet at the new rate." The RX side now carries an
   epoch next to the rate, and the transition is coordinated: the I/O thread re-checks the
   epoch before it publishes and drops the packet if the rate changed, the I/O thread publishes
   the epoch it has drained, and the audio thread drops what the ring holds and stays silent
   until that drain arrives. See the new-p14-145 section.
2. **[test-proves-nothing] The TX regression's scheduling assumption.** "It submits 40,000
   frames and immediately changes rate without synchronizing with the I/O thread, so under a
   legal schedule the old frames can all be sent before the transition and the reversed
   implementation can pass." The I/O thread is now held before it sends, so the frames of the
   TX ring are *certainly* still queued when the rate changes. See the new-p14-145 section.

---

## new-p17-85 — Dropped gap silence is missing from the RX overrun count

**Verdict: FIXED.**

**Where.** `src/core/LinkAudioStream.cpp:225` (the finding's line) — `acceptPacket()` replaced a
sequence gap with silence and ignored what `writeSilence()` returned:

```cpp
m_rx.writeSilence(static_cast<std::size_t>(gap) * header.frames);
```

`AudioRing::writeSilence()` (`src/core/LinkAudioCodec.cpp:172`) returns the frames it could take
(`n = std::min(frames, m_capacity - (head - tail))`), so a full ring silently swallowed the
silence. The report's `rxOverruns` ("frames dropped because the RX ring was full",
`include/LinkAudioStream.h:154`) counted only the dropped *payload* frames, so a hole in the audio
that the gap silence created was invisible in the counters.

**Fix.** `src/core/LinkAudioStream.cpp` — the silence now goes through
`LinkAudioStream::insertGapSilence()` (`src/core/LinkAudioStream.cpp:252`), which counts the
frames the ring could not take as overruns:

```cpp
void LinkAudioStream::insertGapSilence(std::size_t frames) noexcept
{
	const std::size_t silenced = m_rx.writeSilence(frames);
	if (silenced < frames) { m_rxOverruns.fetch_add(frames - silenced, std::memory_order_relaxed); }
}
```

**Regression.** `LinkAudioStreamTest::droppedGapSilenceIsCountedAsAnOverrun` fills the RX ring
exactly (one packet, then 56 packets that each skip 8 sequences — 8·128 frames of silence plus
128 frames of payload — then one that skips 7, whose silence takes exactly the rest of the ring
and whose payload does not fit), and then sends a packet that skips 8 sequences. The 1024 silence
frames of that packet do not fit either, so the overrun count must grow by 1024 + 128.

**RED/GREEN evidence (this session).**

| check | command | exit | result |
| --- | --- | --- | --- |
| RED, this fix reverted only | `cmake --build build --target LinkAudioStreamTest -j8` | 0 | built (`log-04-build-red-bugA.log`) |
| RED, this fix reverted only | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure` | 8 | FAIL `droppedGapSilenceIsCountedAsAnOverrun`: Actual 128, Expected 1152 (`log-05-ctest-red-bugA.log`) |
| GREEN, fix restored | `cmake --build build --target zene LinkAudioStreamTest -j8` | 0 | built (`log-18-build-final.log`) |
| GREEN, fix restored | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure` | 0 | 18 passed, 0 failed (`log-19-ctest-final.log`) |

The RED run above reverts *this* fix only: `acceptPacket()` calls `m_rx.writeSilence(...)` again
and drops its return value; the other two fixes stay in the tree. The only failing test is this
bug's regression.

---

## new-p17-84 — `SampleFrame` arrays are accessed as a flat float array

**Verdict: FIXED.**

**Where.** `src/core/LinkAudioStream.cpp:143` (the finding's line) — `process()` cast the
engine's frame array to `float*` and moved the frames through that pointer:

```cpp
float* interleaved = reinterpret_cast<float*>(frames);
...
m_tx.write(interleaved, static_cast<std::size_t>(count));
... receivePeriod(interleaved, ...)
```

The `static_assert(sizeof(SampleFrame) == AudioChannels * sizeof(float), ...)` next to it
asserted the *size* of a frame, which says nothing about the *type* of its bytes: an array of
`SampleFrame` objects is not a `float` array, and reading it through a `float*` is undefined
behaviour (the `float` objects the pointer names do not exist; the frames own `std::array<sample_t,2>`
members). The same punning was in `addFromRing()`, which added the ring's samples into the
master's frames through the flat pointer.

**Fix.** `src/core/LinkAudioStream.cpp` / `include/LinkAudioStream.h` — the frames are read and
written channel by channel, through the frame's own accessors:

* `sendFrames()` copies the master's frames into a preallocated interleaved buffer
  (`m_txBuf`, allocated in the constructor) with `frames[i].left()` / `frames[i].right()`, in
  chunks of at most `TxChunkFrames` (1024) frames, so a period longer than the buffer is still
  converted in full;
* `addFromRing()` adds the ring's samples into the master's frames with
  `frames[i].left() += ...` / `frames[i].right() += ...`;
* the misleading size assertion is gone; what the wire format needs (a `float` per sample) is
  still asserted.

The ring itself stays a ring of interleaved floats (`src/core/LinkAudioCodec.cpp`): it stores
floats, not frames, so nothing about it changes.

**Regression.** `LinkAudioStreamTest::theFrameArrayIsNotReadAsAFlatFloatArray` reads
`src/core/LinkAudioStream.cpp` and fails while the file still puns a frame array through a
`float*` (or still asserts the size of a frame as if it made the array a float array). The defect
is undefined behaviour with no observable behaviour difference — the layout happens to match
today — so the regression is a static guard on the cause; the *values* are pinned by the existing
`loopbackExchangeIsSampleExact` (every sample of a real round trip, including the right channel)
and `audioThreadAllocatesNothing` (the new conversion allocates nothing on the audio thread).
In this session the guard was strengthened: the pattern now also catches a `const float*`
(`reinterpret_cast\s*<\s*(const\s+)?float\s*\*`), which the first version of the guard would have
let through. The socket half of the stream (`src/core/LinkAudioSocket.cpp`, the file the socket
code moved to) does not touch frames at all — it moves floats between the ring and the
datagram — so there is nothing to guard there.

**RED/GREEN evidence (this session).**

| check | command | exit | result |
| --- | --- | --- | --- |
| RED, this fix reverted only | `cmake --build build --target LinkAudioStreamTest -j8` | 0 | built (`log-06-build-red-bugB.log`) |
| RED, this fix reverted only | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure` | 8 | FAIL `theFrameArrayIsNotReadAsAFlatFloatArray`: "the stream must not read a SampleFrame array through a float*" (`log-07-ctest-red-bugB.log`) |
| GREEN, fix restored | `cmake --build build --target zene LinkAudioStreamTest -j8` | 0 | built (`log-18-build-final.log`) |
| GREEN, fix restored | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure` | 0 | 18 passed, 0 failed (`log-19-ctest-final.log`) |

The RED run above reverts *this* fix only: the `reinterpret_cast<float*>(frames)` punning and the
`sizeof(SampleFrame)` assertion are put back in `sendFrames()` and `addFromRing()`, the other two
fixes stay in the tree. The only failing test is this bug's regression.

---

## new-p14-145 — A sample-rate change leaves old-rate audio in the rings

**Verdict: FIXED.**

**Where.** `src/core/LinkAudioStream.cpp:142` (the finding's line) — `process()` adopted the
engine's new rate and nothing else:

```cpp
if (m_rate.load(std::memory_order_relaxed) != sampleRate) { m_rate.store(sampleRate, std::memory_order_relaxed); }
```

The TX ring (producer: audio thread, consumer: I/O thread) and the RX ring (producer: I/O thread,
consumer: audio thread) still held frames of the old rate. The I/O thread stamped every packet
with the rate it read at send time (`src/core/LinkAudioStream.cpp:377`, `m_rate.load()`), so the
old frames went out labelled with the *new* rate and the peer played them at the wrong speed; the
old frames in the RX ring were played at the new rate.

**Fix, part 1 — the TX side** (`src/core/LinkAudioStream.cpp` / `include/LinkAudioStream.h`,
commit `3a0610724`):

* `onRateChanged()` (audio thread) flushes the RX ring (`m_rx.skip(m_rx.fill())` — the audio
  thread is that ring's consumer) and re-primes the jitter buffer, and bumps a TX epoch;
* the rate and the epoch of the queued TX frames travel together in one atomic
  (`m_txStamp = (epoch << 32) | rate`), so the I/O thread reads the rate *before* it takes frames
  from the ring (`txRateForNextFrames()`): a packet can never carry a rate that is not the rate of
  the frames in it;
* when the epoch changes, the I/O thread drops the queued TX frames (`m_tx.skip(m_tx.fill())` — it
  is that ring's consumer) before sending anything, and publishes the epoch it flushed
  (`m_txFlushedEpoch`);
* the audio thread holds its frames back until the I/O thread has flushed (`sendFrames()` returns
  early while `m_txFlushedEpoch < m_txRateEpoch`), so the two rates are never mixed in the ring;
* a rate that the engine has not published yet (0) no longer drains the TX ring: those frames
  wait for the rate instead of being thrown away.

**Fix, part 2 — the RX publication race** (commit `365490232`, the round-3 blocking point).
The TX side above cannot reach the RX ring: the RX ring is produced by the I/O thread and
consumed by the audio thread, and the rate change was a flush that the audio thread did *not*
order against the I/O thread's publication. A packet that the I/O thread validated before the
change could be published after the flush and then played at the new rate. The fix gives the RX
ring the same treatment the TX ring has — a rate that travels with an epoch — and coordinates
the transition:

* the rate of the frames in the RX ring and the epoch they belong to travel together in one
  atomic (`m_rxStamp = (epoch << 32) | rate`). The I/O thread validates a packet against that
  *pair*, so the rate it checks and the epoch it publishes always belong together;
* `acceptPacket()` re-checks the epoch **before it publishes**: if the rate changed while the
  packet was validated, the packet carries the old rate, the audio thread has already dropped
  the old-rate frames, and the packet is counted as a rate mismatch and dropped instead of
  reaching the ring (`src/core/LinkAudioStream.cpp`, `acceptPacket()`);
* the I/O thread publishes the epoch it has drained at the top of its loop iteration
  (`acknowledgeRxRate()`, called from `ioLoop()` in `src/core/LinkAudioSocket.cpp`) — i.e. after
  the writes of the previous iteration. Everything the I/O thread publishes from then on carries
  the current rate, and the sequence stream of the old rate is dropped (`m_haveRx = false`), so
  the next packet re-synchronises;
* the audio thread drops what the ring holds and **stays silent** while that drain has not
  arrived (`m_rxFlushedEpoch < m_rxRateEpoch` in `receivePeriod()`): the old frames are never
  played at the new rate. When the drain arrives it drops the ring once more — the frames the
  I/O thread published before the drain are in the ring and carry the old rate — and the jitter
  buffer starts again.

The SPSC ownership is untouched: only the audio thread advances the RX ring's tail (it is that
ring's consumer) and only the I/O thread advances its head. The audio thread allocates nothing,
takes no lock and never waits: the drain is read with one atomic load, and the silence during
the transition is a few milliseconds of a device switch, not a stall.

**Regressions.**

* `LinkAudioStreamTest::aSampleRateChangeDropsTheOldRateRxFrames` — the peer queues 8 packets
  (1024 frames) at 48 kHz; the device then changes to 44.1 kHz and the master mix must stay
  silent (the queued 48 kHz frames must not be played at 44.1 kHz). RED: the mix carried 0.5.
* `LinkAudioStreamTest::aSampleRateChangeDoesNotSendOldRateFramesAsNewRate` — the I/O thread is
  **held before it sends** (the seam), so the 40000 frames of 0.5 at 48 kHz are certainly still
  queued when the device changes to 44.1 kHz. Every packet the peer receives afterwards must
  carry the new rate and the new samples, and the stream must come back at the new rate. RED:
  256 packets carried old-rate samples under the new rate. The test no longer depends on the
  scheduling: the hold makes "the old frames are still queued" a fact, not a hope.
* `LinkAudioStreamTest::anOldRatePacketIsNotPlayedAtTheNewRate` (new, the round-3 blocking
  point) — the I/O thread is **held between the validation of a packet and its publication**,
  the engine changes the rate (which drops the frames of the ring), and the packet is published
  afterwards. The old-rate audio must never reach the mix. RED: the mix carried 0.5.

**The seam** (`include/LinkAudioStream.h` / `src/core/LinkAudioStream.cpp`, commit `8d716bc83`).
The two windows the regressions need are inside the I/O thread, so the seam lives in the
product — the same shape as the loopback-only seam of `AbletonLinkSession`:

* `LinkAudioStreamSeam::setSendHold(true)` — the I/O thread waits before it sends the frames of
  the TX ring. The test arms the hold and waits for the counter that says the I/O thread reached
  it (`waitForSendHolds()`), a bounded wait for an observable, never a sleep;
* `LinkAudioStreamSeam::setPublishHold(true)` — the I/O thread waits after it validated a packet
  and before it publishes it (`waitAtPublishHold()` in `acceptPacket()`);
* both holds are off in production: the I/O thread only reads a relaxed flag, and it never takes
  the seam's lock then. The seam's mutex is only taken while a hold is armed, and the audio
  thread never touches it.

**RED/GREEN evidence (this session, against the final tree).**

| check | command | exit | result |
| --- | --- | --- | --- |
| RED, the RX fix reversed | `git diff -- src include > fix-rx.patch` (the fix removed by hand), `cmake --build build --target zene LinkAudioStreamTest -j8` | 0 | built (`log-65-build-red-rx3.log`) |
| RED, the RX fix reversed | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure` | 8 | FAIL `anOldRatePacketIsNotPlayedAtTheNewRate`: `f.left()` 0.5 instead of 0; the other 18 test functions pass (`log-66-ctest-red-rx3.log`) |
| GREEN, the RX fix restored | `git apply -R fix-rx.patch`, `cmake --build build --target zene LinkAudioStreamTest -j8` | 0 | built (`log-67-build-green-rx2.log`) |
| GREEN, the RX fix restored | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure` | 0 | 100% tests passed (`log-68-ctest-green-rx2.log`) |
| RED, the TX epoch mechanism reversed | `git diff -- src include > fix-tx.patch` (the mechanism removed by hand), `cmake --build build --target zene LinkAudioStreamTest -j8` | 0 | built (`log-69-build-red-tx2.log`) |
| RED, the TX epoch mechanism reversed | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure` | 8 | FAIL `aSampleRateChangeDoesNotSendOldRateFramesAsNewRate`: `check.mismatched` 256 instead of 0; the other 18 test functions pass (`log-70-ctest-red-tx2.log`) |
| GREEN, the TX epoch mechanism restored | `git apply -R fix-tx.patch`, `cmake --build build --target zene LinkAudioStreamTest -j8` | 0 | built (`log-71-build-green-tx2.log`) |
| GREEN, the TX epoch mechanism restored | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure` | 0 | 100% tests passed (`log-72-ctest-green-tx2.log`) |

The two RED runs revert *one* mechanism each and keep everything else (including the seam, so
the regressions exercise the same interleaving they do with the fix): the RX run removes the
epoch of the RX ring, the epoch re-check before the publication, the drain acknowledgement and
the transition branch in `receivePeriod()`; the TX run removes the TX epoch, the hold-back of
the audio thread's frames, the flush of the TX ring and the "read the rate before the frames"
rule. Each run fails exactly one regression — the one of the mechanism it took away.

---

## The three together (the 4b proof of the whole product diff)

This is the proof of the state before the round-3 rework, when the whole product diff could be
reverted in one patch. It is kept because it still shows the two round-3-clean findings
(new-p17-85, new-p17-84) failing without their fix:

| check | command | exit | result |
| --- | --- | --- | --- |
| revert all three fixes | `git diff b61e5f965 -- src include > fix.patch` then `git apply -R fix.patch` | 0 | patch applied (`fix.patch`, 224 lines) |
| revert all three, build | `cmake --build build --target zene LinkAudioStreamTest -j8` | 0 | built (`log-10-build-unpatched-all.log`) |
| revert all three, test | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure` | 8 | all four regressions FAIL: `droppedGapSilenceIsCountedAsAnOverrun` (128 vs 1152), `theFrameArrayIsNotReadAsAFlatFloatArray` (the source still punned the array), `aSampleRateChangeDropsTheOldRateRxFrames` (0.5 vs 0), `aSampleRateChangeDoesNotSendOldRateFramesAsNewRate` (256 vs 0) (`log-11-ctest-unpatched-all.log`) |
| restore all three | `git apply fix.patch` | 0 | patch applied |
| restore all three, build | `cmake --build build --target zene LinkAudioStreamTest -j8` | 0 | built (`log-12-build-repatched-all.log`) |
| restore all three, test | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure` | 8 | **all 18 test functions passed**, but the process segfaulted *after* `cleanupTestCase` — the pre-existing crash described below (`log-13-ctest-repatched-all.log`) |

The patch was taken against the base commit `b61e5f965` (the commit the branch starts from),
because the three fixes are already committed on the branch; `git diff -- src include` against
`HEAD` is empty by design.

After the round-3 rework this whole-diff reversal is no longer the right proof, and the proof
in the new-p14-145 section replaces it: the rate-change regressions need the seam (the I/O
thread's hold) to meet the interleaving they are about, so reverting the *whole* product diff
would take the seam away with it and the regressions would fail for the wrong reason. The
per-mechanism reversal is the stronger form anyway — it removes one mechanism at a time and
shows that each regression fails exactly for the mechanism it took away:

* the RX epoch mechanism reversed → `anOldRatePacketIsNotPlayedAtTheNewRate` fails (0.5 vs 0),
  the other 18 test functions pass (`log-65`, `log-66`);
* the TX epoch mechanism reversed → `aSampleRateChangeDoesNotSendOldRateFramesAsNewRate` fails
  (256 mismatched packets vs 0), the other 18 test functions pass (`log-69`, `log-70`);
* both restored → 19 test functions pass (`log-67`, `log-68`, `log-71`, `log-72`).

The two round-3-clean findings keep their own per-bug reversal proofs (the tables above).

---

## Final state

| check | command | exit | result |
| --- | --- | --- | --- |
| product build | `cmake --build build --target zene LinkAudioStreamTest -j8` | 0 | built (`log-67-build-green-rx2.log`, `log-71-build-green-tx2.log`) |
| affected family | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure` | 0 | 1 test binary, 19 QTest slots (17 test functions plus init/cleanup), all passed (`log-68-ctest-green-rx2.log`, `log-72-ctest-green-tx2.log`) |
| affected family, stability | `QT_QPA_PLATFORM=offscreen ./LinkAudioStreamTest` × 8 (`log-76-run-1.log` … `log-76-run-8.log`) | 0 in 6 runs, 139 in 2 | **0 of 8 runs had a failing test function**; 2 runs hit the pre-existing exit-time segfault (see below) |
| gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | every row PASS (`log-73-gate-final-r4.log`) |
| complexity | `python3 -m lizard src/core/LinkAudioStream.cpp src/core/LinkAudioSocket.cpp tests/src/core/LinkAudioStreamTest.cpp tests/src/core/LinkAudioStreamTestSupport.h` | 0 | no function over CCN 10 (`log-21-lizard.log`, re-run for the split) |
| file length | `bash tests/file-length-gate.sh --check` | 1 | the train-wide red that is already on the base; none of the `LinkAudioStream*` files is in the list — the socket half moved to `src/core/LinkAudioSocket.cpp` so that both files stay inside the 500 line ratchet (`log-22-file-length.log`) |
| neighbours | `cd build/tests && ctest -R '^(ControlLinkCommandsTest\|AudioEngineTeardownTest\|AudioEngineProfilerTest)$' --output-on-failure` | 0 | 3 test binaries passed (`log-75-ctest-neighbours2.log`, built by `log-74-build-neighbours2.log`) |

### The exit-time segfault of this test binary is pre-existing (not caused by these fixes)

`LinkAudioStreamTest` sometimes segfaults *after* every test function has passed — the log shows
`Totals: 18 passed, 0 failed` and then `***Exception: SegFault` at process exit. Measured in this
session:

| tree | command | result |
| --- | --- | --- |
| base `b61e5f965` (all five changed files reverted) | `QT_QPA_PLATFORM=offscreen ./LinkAudioStreamTest` × 12 (`log-16-base-run-1.log` … `log-16-base-run-12.log`) | **4 of 12 runs crashed** (runs 1, 3, 11, 12; exit 139), each after `Totals: 14 passed` (the base's own 14 test functions) — the base build is `log-15-build-base.log` |
| base `b61e5f965`, second batch | `QT_QPA_PLATFORM=offscreen ./LinkAudioStreamTest` (`log-31-base2-run-1.log`) | crashed on the first run (exit 139) — the base build is `log-30-build-base2.log` |
| this branch | `QT_QPA_PLATFORM=offscreen ./LinkAudioStreamTest` × 15 (`log-03-binary-run-1.log` … `log-03-binary-run-5.log`, `log-14-head-run-1.log` … `log-14-head-run-10.log`) | **0 of 15 runs crashed**; two further ctest runs of the branch were run, one crashed (`log-13-ctest-repatched-all.log`, `log-28-ctest-committed.log`) and one did not (`log-19-ctest-final.log`) |
| this branch, after the round-3 fix | `QT_QPA_PLATFORM=offscreen ./LinkAudioStreamTest` × 8 (`log-76-run-1.log` … `log-76-run-8.log`) | 2 of 8 runs crashed (runs 4 and 6, exit 139), **0 of 8 had a failing test function** |

The crash is therefore present on the untouched base and is *not* introduced by these fixes (the
base crashes more often in this sample). It happens in the process teardown, after the test run
itself has finished, and is outside the scope of these three findings; it is reported here rather
than worked around.

The stack was captured from a core dump of each tree and is **identical in both**:

```
Program terminated with signal SIGSEGV, Segmentation fault.
#0  libc (memcpy/memmove)
#1  QArrayDataPointer<lmms::AudioEngineWorkerThread*>::reallocateAndGrow(...)
#2  lmms::AudioEngineWorkerThread::drainDeferredPlayHandleEdits()
#3  lmms::AudioEngineWorkerThread::startAndWaitForJobs()
#4  lmms::AudioEngine::renderStageInstruments()
#5  lmms::AudioEngine::renderNextPeriod()
#6  lmms::AudioDummy::run()
```

* branch: `gdb -batch -c <core of the gdb attempt> ./LinkAudioStreamTest` → `log-29-gdb-core.log`
  (the core of the gdb attempt `log-25-gdb-head-2.log`, which died with "Couldn't read debug
  register: No such process" while the `Link Dispatcher` thread was still running);
* base `b61e5f965`: the first run of the second base batch crashed
  (`log-31-base2-run-1.log`, exit 139) and its core gives the same stack → `log-32-gdb-core-base.log`
  (the base binary is `log-30-build-base2.log`).

The crash is in the **engine's** dummy audio thread, growing the `QArray` of worker-thread
pointers in `AudioEngineWorkerThread::drainDeferredPlayHandleEdits()` while the process is being
torn down — a shutdown race in `src/core/AudioEngine.cpp`, not in `LinkAudioStream` (the stream
only reads the engine's sample rate and taps the mix). It is a separate finding for
`src/core/AudioEngine.cpp` and is left untouched here.

---

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Each bug was proven on its own by putting that bug's old behaviour back into the tree:
  new-p17-85 → ctest exit 8, `droppedGapSilenceIsCountedAsAnOverrun` 128 vs 1152
  (`log-05-ctest-red-bugA.log`); new-p17-84 → ctest exit 8,
  `theFrameArrayIsNotReadAsAFlatFloatArray` (`log-07-ctest-red-bugB.log`); new-p14-145 → the RX
  epoch mechanism reversed → ctest exit 8, `anOldRatePacketIsNotPlayedAtTheNewRate` 0.5 vs 0
  (`log-66-ctest-red-rx3.log`), and the TX epoch mechanism reversed → ctest exit 8,
  `aSampleRateChangeDoesNotSendOldRateFramesAsNewRate` 256 vs 0 (`log-70-ctest-red-tx2.log`).
  Each of those runs reverses **one** mechanism and keeps the rest of the tree (including the
  seam, so the regressions meet the same interleaving they meet with the fix), and each fails
  exactly the regression of the mechanism it took away. With the fixes in the tree: build exit 0,
  ctest exit 0, 19 test functions passed (`log-67-build-green-rx2.log`,
  `log-68-ctest-green-rx2.log`, `log-71-build-green-tx2.log`, `log-72-ctest-green-tx2.log`).
* **The test is deterministic: no wall-clock timing, sleeps or races.** Every value the
  regressions depend on is injected: the sequence numbers, the frame counts, the sample values
  and the rate in each packet header. The waits are bounded waits for an *observable* — the
  stream's own `packetsReceived` counter (`waitForPackets()`), the datagrams waiting at the
  peer's socket (`drainPeerPackets()`), the seam's counters (`waitForSendHolds()`,
  `waitForPublishHolds()`) and the stream's rate-mismatch counter (`waitForPacketHandled()`) —
  never a sleep whose duration decides the result, and never a fixed period. The RX regression
  waits for the counter to reach 8 before the rate changes, so the 1024 queued frames are
  certainly in the ring. The **TX regression no longer assumes anything about the scheduling**:
  the I/O thread is held before it sends, the test waits for the counter that says it reached the
  hold, and only then are the old frames queued and the rate changed — the old frames are
  therefore *certainly* still queued at the transition, and the reversed implementation cannot
  pass. The **RX race regression** pins the window itself: the I/O thread is held between the
  validation of a packet and its publication, and the test waits for the counter that says the
  packet was validated before it changes the rate.
* **The fix is at the cause and complete.** `process()` is the only caller of `sendFrames()` and
  `receivePeriod()`; `addFromRing()` is called only by `receivePeriod()`; `insertGapSilence()` is
  called only by `acceptSequence()`; `acceptSequence()` is called only by `acceptPacket()`;
  `onRateChanged()` is called only by `process()` (the engine enters the stream through
  `LinkAudioStream::tap()`, `include/LinkAudioStream.h`, and `src/core/AudioEngine.cpp` calls
  that — unchanged); `txRateForNextFrames()` is called only by `sendPending()`;
  `acknowledgeRxRate()` is called only by `ioLoop()`. The TX ring is produced by the audio thread
  and consumed by the I/O thread, so only the I/O thread drops TX frames (`m_tx.skip()`), and the
  RX ring is produced by the I/O thread and consumed by the audio thread, so only the audio
  thread drops RX frames (`m_rx.skip()`) — no ring is touched from the wrong side, and the drain
  acknowledgement is a counter, not a move of frames between the sides. The header and the
  definitions changed together, and `zene` and the test were rebuilt after every change.
* **No collateral damage.** Nothing about the saved format, the projects, the control commands or
  the undo stack changed: no new command, no new file format, no new limit that another caller
  can hit. The conversion buffer (`m_txBuf`) is allocated once in the constructor and chunked at
  1024 frames, so a period of any length is still converted in full; the ring's capacity, the
  jitter buffer's target and the counters are unchanged. The socket half of the stream moved to
  `src/core/LinkAudioSocket.cpp` (commit `b82ccca98`) — the same code, moved along the seam that
  was already in the file, because the fix pushed the file past the 500 line ratchet; no
  behaviour changed with it. The existing tests of the family
  (`loopbackExchangeIsSampleExact`, `audioThreadAllocatesNothing`,
  `realtimeStressReportsBoundedUnderrunsAndLatency`, `disablingWhileFloodedJoinsPromptly`,
  `packetsFromAStrangerOrAtAnotherRateAreCounted`, `pcmStreamsWhileLinkSyncIsActive`,
  `commandsAreRegisteredAndUndoable`, `theStreamLeavesLinkSyncAlone`) still pass unchanged, and
  the neighbouring binaries `ControlLinkCommandsTest`, `AudioEngineTeardownTest` and
  `AudioEngineProfilerTest` pass as well (`log-75-ctest-neighbours2.log`).
* **Verdict evidence (the code, not a claim).** new-p17-85: `AudioRing::writeSilence()`
  (`src/core/LinkAudioCodec.cpp:172`) returns the frames it accepted, and the old
  `acceptPacket()` threw that value away. new-p17-84: the old `process()` read
  `reinterpret_cast<float*>(frames)` and the `static_assert` next to it only constrained a size.
  new-p14-145: the old `process()` stored the new rate and nothing else; the old
  `sendPending()` stamped the packets with the rate it read *after* taking the frames from the
  ring; and the old `acceptPacket()` validated a packet against the rate and published it later,
  so a packet validated before the change could reach the ring after the flush. All three are
  the code the fixes replace.
* **None of the three verdicts is ALREADY-FIXED or FALSE-POSITIVE**, so that question does not
  apply here: each finding describes code that was on the branch before its fix commit, and each
  fix commit's regression fails when that fix is reverted (the RED tables above). The verdicts are
  FIXED, FIXED and FIXED.
* **Realtime.** The audio-thread path is `tap()` → `process()` → `sendFrames()` /
  `receivePeriod()` → `addFromRing()`, plus `onRateChanged()`. None of them allocates, frees,
  locks, waits, logs or does I/O: the only allocation is `m_txBuf` in the constructor, the ring
  buffers were already preallocated, and the new state is plain members and lock-free atomics
  (`m_txStamp`, `m_txFlushedEpoch`, `m_rxStamp`, `m_rxFlushedEpoch`). The transition itself is
  one atomic load in `receivePeriod()` — the audio thread never waits for the I/O thread, it
  stays silent until the drain arrives. The I/O-thread path (`sendPending()` →
  `txRateForNextFrames()`, `receiveDatagrams()` → `acceptPacket()` → `acknowledgeRxRate()`)
  touches only its own ring and the atomics. The seam's mutex is only taken while a hold is
  armed, which never happens in production, and never on the audio thread.
* **Qt 5.15 / MSVC / macOS portability.** The fix uses no new API: `QUdpSocket` and
  `QHostAddress` are Qt 5 and Qt 6 alike, and the test support header uses only
  `QElapsedTimer`, `QUdpSocket` and `QTest::qWait`. The product change touches no Windows or
  POSIX header. `std::uint64_t` shifts and `<utility>`'s `std::pair` are C++11/C++17 standard.
  The seam uses `std::mutex` and `std::condition_variable`, which are standard C++11.
* **Undo.** No command was added or changed, so the undo stack is untouched (one honest step, as
  before) and the control-command snapshot canary does not move.
