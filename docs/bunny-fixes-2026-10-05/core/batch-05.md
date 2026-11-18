# Core lane — batch 05 (2026-10-05)

Six findings. One fixed (the RetroMidiRing capacity overflow), one already covered by an
earlier commit on this branch, one false positive, three real but not deterministically
testable headlessly.

### Cluster 5243 — A shared-memory size mismatch leaves the host waiting forever
Verdict: FALSE-POSITIVE
Evidence: `include/RemotePluginClient.h:293-297` — the `IdStartProcessing` case calls
`doProcessing();` and then sets `reply_message.id = IdProcessingDone; reply = true;` in the
same case block, and `sendMessage(reply_message)` runs unconditionally afterwards
(`include/RemotePluginClient.h:314-317`). The early `return` at
`include/RemotePluginClient.h:358-361` exits `doProcessing()` only — it does not return out
of `handleMessage()`, so the host's `waitForMessage(IdProcessingDone)`
(`src/core/RemotePlugin.cpp:434`) always completes. The report misread the return as
escaping the message handler. No reply can be lost on this path.
Commit: none
Test: none
Red/green: none

### Cluster 5274 — Audio ring snapshots can race with the producer
Verdict: REAL-UNTESTED
Evidence: `include/RetroAudioRing.h:163-166` copies non-atomic `SampleFrame` slots while
`push()` writes them at `include/RetroAudioRing.h:110`; the sequence re-check at
`include/RetroAudioRing.h:170` detects the overlap after the fact but, as the finding says,
concurrent non-atomic access is a data race in C++ and the seqlock does not make it legal.
The strict fix is the MIDI ring's shape — slots held in atomic words
(`include/RetroMidiRing.h:249-253`, whose `copyOut` at `include/RetroMidiRing.h:200` has the
same sequence structure but atomic storage) — which is a rewrite of the producer's hot path,
not a defect-sized change. A headless test cannot demonstrate the race deterministically (it
requires winning a specific interleaving), so per the lane procedure no product code was
changed.
Commit: none
Test: none
Red/green: none

### Cluster 5278 — MIDI ring capacity rounding has the same overflow hang
Verdict: FIXED
Evidence: `include/RetroMidiRing.h:239-244` (pre-fix) shifted `result <<= 1` with no
overflow check, so a capacity above the largest representable power of two wrapped the loop
variable to zero and the constructor never returned — the same defect already fixed in
`SampleFrameRingBuffer.h:144` and `RetroAudioRing.h:234`. The guard now throws
`std::length_error` for a capacity above `(size_t max >> 1) + 1`, matching the siblings, and
the header pulls in `<limits>` and `<stdexcept>`.
Commit: 66a5c11f5
Test: RetroMidiRingTest::refusesACapacityThatCannotBeRoundedUp
Red/green: RED `cd build/tests && QT_QPA_PLATFORM=offscreen timeout 20 ./RetroMidiRingTest refusesACapacityThatCannotBeRoundedUp` -> EXIT=124 (hung; a full ctest run spun 279 s before being killed). GREEN `cmake --build build --target RetroMidiRingTest zene` -> EXIT=0; `cd build/tests && ctest -R '^(RetroMidiRingTest|MidiRetroCaptureTest|RetroMidiCaptureCommandsTest|RetroAudioCaptureTest)$'` -> EXIT=0 (4/4 passed); `bash tests/complexity-gate.sh --check` -> EXIT=0; `bash tests/file-length-gate.sh --check` -> EXIT=0.

### Cluster 2475 — Capture ring overflow silently shortens and compresses takes
Verdict: SKIPPED-SCOPE
Evidence: the drop-newest counter exists and is accurate
(`include/SampleFrameRingBuffer.h:95`, exposed at `include/SampleRecordAccumulator.h:126` as
`framesDropped()`), and the record handle indeed ignores it —
`src/core/SampleRecordHandle.cpp:191` calls `m_accum->append(...)` and discards the
"frames written" result, so later accepted frames are appended directly after earlier ones.
Both proposed remedies ("preserve dropped time in the result" or "surface an explicit
recording-overflow error") change the recording result or the control-surface schema, which
is rule 8 / a design change: the only existing report of a drop counter for the *other*
recorder is a control-command result field at `src/core/ControlCommandsRecorder.cpp:71`, so
surfacing this one means adding a command result field. The counter's absence from any caller
was confirmed by a bounded search of `src` and `tests`.
Commit: none
Test: none
Red/green: none

### Cluster 5310 — Ring-buffer `available()` can underflow during concurrent reads and writes
Verdict: REAL-UNTESTED
Evidence: `include/SampleFrameRingBuffer.h:119-120` samples the write position and then the
read position as two independent loads, so a consumer that advances `readPos` past the
sampled write position makes the unsigned subtraction wrap to a near-maximum count — the
interleaving described in the report is possible. The one-line correction is to load
`m_readPos` before `m_writePos` (the standard SPSC observer ordering, which guarantees the
sampled write position is not older than the sampled read position). No headless test can
force that interleaving deterministically, so per the lane procedure no product code was
changed. Note for the parent: production impact is nil today — `available()` has no caller in
`src` (the accumulator drains with `read()`), only in tests.
Commit: none
Test: none
Red/green: none

### Cluster 4414 — Extreme ring capacities can hang construction
Verdict: ALREADY-FIXED
Evidence: `include/SampleFrameRingBuffer.h:144-147` already throws `std::length_error` when
`value > (size_t max >> 1) + 1`, so the shift loop at line 149 cannot wrap. The guard is
noted as pre-existing in the commit for the same defect in the sibling ring (ccc9513ca), and
`tests/src/core/RecordRingBufferTest.cpp:72` and
`tests/src/core/InputChannelRingTest.cpp:50` both cite it as the reference implementation.
This finding and cluster 5278 are the same defect in the same family; only the MIDI ring
still lacked the guard.
Commit: none (guard pre-dates this batch)
Test: none
Red/green: none

Leftovers: cluster 5274 and 5310 are real but have no deterministic headless test, so they
are left for the parent — 5310 needs only the two loads in
`include/SampleFrameRingBuffer.h:119-120` swapped, and 5274 needs the `RetroAudioRing` slots
reworked into atomic words like `RetroMidiRing`'s. Cluster 2475 needs a control-surface
decision before it can be fixed. Nothing half-done; the tree is clean apart from this report.
