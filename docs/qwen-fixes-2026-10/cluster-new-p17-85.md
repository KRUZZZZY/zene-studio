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

The shared loopback helpers the regressions use (`sendAudioPacket`, `waitForPackets`,
`drainPeerPackets`) live in `tests/src/core/LinkAudioStreamTestSupport.h`; `tests/CMakeLists.txt`
now defines `ZENE_SOURCE_DIR` for `LinkAudioStreamTest` (the source guard of new-p17-84 reads
`src/core/LinkAudioStream.cpp`).

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
`LinkAudioStream::insertGapSilence()`, which counts the frames the ring could not take as
overruns:

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

**Evidence.**

| check | command | exit | result |
| --- | --- | --- | --- |
| RED (test first) | `cmake --build build --target LinkAudioStreamTest -j8` | 0 | built (`build-1-red-p1785.log`) |
| RED (test first) | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure` | 8 | FAIL `droppedGapSilenceIsCountedAsAnOverrun`: Actual 128, Expected 1152 (`test-1-red-p1785.log`) |
| 4b revert | `git apply -R fix-p1785.patch` → build → ctest | 0 / 0 / 8 | FAIL, same assertion (`build-3-unpatched-p1785.log`, `test-3-unpatched-p1785.log`) |
| 4b re-apply | `git apply fix-p1785.patch` → build → ctest | 0 / 0 / 0 | all tests pass (`build-4-repatched-p1785.log`, `test-4-repatched-p1785.log`) |
| 4b, all three fixes reverted at once | `git apply -R final-fix-all.patch` → build → ctest | 0 / 0 / 8 | 4 regressions FAIL (`test-20-unpatched-all.log`) |
| 4b, all three fixes restored | `git apply final-fix-all.patch` → build → ctest | 0 / 0 | see the table at the end |

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

**Evidence.**

| check | command | exit | result |
| --- | --- | --- | --- |
| 4b revert | `git apply -R fix-p1784.patch` → build → ctest | 0 / 0 / 8 | FAIL `theFrameArrayIsNotReadAsAFlatFloatArray`: "the stream must not read a SampleFrame array through a float*" (`build-9-unpatched-p1784.log`, `test-9-unpatched-p1784.log`) |
| 4b re-apply | `git apply fix-p1784.patch` → build → ctest | 0 / 0 / 0 | all tests pass (`build-10-repatched-p1784.log`, `test-10-repatched-p1784.log`) |
| 4b, all three fixes reverted at once | `git apply -R final-fix-all.patch` → build → ctest | 0 / 0 / 8 | 4 regressions FAIL (`test-20-unpatched-all.log`) |
| 4b, all three fixes restored | `git apply final-fix-all.patch` → build → ctest | 0 / 0 | see the table at the end |

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

**Fix.** `src/core/LinkAudioStream.cpp` / `include/LinkAudioStream.h`:

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

**Regressions.**

* `LinkAudioStreamTest::aSampleRateChangeDropsTheOldRateRxFrames` — the peer queues 8 packets
  (1024 frames) at 48 kHz; the device then changes to 44.1 kHz and the master mix must stay
  silent (the queued 48 kHz frames must not be played at 44.1 kHz). RED: the mix carried 0.5.
* `LinkAudioStreamTest::aSampleRateChangeDoesNotSendOldRateFramesAsNewRate` — 40000 frames of
  0.5 at 48 kHz (more than the I/O thread sends in one wake, so some are still queued when the
  rate changes), then the rate changes to 44.1 kHz. Every packet the peer receives must carry the
  samples of the rate in its own header, and the stream must come back at the new rate. RED: 256
  packets carried old-rate samples under the new rate.

**Evidence.**

| check | command | exit | result |
| --- | --- | --- | --- |
| 4b revert | `git apply -R fix-p14145.patch` → build → ctest | 0 / 0 / 8 | FAIL both: `f.left()` 0.5 instead of 0, and `check.mismatched` 256 instead of 0 (`build-12-unpatched-p14145.log`, `test-12-unpatched-p14145.log`) |
| 4b re-apply | `git apply fix-p14145.patch` → build → ctest | 0 / 0 | all 18 tests pass; the run ended in the pre-existing exit-time segfault described below (`build-13-repatched-p14145.log`, `test-13-repatched-p14145.log`) |
| 4b, all three fixes reverted at once | `git apply -R final-fix-all.patch` → build → ctest | 0 / 0 / 8 | 4 regressions FAIL (`test-20-unpatched-all.log`) |
| 4b, all three fixes restored | `git apply final-fix-all.patch` → build → ctest | 0 / 0 | see the table at the end |

---

## Final state

| check | command | exit | result |
| --- | --- | --- | --- |
| product build | `cmake --build build --target zene -j8` | 0 | built (`build-21-repatched-all.log`) |
| affected family | `cd build/tests && ctest -R '^LinkAudioStreamTest$' --output-on-failure` | 0 | 1 test binary, 18 test functions, all passed (`test-21-repatched-all.log`) |
| gates | `python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` | 0 | every row PASS (`gate-2.log`) |
| complexity | `python3 -m lizard src/core/LinkAudioStream.cpp tests/src/core/LinkAudioStreamTest*.{cpp,h}` | 0 | no function over CCN 10 |
| file length | `bash tests/file-length-gate.sh --check` | 1 | no `LinkAudioStream*` file over 500 code lines (the gate's own train-wide red is pre-existing) |

### A pre-existing failure in this test binary (not caused by these fixes)

`LinkAudioStreamTest` ends in a segfault in the engine's own audio thread
(`AudioEngineWorkerThread::drainDeferredPlayHandleEdits()`) on some runs, *after* every test
function has passed. It is present on the untouched base commit `b61e5f965` as well: 2 of 10 runs
crash on the base (`orig-run-1.log` … `orig-run-10.log`) and 1 of 10 runs crash with these fixes
(`final-run-1.log` … `final-run-10.log`). The backtraces are in `gdb-1.log`. It is an engine
shutdown race, outside the scope of these three findings, and it is reported here rather than
worked around.

## Self-check

* **The regression test FAILS with the product change reverted and PASSES with it (exit codes).**
  Reverting the whole product diff (`git apply -R final-fix-all.patch`, build exit 0) and running
  the family gives ctest exit 8 with exactly the four new regressions failing
  (`test-20-unpatched-all.log`): `droppedGapSilenceIsCountedAsAnOverrun` (128 vs 1152),
  `theFrameArrayIsNotReadAsAFlatFloatArray` (the source still punned the array),
  `aSampleRateChangeDropsTheOldRateRxFrames` (0.5 vs 0) and
  `aSampleRateChangeDoesNotSendOldRateFramesAsNewRate` (256 mismatched packets vs 0). Re-applying
  it (`git apply final-fix-all.patch`, build exit 0) gives ctest exit 0 with all 18 test
  functions passing (`test-21-repatched-all.log`). Each bug was also proven on its own, before
  the other two were in the tree (see the per-bug tables).
* **The test is deterministic: no wall-clock timing, sleeps or races.** Every value the
  regressions depend on is injected: the sequence numbers, the frame counts, the sample values
  and the rate in each packet header. The waits are bounded waits for an *observable* — the
  stream's own `packetsReceived` counter (`waitForPackets()`) and the datagrams waiting at the
  peer's socket (`drainPeerPackets()`) — never a sleep whose duration decides the result, and
  never a fixed period. The RX regression waits for the counter to reach 8 before the rate
  changes, so the 1024 queued frames are certainly in the ring. The TX regression fills the ring
  with 40000 frames, which is more than the I/O thread can send in one wake (256 packets), so
  frames are certainly still queued when the rate changes.
* **The fix is at the cause and complete.** `process()` is the only caller of `sendFrames()` and
  `receivePeriod()`; `addFromRing()` is called only by `receivePeriod()`; `insertGapSilence()` is
  called only by `acceptPacket()`; `onRateChanged()` is called only by `process()` (the engine
  enters the stream through `LinkAudioStream::tap()`, `include/LinkAudioStream.h:189`, and
  `src/core/AudioEngine.cpp` calls that — unchanged); `txRateForNextFrames()` is called only by
  `sendPending()`. The TX ring is produced by the audio thread and consumed by the I/O thread, so
  only the I/O thread drops TX frames (`m_tx.skip()`), and the RX ring is produced by the I/O
  thread and consumed by the audio thread, so only the audio thread drops RX frames
  (`m_rx.skip()`) — no ring is touched from the wrong side. The header and the definition changed
  together, and `zene` and the test were rebuilt after every change.
* **No collateral damage.** Nothing about the saved format, the projects, the control commands or
  the undo stack changed: no new command, no new file format, no new limit that another caller
  can hit. The conversion buffer (`m_txBuf`) is allocated once in the constructor and chunked at
  1024 frames, so a period of any length is still converted in full; the ring's capacity, the
  jitter buffer's target and the counters are unchanged. The existing tests of the family
  (`loopbackExchangeIsSampleExact`, `audioThreadAllocatesNothing`,
  `realtimeStressReportsBoundedUnderrunsAndLatency`, `disablingWhileFloodedJoinsPromptly`,
  `packetsFromAStrangerOrAtAnotherRateAreCounted`, `pcmStreamsWhileLinkSyncIsActive`,
  `commandsAreRegisteredAndUndoable`, `theStreamLeavesLinkSyncAlone`) still pass unchanged.
* **Verdict evidence (the code, not a claim).** new-p17-85: `AudioRing::writeSilence()`
  (`src/core/LinkAudioCodec.cpp:172`) returns the frames it accepted, and the old
  `acceptPacket()` threw that value away. new-p17-84: the old `process()` read
  `reinterpret_cast<float*>(frames)` and the `static_assert` next to it only constrained a size.
  new-p14-145: the old `process()` stored the new rate and nothing else, and the old
  `sendPending()` stamped the packets with the rate it read *after* taking the frames from the
  ring. All three are the code the fixes replace.
* **Realtime.** The audio-thread path is `tap()` → `process()` → `sendFrames()` /
  `receivePeriod()` → `addFromRing()`, plus `onRateChanged()`. None of them allocates, frees,
  locks, waits, logs or does I/O: the only allocation is `m_txBuf` in the constructor, the ring
  buffers were already preallocated, and the new state is plain members and lock-free atomics
  (`m_txStamp`, `m_txFlushedEpoch`). The I/O-thread path (`sendPending()` →
  `txRateForNextFrames()`) touches only its own ring and the atomics.
* **Qt 5.15 / MSVC / macOS portability.** The fix uses no new API: `QUdpSocket` and
  `QHostAddress` are Qt 5 and Qt 6 alike, and the test support header uses only
  `QElapsedTimer`, `QUdpSocket` and `QTest::qWait`. The product change touches no Windows or
  POSIX header. `std::uint64_t` shifts and `<utility>`'s `std::pair` are C++11/C++17 standard.
* **Undo.** No command was added or changed, so the undo stack is untouched (one honest step, as
  before) and the control-command snapshot canary does not move.
