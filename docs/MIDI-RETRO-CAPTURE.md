# Retrospective MIDI capture — design

**Status: design only, no C++ implementation.** Branch `next/midi-retro`, base `3fd5a4f3c`
(the release-line tip), confirmed in-worktree with `git rev-parse HEAD~1` →
`3fd5a4f3c8acb9dcb26d9d27a7fd7d482ad8bde2`, whose subject is
`3fd5a4f3c fix(gates): the manifest entry and the report citation, which the earlier commit left behind`,
and `git rev-parse --abbrev-ref HEAD` → `next/midi-retro`. (This line used to cite
`git log --oneline -1`, which confirms the base only until the document is committed; at any commit
after it, that command prints the document's own commit. `git rev-parse HEAD~1` is true at the stated
base and at every commit on top of it.)

Every claim about the tree carries a `path:line` that was read on **this** branch. Claims that could
not be settled without a build are marked **UNVERIFIED** and are questions for the implementation
lane, not prose. A "claims and how each was checked" table is at the end (§9), including the four
knowledge-base claims about `RecordRingBuffer` that this document was asked to verify.

The item is the owner's decision of 2026-09-12 (`../NEXT-RELEASE-HANDOFF.md:92-95`): *"retrospective
MIDI capture. No dependency, no licence question, days–weeks. A bounded recent-event ring per port,
written to a clip on demand, off by default, allocation-free on the MIDI thread;
`include/RecordRingBuffer.h` is the precedent."* §7 states what it must not become, with the
handoff sentence being obeyed quoted.

---

## 1. What exists today: `RecordRingBuffer`, verified claim by claim

`include/RecordRingBuffer.h` (180 lines) is a bespoke, dependency-free, single-class SPSC ring of
`sample_t` (= `float`, `include/LmmsTypes.h:39`). Its real API:

| Member | Line | Real signature |
| --- | --- | --- |
| ctor | `include/RecordRingBuffer.h:62` | `explicit RecordRingBuffer(std::size_t minCapacityFrames)`; `m_capacity(nextPowerOfTwo(v))`, `m_mask(m_capacity - 1)`, `m_data(m_capacity, 0.f)` (`:63-65`) |
| copy | `:69-70` | copy ctor and assignment `= delete` |
| `write` | `:74` | `bool write(sample_t frame) noexcept` — false and an overflow bump when full (`:78-82`) |
| `writeBlock` | `:91` | `std::size_t writeBlock(const sample_t* src, std::size_t frames) noexcept` → forwards to `writeStrided(src, 1, frames)` (`:93`) |
| `writeStrided` | `:99` | `std::size_t writeStrided(const sample_t* src, std::size_t stride, std::size_t frames) noexcept` — writes the free prefix, counts the remainder (`:103-114`) |
| `read` | `:119` | `std::size_t read(sample_t* dst, std::size_t frames) noexcept` — 0 when empty |
| `available` | `:134` | `std::size_t available() const noexcept` |
| `capacity` | `:140` | `std::size_t capacity() const noexcept` |
| `overflowCount` | `:143` | `std::uint64_t overflowCount() const noexcept` |
| `reset` | `:150` | `void reset() noexcept` — "Only valid while neither producer nor consumer is running" (`:148-149`) |
| members | `:165-174` | `const std::size_t m_capacity;` `const std::size_t m_mask;` `std::vector<sample_t> m_data;` `alignas(64) std::atomic<std::uint64_t> m_writePos{0};` … `m_readPos{0};` … `m_overflow{0};` |

Its one consumer is the recording prototype: `TrackRecorder` holds it as
`std::unique_ptr<RecordRingBuffer> m_ring; // allocated in the constructor`
(`include/TrackRecorder.h:110`, constructed at `src/core/audio/TrackRecorder.cpp:41`), and the audio
thread only calls `m_ring->writeStrided(...)` (`src/core/audio/TrackRecorder.cpp:159-160`).

### The four knowledge-base claims, checked against the file

| KB claim | Verdict | Evidence |
| --- | --- | --- |
| "lock-free SPSC ring buffer" | **Partly verified.** SPSC: yes — one producer-owned index and one consumer-owned index, documented at `include/RecordRingBuffer.h:46-52`, and the class is non-template with a deleted copy ctor (`:69-70`). "Lock-free" is **asserted by construction, not by the code**: the file carries no `static_assert(std::atomic<std::uint64_t>::is_always_lock_free)` and no `is_lock_free()` check anywhere. On a target where 64-bit atomics are lock-free (x86-64, aarch64) it is lock-free because `write`/`read` take no lock and cannot block. | `include/RecordRingBuffer.h:46-52, 69-70, 74-131, 172-174`; `grep -n "static_assert\|is_lock_free" include/RecordRingBuffer.h` → no output |
| "allocation only in the ctor" | **Verified.** The only allocation in the file is the member initialiser `m_data(m_capacity, 0.f)` (`:65`). Every other member function is `noexcept` and indexes into `m_data`. The *placement* obligation is the owner's: the header says the constructor "must run off the audio thread" (`:43-44`), and `TrackRecorder` satisfies it by constructing the ring in its own constructor (`src/core/audio/TrackRecorder.cpp:41`). | `include/RecordRingBuffer.h:43-44, 62-67, 74-155` |
| "drop-newest overflow" | **Verified.** `write` returns false and counts when full instead of overwriting (`:78-82`); `writeStrided` writes only the free prefix and counts the rest (`:103-114`); the class comment says "unread data is never overwritten and the producer never blocks" (`:54-56`). | `include/RecordRingBuffer.h:54-56, 78-82, 103-114` |
| "`alignas(64)` false-sharing avoidance" | **Verified, with a precision correction.** All three index atomics carry `alignas(64)` (`:172-174`), which separates the three counters from each other onto their own cache lines. It does **not** align the data slots — `m_data` is a plain `std::vector<sample_t>` (`:168`) whose slots are shared between producer and consumer by design — and `alignas(64)` is a minimum, so on a machine with a larger cache line the guarantee is weaker than the phrase suggests. | `include/RecordRingBuffer.h:168, 172-174` |

`m_capacity`, `m_mask` and the vector header occupy the first 40 bytes, so the three atomics land at
offsets 64/128/192 with padding in between; that padding is the false-sharing avoidance, and by the
declared layout it costs **192 bytes per ring** (24 bytes before the first atomic, 56 between the first
and second, 56 between the second and third, and 56 trailing, where `sizeof` rounds the class up to its
64-byte alignment: 256 total against 64 bytes of payload). **Net result: two of the four claims stand
exactly as written — allocation only in the ctor, and drop-newest overflow. The other two need the
caveats above: nothing in the file *machine-asserts* lock-freedom (`grep -n "static_assert\|is_lock_free"
include/RecordRingBuffer.h` → no output, exit 1) — although the file does say "lock-free" in prose twice,
at `:2` and `:41` — and the alignment applies to the three indices, not to the slots.**

### Is it usable as-is for MIDI events? No — a second, event-typed ring is needed

Three reasons, each from the file:

1. **The element type is fixed and is not an event.** `sample_t` = `float` (`include/LmmsTypes.h:39`);
   `bool write(sample_t frame)` (`:74`); `std::vector<sample_t> m_data` (`:168`). The class is not a
   template, so it cannot be instantiated for another element, and it has no field for a timestamp —
   a MIDI event without a time is not placeable in a clip.
2. **`MidiEvent` must not be stored by value in a long-lived buffer.** It carries two raw pointers —
   `const char* m_sysExData;` and `const void* m_sourcePort;` (`include/MidiEvent.h:217-218`). The
   `sourcePort` handed to the port on the ALSA-sequencer path is `&ev->source`, the address of a
   field of the sequencer's own event struct (`src/core/midi/MidiAlsaSeq.cpp:531`, pointer inserted
   into the `MidiEvent` at `:547`). Retaining that across a bounded window would retain a dangling
   pointer. The ring's element must be a **POD snapshot** of the semantic fields.
3. **The overflow policy is the wrong one.** A retrospective buffer must hold the *most recent* N
   events; `RecordRingBuffer` deliberately keeps the *oldest* and drops the newest
   (`:54-56`). Copying its policy would make a "capture what I just played" feature capture what the
   user played first and then stop. §3 chooses **drop-oldest** and says how to get it without a
   second producer-side index.

So: the precedent's *properties* are what to copy — allocation once at construction, one
relaxed-loaded index per side, acquire/release hand-off, a monotonic counter for what was lost, no
locks, no syscalls — and not the class.

**Existing event-typed rings, so that this does not invent a third shape.** Two exist, and neither is
the right thing to lift:

- `LocklessRingBuffer<T>` / `LocklessRingBufferReader<T>` (`include/LocklessRingBuffer.h:39-88`) is
  already used for an event type — `LocklessRingBuffer<ScriptMidiEvent> m_midiIn;`
  (`include/ScriptEngine.h:249-250`, element struct `ScriptMidiEvent` at `:124-130`, a four-int32 POD
  with no pointers — the closest existing *element shape* to what §3 needs) — written by
  `ScriptEngine::pushMidiInEvent` at `src/core/ScriptEngine.cpp:764-767`. But it is a thin wrapper over
  a **git submodule**: `#include <ringbuffer/ringbuffer.h>` (`include/LocklessRingBuffer.h:31`),
  built from `src/3rdparty/ringbuffer` (`src/3rdparty/CMakeLists.txt:16,20`; submodule declared in
  `.gitmodules`). That submodule's directory is **empty in this worktree** (`ls -la
  src/3rdparty/ringbuffer` → only `.` and `..`), so `ringbuffer/ringbuffer.h` could not be read here —
  **UNVERIFIED**. It also drags Qt synchronisation into the reader: `QWaitCondition m_notifier`
  (`include/LocklessRingBuffer.h:65`) and a reader `waitForData()` that takes a `QMutex`
  (`:79-85`), with `write(..., notify=true)` calling `wakeAll()` (`:54-60`). None of that belongs on
  the MIDI thread, and the owner's "no dependency" points away from a submodule.
- `SpscRingBuffer<T, Capacity>` (`src/wasm/WasmSpscRingBuffer.h:46`) is a header-only,
  dependency-free SPSC template whose `push` returns false when full (`:52-64`) and whose indices are
  modulo-wrapped (`:55, 74, 87`). It lives in `namespace lmms::wasm` and is compiled into the WASM
  sandbox; its capacity is a template parameter and its element must be default-constructible in a
  `std::array<T, StorageSize>` (`:95`). Its shape is close to what this feature needs, but it is not
  the class to reuse from outside `src/wasm`.
- For completeness, the third ring in the tree is `SampleFrameRingBuffer` — the same policy and the
  same construction discipline as `RecordRingBuffer`, for stereo frames
  (`include/SampleFrameRingBuffer.h:41-47`).

---

## 2. The MIDI path, and where a capture hook can honestly sit

### The port/client abstraction

- Base class `MidiClient` (`include/MidiClient.h:45`) with the static opener
  `openMidiClient()` (`:111`); the raw-parser intermediate `MidiClientRaw : MidiClient` at `:124`
  with `parseData(unsigned char)` (`:139`) and the private `processParsedEvent()` (`:147`).
- **Concrete subclasses: 8.** Count command and output:

  ```
  $ grep -rn "class .*public MidiClient" --include=*.h include | grep -v 'class MidiClientRaw'
  include/MidiSndio.h:44:class MidiSndio : public QThread, public MidiClientRaw
  include/MidiJack.h:50:class MidiJack : public QThread, public MidiClientRaw
  include/MidiApple.h:45:class MidiApple : public QObject, public MidiClient
  include/MidiDummy.h:35:class MidiDummy : public MidiClientRaw
  include/MidiAlsaRaw.h:45:class MidiAlsaRaw : public QThread, public MidiClientRaw
  include/MidiWinMM.h:45:class MidiWinMM : public QObject, public MidiClient
  include/MidiAlsaSeq.h:48:class MidiAlsaSeq : public QThread, public MidiClient
  include/MidiOss.h:42:class MidiOss : public QThread, public MidiClient
  ```

  Five derive through `MidiClientRaw` (Sndio, Jack, Dummy, AlsaRaw, Oss) and three straight from
  `MidiClient` (Apple, WinMM, AlsaSeq). One client is open at a time — the opener picks one.
  The accessor at runtime is `AudioEngine::midiClient()`
  (`src/core/ControlCommandsSettings.cpp:373`, `engine->midiClient()`).

### The receiving thread, the receive entry point, and the event type

| What | Where |
| --- | --- |
| **ALSA-sequencer input thread** (the Linux default client) | `src/core/midi/MidiAlsaSeq.cpp:469` `void MidiAlsaSeq::run()` — the object is a `QThread` (`include/MidiAlsaSeq.h:48`) started at idle priority, `src/core/midi/MidiAlsaSeq.cpp:119` `start( QThread::IdlePriority );` |
| Its poll/fetch | `src/core/midi/MidiAlsaSeq.cpp:504` `m_seqMutex.lock();` → `:507` `snd_seq_event_input_pending(...)` → `:510` `snd_seq_event_input( m_seqHandle, &ev )` → unlock → dispatch, re-lock at `:623` |
| Its dispatch | `src/core/midi/MidiAlsaSeq.cpp:521-533` resolves the destination `MidiPort*` and the source address; `:535-538` `if( dest == nullptr ) continue;`; `:540` `switch( ev->type )` with `dest->processInEvent( MidiEvent(...) )` from `:543` |
| **Raw-client receive entry point** | `src/core/midi/MidiClient.cpp:240` `void MidiClientRaw::processParsedEvent()` — parser entry `::parseData( const unsigned char c )` at `:104`; `MidiLearn::instance()->handleMidiEvent(...)` at `:244`; then `midiPort->processInEvent(...)` for every subscribed port at `:248` |
| **The event type received** | `lmms::MidiEvent` (`include/MidiEvent.h:36`); type enum `MidiEventTypes` values `0x80`–`0xFF` (`include/Midi.h:33-61`); private layout `m_type`, `m_metaEvent`, `int8_t m_channel` (`include/MidiEvent.h:209`), a union `{int16_t m_param[2]; uint8_t m_bytes[4]; int32_t m_sysExDataLen;}` (`:210-215`), then `const char* m_sysExData; const void* m_sourcePort; Source m_source;` (`:217-221`) |
| Port-level masking, then the processor | `src/core/midi/MidiPort.cpp:130` `MidiPort::processInEvent( const MidiEvent& event, const TimePos& time )` → `:152` `m_midiEventProcessor->processInEvent( inEvent, time )`; declared `include/MidiPort.h:114`; the mask is `isInputEnabled() && ( inputChannel() == 0 \|\| inputChannel()-1 == event.channel() )` at `src/core/midi/MidiPort.cpp:133-134` |
| The two `MidiEventProcessor` implementors (i.e. the two things that own a `MidiPort`) | `MidiController : Controller, MidiEventProcessor` (`include/MidiController.h:47`, member `MidiPort m_midiPort;` at `:94`), `InstrumentTrack : Track, MidiEventProcessor` (`include/InstrumentTrack.h:62`, member `MidiPort m_midiPort;` at `:295`) |

There is **no central port registry**: a `MidiPort` is a member of its owner and is constructed with it
(`src/core/midi/MidiPort.cpp:45-48`), so "per port" has exactly two owners.

### Where the real-time constraint applies — the honest inventory

- **The MIDI input thread already locks.** `MidiAlsaSeq::run()` takes `m_seqMutex` around the
  `snd_seq_event_input*` calls (`src/core/midi/MidiAlsaSeq.cpp:504`, released before dispatch,
  re-taken at `:623`), and the same mutex is taken in eight other members (`:144, 239, 251, 320, 349,
  384, 435, 639, 665`). This is pre-existing and is not what this feature introduces.
- **The MIDI input thread is not allocation-free end-to-end today.** An event that reaches a port
  goes `MidiPort::processInEvent` (`src/core/midi/MidiPort.cpp:152`) →
  `InstrumentTrack::processInEvent` (`src/tracks/InstrumentTrack.cpp:394`), whose `MidiNoteOn` case
  calls `NotePlayHandleManager::acquire(...)` (`src/tracks/InstrumentTrack.cpp:425`) and
  `Engine::audioEngine()->addPlayHandle( nph )` (`:443`). No test in this tree measures allocation on
  that path.
  **The constraint this design accepts is therefore narrow and exact: the new hook must be
  allocation-free, lock-free and syscall-free, and must add O(1) work per event — it does not, and
  must not be claimed to, make the whole MIDI thread realtime-clean.**
- **The existing working example of that constraint is MIDI learn.** `MidiLearn::handleMidiEvent` is
  called from exactly the two input paths (`src/core/midi/MidiClient.cpp:244` for raw clients and
  `src/core/midi/MidiAlsaSeq.cpp:582` for the sequencer), and its header documents the contract:
  "MIDI input thread - `handleMidiEvent()` (`MidiAlsaSeq::run()` for the sequencer client,
  `MidiClientRaw::processParsedEvent()` for the raw clients). It does one atomic load per event while
  disarmed and, when armed and a control is focused, records the control-change in a lock-free slot.
  It allocates nothing, takes no lock, never waits, and never touches a model, a Song or a MidiPort."
  (`include/MidiLearn.h:58-63`). The state is `std::atomic<bool> m_enabled{false};`
  (`include/MidiLearn.h:126`).
- The input threads are not all Qt threads: `tests/src/core/MidiLearnThreadTest.cpp:74-75` documents
  that "the real input paths are not all Qt threads - MidiJack delivers from a JACK callback thread",
  which is why that test delivers from a plain `std::thread`.

### The seam: two call sites, not one, and *before* the routing mask

`MidiLearn` is the **precedent for the thread contract**, not the seam, because it is offered a
different event set on each path: the raw path hands it every parsed event
(`src/core/midi/MidiClient.cpp:244`) while the sequencer path hands it only the controller case
(`src/core/midi/MidiAlsaSeq.cpp:578-584`). A capture that adopted `MidiLearn`'s call site verbatim
would silently capture CC only on the default Linux client.

The right seams, and why they are not the `MidiPort`:

1. `src/core/midi/MidiClient.cpp:240-249` — one call site covers all five raw clients, before the
   per-port loop, so the capture sees the event even when no port is subscribed.
2. `src/core/midi/MidiAlsaSeq.cpp:469`'s loop body, **above the `if( dest == nullptr ) continue;` at
   `:535-538` and above the `switch( ev->type )` at `:540`** — so an event addressed to a port the app
   does not listen to, or an event type the switch does not handle, is still captured once.
   `ev->time.tick` is available there for free: the note cases already use it as
   `TimePos( ev->time.tick )` (`:549`, `:559`) while the CC/PC/chan-pressure/pitch-bend cases pass the
   default `TimePos()` (`:583`, `:593`, `:602`, `:609`), i.e. the tick is present in the event and is
   currently discarded.

Capturing at `MidiPort::processInEvent` (`src/core/midi/MidiPort.cpp:130`) was considered and
rejected: it runs *after* the input mask (`:133-134`), so a track that is not armed for that channel
would capture nothing — which is precisely the failure this feature exists to remove ("I played it and
nothing was armed"). Capturing before the `continue` at `:535` also means **"per port" is read as per
*input source*, not per app `MidiPort`**: one ring per open client, with a `source` field in the
element so the ALSA source port (`ev->source.port`, resolved at `src/core/midi/MidiAlsaSeq.cpp:521-533`)
keeps the granularity the owner's phrase implies. Raw clients have no source id and write 0.

---

## 3. The design

### 3.1 Element type

A 16-byte POD snapshot. It is deliberately **not** a `MidiEvent` (see §1: two pointers) and it is
closer to the existing event POD `ScriptMidiEvent` (`include/ScriptEngine.h:124-130`) than to a sample
frame:

```cpp
struct RetroMidiEvent            // snapshot; never a MidiEvent (MidiEvent.h:217-218)
{
    std::uint32_t tick;          // transport tick the event was stamped with
    std::uint16_t source;        // ALSA source port, or 0 for raw clients
    std::uint8_t  type;          // MidiEventTypes, Midi.h:33-61 (0x80..0xFF)
    std::uint8_t  channel;       // MidiEvent::m_channel is int8_t (MidiEvent.h:209); widened here
    std::uint8_t  param1;        // key / controller / program / pressure / pitch-bend lo
    std::uint8_t  param2;        // velocity / value / pitch-bend hi
    std::uint8_t  flags;         // bit0 = Source::External, bit1 = a SysEx was seen and dropped
    std::uint8_t  pad;
    std::uint16_t reserved;
};
static_assert(sizeof(RetroMidiEvent) == 16, "ring slots are the unit of the capacity budget");
```

The eight-bit params are lossless for channel messages because the accessors already mask to seven
bits — `velocity()` returns `m_data.m_param[1] & 0x7F` (`include/MidiEvent.h:123`),
`controllerNumber()` `param(0) & 0x7F` (`:155`), and the clients that construct events pass 7-bit
values (`src/core/midi/MidiAlsaSeq.cpp:543-608`, `src/core/midi/MidiClient.cpp:240-249`). The one
14-bit message is pitch bend: the sequencer path already folds the 8192 offset into the raw param
(`src/core/midi/MidiAlsaSeq.cpp:606-608`, `ev->data.control.value + 8192`), so the element must split
it (`param1 = v & 0x7F`, `param2 = (v >> 7) & 0x7F`) and the writer must rejoin it. SysEx is **not**
captured in the smallest version — it is variable-length and would need a second bounded byte store —
and `flags` records that one was seen and dropped rather than losing it silently.

### 3.2 Size, and where it is allocated

- Capacity is a compile-time constant per ring, a power of two, `1u << 13` = **8192 events × 16 B =
  128 KiB per ring**. The arithmetic behind that number: a dense two-hand performance is roughly
  10–20 events/s, so 8192 events is ~7–13 minutes of the densest playing; the memory is bounded and
  paid once per open client (one client is open at a time, §2). One constant to change if the owner
  wants ~64 KiB instead (`1u << 12`).
- **Allocated exactly once, in the constructor, off the MIDI thread and off the audio thread.** It is
  a member of the capture object, and the capture object is a member of the client (or of the
  `MidiPort`) built with it, mirroring `TrackRecorder`'s
  `std::unique_ptr<RecordRingBuffer> m_ring; // allocated in the constructor`
  (`include/TrackRecorder.h:110`). Concrete placement: one `RetroMidiCapture` member on `MidiClient`,
  next to the existing `std::vector<MidiPort *> m_midiPorts;` (`include/MidiClient.h:114`), so both
  seams in §2 reach it through `this` and it is destroyed with the client.
- The storage itself is a plain `std::array<RetroMidiEvent, Capacity>` or one `std::unique_ptr<...>`
  built in the ctor; **no container that can grow**, ever.

### 3.3 What must never allocate or lock on the MIDI thread

The whole `capture(const MidiEvent&, std::uint32_t tick) noexcept` path. Concretely, prohibited in
that function and everything it calls:

- any container growth (`std::vector::push_back`, `QString`, `QJsonObject`), any `new`/`delete`;
- any `QMutex`/`QWaitCondition`/`QMutexLocker`, any `std::mutex`, any `QSemaphore`;
- any journal or transaction call (`ProjectJournal`, `addJournalCheckPoint`), any `Model`, `Song`,
  `MidiPort`, `MidiClip` or `AutomatableModel` member access;
- any logging, any `ConfigManager` read; any `std::sort` over the window;
- and it must not copy the window — copying is the consumer's job (§3.5).

The only operations are relaxed/acquire atomic loads and stores and one 16-byte struct store.

### 3.4 Overflow policy: drop-OLDEST, and how to get it with one index

This is a deliberate departure from `RecordRingBuffer`'s documented policy
(`include/RecordRingBuffer.h:54-56`: "unread data is never overwritten"), and the departure is the
point: a retrospective buffer must hold the most recent N events.

- The writer owns one monotonically increasing counter `m_head` and stores into
  `m_slots[head & (Capacity - 1)]` (the mask, so the modulo is free — the same trick
  `RecordRingBuffer` uses at `include/RecordRingBuffer.h:83, 107, 127`), then publishes `head + 1`
  with a release store. There is **no tail index on the producer side**, so the producer cannot be
  wrong about what the consumer has read, and there is no second atomic to share a cache line with.
- The consumer clamps `first = (head > Capacity) ? head - Capacity : 0` when it takes a snapshot.
  "Dropped the oldest" is thus enforced at read time, on the consumer's thread, by arithmetic.
- `m_overwritten` (relaxed `std::uint64_t`, incremented only when `head` crosses `Capacity`) is
  published so the UI and an agent can be told honestly how much of the past fell out of the window,
  exactly as `overflowCount()` is exposed on the sample rings
  (`include/RecordRingBuffer.h:143`, surfaced at `include/TrackRecorder.h:101`).

### 3.5 Taking the snapshot without locking the MIDI thread

The writer must be quiesced for the duration of a read — the ring is a single array and the writer
overwrites slots in place, so a live read could tear an element. The handshake keeps all the work on
the consumer:

1. Consumer (GUI/control thread) sets `m_snapshotPending = true` (release) and records a deadline.
2. Writer (MIDI thread), at the top of `capture(...)`, does one relaxed load of
   `m_snapshotPending`; while it is set it **drops** new events (counted in the same `m_overwritten`
   and in a separate `m_pausedDropped`) and writes nothing, then publishes `m_writerIdle = true`.
3. Consumer spins (bounded, e.g. a few milliseconds via `QDeadlineTimer`) on `m_writerIdle`, then
   reads the slots, then clears `m_snapshotPending`. If the deadline expires it **refuses typed**
   (`busy`) rather than reading a live ring — the same shape the registry already uses for
   `ControlErrorKind::Busy` (`include/ControlRegistry.h:56`).

The window is a bounded memcpy on the consumer and **one relaxed atomic load per event** on the MIDI
thread — exactly `MidiLearn`'s "one atomic load per event while disarmed"
(`include/MidiLearn.h:58-63`). The rejected alternative was to have the MIDI thread copy into a second
preallocated buffer: that moves a bounded-but-real memcpy onto the MIDI thread, which the owner's
constraint forbids.

**Timestamps.** Each element carries a tick, and the honest source of that tick is a new lock-free
publisher rather than a read of `Song::getPlayPos()`: that returns `const TimePos&` by reference from
live state (`include/Song.h:258-265`) and `include/Song.h` contains no atomics at all
(`grep -n atomic include/Song.h` → no output), so reading it from the MIDI thread would be a data
race. The change is one `std::atomic<std::uint32_t>` current-tick on the engine, written once per
period (relaxed store) on the audio thread where the per-period play position is already computed
(`src/core/Song.cpp:389` region) and read relaxed on the MIDI thread. On the ALSA-sequencer path
`ev->time.tick` is available directly and can be preferred. **Consequence to state in the UI:** with
the transport stopped, the published tick is constant and a capture would stack events at one
position; the smallest honest version therefore arms capture only while the transport is rolling.
**UNVERIFIED without a build:** the cost of the extra relaxed store per period and whether the
sequencer tick and the published tick agree in the same session — both are measurements for the
implementation lane.

### 3.6 The "off by default" switch: mechanism, home, and what this release's conventions favour

Both mechanisms exist in the tree; **the release's own conventions favour the runtime one, with a
persisted default — not a CMake option.**

- **Compile-time, and what it is used for here.** `OPTION(WANT_SESSION_VIEW ... OFF)`,
  `OPTION(WANT_STEM_SPLIT ... OFF)` and `option(ZENE_TELEMETRY ... ON)` (`CMakeLists.txt:120, 121,
  140`) gate a whole *data layer with dependencies* (Session View), a *large third-party runtime*
  (HTDemucs/ONNX), or *networking code*. A 128 KiB array with no dependency is none of those.
- **Runtime, off by default — the actual precedent.** MIDI learn: `std::atomic<bool> m_enabled{false};`
  (`include/MidiLearn.h:126`), flipped by the registered command `midi.learn_toggle`
  (`src/core/ControlCommandsSettings.cpp:392`), driven from the menu action `Edit > MIDI Learn` which
  *declares* that id (`src/gui/MainWindow.cpp:372`,
  `m_midiLearnAction->setData(QStringLiteral("midi.learn_toggle"))`) and whose slot invokes the same
  command (`src/gui/MainWindow.cpp:1390-1396`). Its docstring makes the point the design needs: mode
  state, not project state, so it records no transaction
  (`src/core/ControlCommandsSettings.cpp:400-402`).
- **Persistence already exists** if the arm should survive a restart:
  `ConfigManager::inst()->value(cls, attribute, default)` with `<class>/<attribute>` keys, read/written
  by `settings.get` / `settings.set` (`src/core/ControlCommandsSettings.cpp:117, 145`; key format
  validated at `:59-70`). An existing boolean example is
  `ConfigManager::inst()->value("ui", "mixerchanneldeletionwarning", "1")`
  (`src/gui/MixerChannelView.cpp:328`).

**Decision:** one `std::atomic<bool> m_armed{false}` in the capture object (the `MidiLearn` shape),
plus a persisted `midi/retrocapture` key read **once** in the capture object's constructor so the
setting has no per-event cost, plus the registered command in §5. No CMake option. If the owner later
wants a build-time kill switch it costs three lines, but nothing in this release's conventions
justifies one now.

### 3.7 The write-to-clip operation

The consumer holds a `std::vector<RetroMidiEvent>` (allocation is fine there — it is the GUI thread).
The operation reuses the **existing** note path rather than inventing one:

- Target: a `MidiClip`. Its note API is `Note* addNote(const Note&, const bool _quant_pos = true)`
  (`include/MidiClip.h:63`, defined `src/tracks/MidiClip.cpp:180`), with `NoteVector m_notes` exposed
  at `include/MidiClip.h:75`.
- The convention the registry already follows for a structural clip edit is: `clip->addJournalCheckPoint()`
  **before** the mutation, `clip->addNote(fresh, false)` (`false` = the caller's ticks are taken as
  given, not quantised), then `clip->dataChanged()` — at `src/core/ControlCommandsNotes.cpp:118`, `:125`
  and `:127` — which is exactly what `note.add` does, with the inverse recorded as `note.remove`
  (`:140`). Writing a whole capture therefore becomes **one journal checkpoint / one undo step**.
- `Note`'s constructor is `Note(length, pos, key, volume, panning, detuning)`
  (`include/Note.h:104-109`); the capture does not need panning or detuning.
- Note-on/note-off pairs are matched in arrival order into one `Note` each;
  `position = event.tick - windowStartTick`, `length = offTick - onTick` (minimum 1 tick). An unmatched
  note-on at the window edge is closed at the window end and counted in the result as
  `unmatched_ons`, so the caller can see the truncation rather than being told the capture was clean.

---

## 4. The smallest honest version

**Size band: 3–5 days for the ring plus the seam plus note materialisation; ~1 working week for the
smallest honest version (ring, arm switch, command, menu item, allocation test, this document's
update); ~2 weeks including the persisted setting, the per-source filtering and a headless sweep
run.** That is the owner's "days–weeks", and it is at the fast end of it for the reason the owner
gave: no dependency, no licence question, no audio-path work.

The first three commits I would write — files and functions, not areas:

**Commit 1 — the ring, alone.**
Files: `include/RetroMidiRing.h` (new), `tests/src/core/RetroMidiRingTest.cpp` (new),
`tests/CMakeLists.txt` (one line, next to `src/core/RecordRingBufferTest.cpp` at `:52`),
plus the tick publisher (`std::atomic<std::uint32_t>` on the engine, in the file that already owns
the per-period play position) and its one-relaxed-store-per-period site.
Functions: `class RetroMidiRing` with `explicit RetroMidiRing(std::size_t minCapacityEvents)`,
`bool push(const RetroMidiEvent&) noexcept`, `void beginSnapshot() noexcept`,
`bool writerIdle() const noexcept`, `std::size_t copyOut(RetroMidiEvent*, std::size_t) noexcept`,
`void endSnapshot() noexcept`, `std::uint64_t overwrittenCount() const noexcept`,
`std::size_t capacity() const noexcept`, and the private `nextPowerOfTwo()` /
`slotsFor(std::uint32_t head)` helpers modelled on `include/RecordRingBuffer.h:158-163`.
Tests: capacity rounds up to a power of two and is exact at a power of two (the slot
`Capacity_IsPowerOfTwoAndAtLeastRequested`, `tests/src/core/RecordRingBufferTest.cpp:58-68`); filling past capacity keeps the **last** N in order
and counts the rest; `copyOut` returns them in arrival order; a two-thread producer/consumer run with
no loss, no duplication and no reordering (the shape of
`tests/src/core/RecordRingBufferTest.cpp:195-233`); and `ProducerPath_DoesNotAllocate` modelled on
`tests/src/core/RecordRingBufferTest.cpp:238-251` using `tests/src/core/AllocationProbe.h`.

**Commit 2 — the seam, wired and inert while disarmed.**
Files: `include/RetroMidiCapture.h` + `src/core/RetroMidiCapture.cpp` (new: the arm flag, the ring,
the snapshot handshake), `include/MidiClient.h` (one member next to `m_midiPorts` at `:114`),
`src/core/midi/MidiClient.cpp` (one call at `:244` next to the existing `MidiLearn` call),
`src/core/midi/MidiAlsaSeq.cpp` (one call in the loop body above the `continue` at `:535-538`).
Functions: `void RetroMidiCapture::capture(const MidiEvent&, std::uint32_t tick) noexcept`,
`bool arm(bool)`, `bool isArmed() const`, plus a `stampFrom(const MidiEvent&)` mapper.
Behaviour change while disarmed: one relaxed atomic load per event, and a headless sweep of the
registry must produce byte-identical results to the pre-commit run.

**Commit 3 — the surface and the write-to-clip.**
Files: `src/core/ControlCommandsMidi.cpp` (new, registered the way the existing settings commands
are), `src/gui/MainWindow.cpp` (the menu actions, next to the MIDI Learn action at `:365-373`),
`tests/src/core/MidiRetroCaptureTest.cpp` (new: the MIDI-thread allocation probe of §6 and the
note-materialisation test).
Functions: `registerMidiRetroCaptureArm()`, `registerMidiRetroCaptureToClip()`,
`registerMidiRetroCaptureStatus()` (naming in §5), `bool writeWindowToClip(MidiClip*, const
RetroMidiEvent*, std::size_t, tick_t windowStartTick)` — the note-matcher of §3.7 — and the
`MainWindow` slot that invokes the command.

---

## 5. The surfaces

### The registry

`ControlRegistry` is the single command registry (`include/ControlRegistry.h:102`), one
`ControlCommand` record per user-facing action (`:81-100`: `id`, `group`, `verb`, `description`,
`requiresDecl`, `argsSchema`, `resultSchema`, `mutating`, `requiresEngine`, `handler`), registered
through `void registerCommand(const ControlCommand&)` (`:128`). **Handlers always run on the UI
thread** (`:102`), which is what makes the snapshot handshake of §3.5 the right shape.

### Rule A15 is gate-enforced, mechanised

Programme rule A15 — "An `agent_surface` ctest asserts, by reflection, that every registered
menu/toolbar action has a command ID and that every declared command is reachable headless; a new
action without a command fails the build." (`../ableton-gap/SPEC-zene-studio.md:132`) — is implemented
by `tests/agent-surface-gate.py` (header: "the `agent_surface` ctest gate (SPEC-zene-studio.md A15)").
Its REFLECTION half walks the live `MainWindow` over the control socket via `control.surface_report`
and requires every user-visible action to resolve to a registered command id, ratcheted by
`tests/agent-surface-baseline.txt`; its REVERSE half requires every declared command to be either
swept headlessly or listed in `tests/agent-surface-allowlist.txt` with a reason. **So both directions
of this feature's surface are load-bearing: a menu item with no command id fails the gate, and a
command that cannot be swept and has no `requires` excuse fails it too.**

### The commands this feature needs

The `midi` group already exists (`midi.device_list` `src/core/ControlCommandsSettings.cpp:351`,
`midi.learn_toggle` `:392`), and the existing verbs are `snake_case` after the dot.

| id | args | result | class |
| --- | --- | --- | --- |
| `midi.retro_capture_arm` | `{}` | `{armed, seconds, capacity_events, changed}` | Not mutating. Mode/engine state, not project state — the `midi.learn_toggle` precedent, which records no transaction (`src/core/ControlCommandsSettings.cpp:400-402`). |
| `midi.retro_capture_status` | `{}` | `{armed, client, events_buffered, capacity_events, overwritten, seconds_span}` | Read-only. Needed so an agent can tell whether a capture is armed without a GUI. |
| `midi.retro_capture_to_clip` | `{track (trk-<n>), length? (ticks)}` | `{clip, events, window_start, events_written, events_overwritten, unmatched_ons}` | `mutating = true`; one journal checkpoint over the clip, inverse via the existing `clip.delete` — the `note.add` shape (`src/core/ControlCommandsNotes.cpp` `registerNoteAdd`). |

`requiresDecl`: leave empty (headless-safe) for all three if the implementation can take a snapshot
with no GUI, which §3.5 allows; if the arm is genuinely GUI-state-only, declare `display` and say why
in the description. The precedents in the tree are `telemetry.consent` — the only command that declares
`display` (`src/core/ControlCommandsTelemetry.cpp:248`, `{"display", "human"}`) — and `transport.play`,
which declares `device` (`src/core/ControlCommandsTransport.cpp:87`). **`midi.learn_toggle`, which this
section used to cite as the `display` precedent, declares no `requiresDecl` at all**: a case-sensitive
`grep -n "Requires\|requires" src/core/ControlCommandsSettings.cpp` returns one hit, the description
string at `:260`; the only two declarers in the tree are the two above; and the comment block at
`:403-408` says the opposite of what the old sentence claimed — *"no display, device or human is
required — an offscreen instance arms the mode exactly like a visible one, which is why this command is
swept headlessly instead of being allowlisted"*. Its typed refusal is for a `--no-gui` instance, i.e.
handler behaviour, not a declared requirement. So `midi.learn_toggle` is the precedent for the
*headless-safe* option, not the `display` one.
**Which of the two applies is a decision for the implementation lane; the gate will force it either
way.**

### Menu and keyboard

- Menu: `Edit > Capture MIDI` performing `midi.retro_capture_to_clip`, and an `Arm` checkable item
  for `midi.retro_capture_arm`, placed next to the MIDI Learn action
  (`src/gui/MainWindow.cpp:364-374`). Both declare their command id with `setData(...)` and their
  slots invoke that command — the A11/A15 convention stated in the comment at
  `src/gui/MainWindow.cpp:370-372`.
- Keyboard: **do not take an unbounded key.** `Ctrl+Shift+R` is already `runScript`
  (`src/gui/MainWindow.cpp:327`) and `Ctrl+M` is `onExportProjectMidi` (`:336`); the house helper is
  `keySequence(...)` (`include/DeprecationHelper.h:118`, used throughout `MainWindow::finalize`).
  `Ctrl+Shift+M` is unused in `MainWindow.cpp`'s action list and would read naturally next to the
  existing `Ctrl+Shift+<letter>` bindings. **UNVERIFIED:** that no other file claims
  `Ctrl+Shift+M` — confirming it needs the built shortcut table (a GUI run), which this lane did not
  perform.

---

## 6. The test plan

**The programme's gold standard for realtime work, named:** *allocation counters on the MIDI thread* —
the rule is AGENTS.md rule 4 ("no allocation, no locking, no unbounded growth on audio-thread paths.
Tests that prove it (allocation counters) are the gold standard — copy that pattern"), and this
repo's own gate table states the standard and its enforcement together:
`docs/CONVENTIONS.md:27`, row 9, "Realtime safety: no allocation/locking on audio-thread paths … "
enforced by "allocation-counter tests (`tests/src/core/AllocationProbe.h`, used by
`RecordRingBufferTest` and the two-track capture/recording harnesses) … **partially enforced** — a rule
held by tests where they exist, not by a sweeping gate".

**The existing test that already does it, by path:** `tests/src/core/RecordRingBufferTest.cpp`,
slot `ProducerPath_DoesNotAllocate` at `:238` — it resets `lmms::test::tlAllocationCount`, sets
`lmms::test::tlCountAllocations = true`, runs 1000 `writeBlock` calls, and asserts the count is 0.
The probe itself is `tests/src/core/AllocationProbe.h`: a `thread_local` flag and counter (`:37-40`),
and replaceable global `operator new`/`new[]`/`delete` so that *every* C++ allocation in the test
binary is counted on the thread that made it (`:54-76`). Fourteen test sources include it (a
`grep -rln "AllocationProbe.h" tests/src` returns fifteen paths, one of which is `AllocationProbe.h`
itself, whose header comment names the file), among them
`tests/src/core/RecordingRealtimeTest.cpp`, `tests/src/core/TwoTrackRecordingHarness.cpp` and
`tests/src/tracks/SampleClipWindowTest.cpp`.

**The honest gap this feature must close.** No test in this tree applies the probe to a MIDI-thread
path: `tests/src/core/MidiLearnThreadTest.cpp` asserts the *thread-of-write* discipline — it delivers
through `deliverFromMidiInputThread(...)`, which uses a plain `std::thread` because "the real input
paths are not all Qt threads - MidiJack delivers from a JACK callback thread" (`:74-75`) — but it is
not among the fourteen files that include `AllocationProbe.h`. `MidiLearn` also exposes the seam a
test asserts on for this purpose: `Qt::HANDLE lastBindingThreadId()` is documented as "the seam a
test asserts on to prove the write is not on the MIDI input thread" (`include/MidiLearn.h:106-108`).
So the new test is: push through `RetroMidiCapture::capture(...)` from a `std::thread` with
`tlCountAllocations = true` and assert `tlAllocationCount == 0`; assert the *write* landed on that
thread; and assert that a snapshot taken from the consumer thread neither loses nor duplicates an
event under concurrent pushes.

**What an offline test can prove without a build.** (a) The ring's arithmetic: capacity rounding,
drop-oldest retention, arrival order, and the snapshot clamp — all pure state transitions, provable
from the source with the same slot-level assertions `RecordRingBufferTest.cpp:195-233` already makes.
(b) That the producer path is allocation-free *on the thread the probe runs on* — `AllocationProbe`
replaces the global allocator and counts per thread, so a run under it is exactly the property the
rule names; and it is a self-contained `QTEST_APPLESS_MAIN` binary
(`tests/src/core/RecordRingBufferTest.cpp:256`), so it needs no audio device, no GUI, no MIDI hardware
and no window — one test target to compile, not the product. (c) That the writer never touches the
reader's indices — a two-thread no-loss test.

**What it cannot prove without a build, stated plainly.** That the ALSA and raw clients really deliver
on the thread assumed here; that the published tick and the sequencer tick agree in a live session;
that no *other* code on the MIDI thread allocates — the probe proves the region under test, and §2
records that `InstrumentTrack::processInEvent` allocates today
(`src/tracks/InstrumentTrack.cpp:425, 443`); and that the host build has lock-free 64-bit atomics —
nothing in `RecordRingBuffer.h` asserts it and nothing here does either. All four are **UNVERIFIED**
in this document and are the implementation lane's first measurements.

---

## 7. What it must NOT do

**No audio path. Retrospective AUDIO capture is the sibling item and is not in scope.** The sentence
being obeyed, verbatim, from the workspace register (`../NEXT-RELEASE-HANDOFF.md:94-95`):

> "**Its expensive twin is item 15 (audio), which waits on `#611`'s ALSA capture path — do 14
> first.**"

and the item's own register sentence immediately above it (`../NEXT-RELEASE-HANDOFF.md:92-94`):

> "**Item 14 lifted**: retrospective MIDI capture. No dependency, no licence question, days–weeks. A
> bounded recent-event ring per port, written to a clip on demand, off by default, allocation-free on
> the MIDI thread; `include/RecordRingBuffer.h` is the precedent."

Consequences that follow, each checkable:

- **No new work on the audio thread** and no change to `RecordRingBuffer.h`, `SampleFrameRingBuffer.h`
  or `TrackRecorder` — they are the precedent to read, not the code to edit. The only audio-thread
  addition this design allows is one relaxed store per period for the tick publisher (§3.5).
- **No new dependency.** The vendored `src/3rdparty/ringbuffer` submodule stays unused by this
  feature (§1) — which also keeps the feature clear of the licence question the owner said does not
  arise.
- **No lock, no allocation, no syscall on any MIDI input thread** (§3.3), while accepting honestly
  that the thread is not allocation-free today (§2).
- **Off by default** — the arm flag defaults false, the persisted key defaults `"0"`, and nothing
  starts capturing because a client opened.
- **No unbounded growth** — fixed capacity, no container that grows, and an overwritten counter
  instead of a growing backlog.
- **No silent loss.** SysEx is not captured in the smallest version, and that is recorded in the event
  `flags` and reported by the command, rather than being dropped invisibly.
- **No new audio-adjacent alarm** — do not add a "retrospective audio" placeholder, buffer, or
  parameter; audio waits on the ALSA capture path.

---

## 8. Open questions for the implementation lane (all UNVERIFIED here)

1. Does this build have lock-free 64-bit atomics, and does anything need to assert it?
2. Do the published tick and `ev->time.tick` agree within the window in a live ALSA session, and is
   the per-period relaxed store measurably free on the audio thread?
3. Is `Ctrl+Shift+M` unclaimed across the whole shortcut table?
4. Does the headless sweep accept `midi.retro_capture_arm` as headless-safe, or must it declare
   `display` (the way `telemetry.consent` does, `src/core/ControlCommandsTelemetry.cpp:248`)?
5. Should the ring live on `MidiClient` (one per open client, per §2) or be replicated per `MidiPort`
   for the two owners — the second is closer to the owner's phrase and the first is what catches
   events no port subscribes to. §2 argues for the first on the evidence of
   `src/core/midi/MidiAlsaSeq.cpp:535-538` and `src/core/midi/MidiPort.cpp:133-134`.

---

## 9. Claims, and how each was checked

Every check below was run in `.../zene-next-midi-retro` on branch `next/midi-retro` at `3fd5a4f3c`.
`cat`/`grep`/`sed`/`find`/`ls` were used unpiped or with the exit code taken from the command itself
(`cmd > /tmp/x.log 2>&1; echo EXIT=$?`).

| # | Claim | How it was checked |
| --- | --- | --- |
| 1 | The branch is `next/midi-retro`, based on `3fd5a4f3c` | `git rev-parse HEAD~1` → `3fd5a4f3c8acb9dcb26d9d27a7fd7d482ad8bde2`; `git rev-parse --abbrev-ref HEAD`; `git status --porcelain` (clean) |
| 2 | `RecordRingBuffer`'s real API and lines | `cat -n include/RecordRingBuffer.h` (whole file, 180 lines) — every line number above is from that output |
| 3 | Its one consumer is `TrackRecorder` | `grep -rn "RecordRingBuffer" --include=*.h --include=*.cpp .` → `src/core/audio/TrackRecorder.cpp:41`, `include/TrackRecorder.h:41,110`, plus the test and the two manifest files |
| 4 | `sample_t` is `float` | `grep -n "sample_t\b" include/LmmsTypes.h` → `39:using sample_t = float;` |
| 5 | No *machine* lock-free assertion in the ring | `grep -n "static_assert\|is_lock_free" include/RecordRingBuffer.h` → no output, exit 1 (the file's prose at `:2` and `:41` does say "lock-free") |
| 6 | `MidiEvent` carries two raw pointers | `sed -n '204,225p' include/MidiEvent.h` → `const char* m_sysExData;`, `const void* m_sourcePort;` |
| 7 | **8** concrete `MidiClient` subclasses | `grep -rn "class .*public MidiClient" --include=*.h include \| grep -v 'class MidiClientRaw'` → 8 lines (output pasted in §2) |
| 8 | The ALSA receive loop, its thread and its lock | `grep -n "void MidiAlsaSeq::run\|snd_seq_event_input\|m_seqMutex.lock()\|dest == nullptr\|switch( ev->type )" src/core/midi/MidiAlsaSeq.cpp`; `sed -n '469,560p'` and `'528,545p'` and `'560,625p'` |
| 9 | The raw receive entry point and its event dispatch | `sed -n '236,252p' src/core/midi/MidiClient.cpp`; `grep -n "processParsedEvent\|parseData" src/core/midi/MidiClient.cpp` |
| 10 | `MidiLearn` sees different event sets per path | `grep -rn "MidiLearn::instance()" src include` → `src/core/midi/MidiClient.cpp:244` (unconditional) and `src/core/midi/MidiAlsaSeq.cpp:582` (inside `case SND_SEQ_EVENT_CONTROLLER`) |
| 11 | The MIDI-thread contract, quoted | `grep -n "MIDI input thread\|allocates nothing\|one atomic load" include/MidiLearn.h` → `:58-63`; `grep -n "m_enabled" include/MidiLearn.h` → `:126` |
| 12 | The MIDI thread is not allocation-free end-to-end | `sed -n '394,470p' src/tracks/InstrumentTrack.cpp` → `NotePlayHandleManager::acquire(...)` and `addPlayHandle(...)` in the `MidiNoteOn` case; no test measures it (`grep -rln "AllocationProbe.h" tests/src` → 15 files, none of them a MIDI test) |
| 13 | `getPlayPos()` is not thread-safe | `sed -n '250,270p' include/Song.h` → `const TimePos& getPlayPos(...) const`; `grep -n atomic include/Song.h` → no output |
| 14 | The lockless event ring depends on a submodule that is not checked out here | `grep -rn "ringbuffer/" …` → `include/LocklessRingBuffer.h:31`, `include/Lv2Proc.h:36`, `src/3rdparty/CMakeLists.txt:16,20`; `cat .gitmodules` → `src/3rdparty/ringbuffer`; `ls -la src/3rdparty/ringbuffer` → empty directory |
| 15 | The other two rings, and their policies | `cat -n include/LocklessRingBuffer.h`; `sed -n '1,90p' src/wasm/WasmSpscRingBuffer.h`; `sed -n '25,80p' include/SampleFrameRingBuffer.h` |
| 16 | `ScriptMidiEvent` is the existing event POD | `grep -n "ScriptMidiEvent\|LocklessRingBuffer" include/ScriptEngine.h`; `grep -n "pushMidiInEvent\|popMidiInEvent" src/core/ScriptEngine.cpp` |
| 17 | The command registry and the A15 gate | `cat -n include/ControlRegistry.h`; `sed -n '1,40p' tests/agent-surface-gate.py`; `grep -n "A15" ../ableton-gap/SPEC-zene-studio.md` → `:132`; `head -25 tests/agent-surface-baseline.txt` |
| 18 | The `midi.*` command namespace and the learn precedent end-to-end | `grep -rn 'cmd.id = QStringLiteral("' src/core/ControlCommands*.cpp` (full id list); `sed -n '330,440p' src/core/ControlCommandsSettings.cpp`; `sed -n '360,375p' src/gui/MainWindow.cpp` |
| 19 | The existing off-by-default mechanisms | `grep -n "OPTION(WANT_\|option(WANT_\|option(ZENE_" CMakeLists.txt` → `:120,121,140`; `grep -rn "ConfigManager::inst()->value(" src include` → the boolean-setting examples incl. `src/gui/MixerChannelView.cpp:328` |
| 20 | The note-write convention | `sed -n '50,140p' src/core/ControlCommandsNotes.cpp`; `grep -n "MidiClip::addNote" src/tracks/MidiClip.cpp` → `:180`; `sed -n '100,112p' include/Note.h` |
| 21 | The allocation probe and the existing allocation test | `cat -n tests/src/core/AllocationProbe.h`; `grep -n "ProducerPath_DoesNotAllocate" tests/src/core/RecordRingBufferTest.cpp` → `:238`; `sed -n '185,260p'`; `grep -n "AllocationProbe.h" docs/CONVENTIONS.md` → `:27` |
| 22 | The A15/AGENTS rule text | `cat -n ../NEXT-RELEASE-HANDOFF.md`; `../ableton-gap/SPEC-zene-studio.md:132`; AGENTS.md rule 4 and 6 (workspace root); `docs/CONVENTIONS.md:27` |
| 23 | Shortcut usage in `MainWindow` | `grep -n "setShortcut\|QKeySequence\|keySequence(" src/gui/MainWindow.cpp`; `grep -rn "QKeySequence keySequence" include` → `include/DeprecationHelper.h:118` |

**Claims deliberately left UNVERIFIED** (each would need a build, a GUI run or measurement):
whether 64-bit atomics are lock-free in this configuration; whether the sequencer tick and a
published tick agree; the cost of the added per-period atomic store; whether `Ctrl+Shift+M` is free
across the whole shortcut table; whether `midi.retro_capture_arm` passes the headless sweep or must
declare `requires`; the contents of `ringbuffer/ringbuffer.h` (submodule absent from this worktree);
and any assertion about the MIDI thread as a whole being allocation-free, which the evidence in §2
actively contradicts for the `InstrumentTrack` path.

---

## Corrections applied after the independent audit (2026-09-12)

The independent audit (`NEXT-WAVE1-DOC-AUDIT.md`, commit `c22fa23` in the workspace repo) read this
document at `598bba392` and reported seven false claims. Every one was re-derived with its own command
before the text was touched; the design, the decisions and the open questions are otherwise unchanged.

1. **Base confirmation (header, §9 row 1).** Was: "base `3fd5a4f3c` … confirmed in-worktree with `git log --oneline -1` → `3fd5a4f3c fix(gates): …`". Now: `git rev-parse HEAD~1` → `3fd5a4f3c8acb9dcb26d9d27a7fd7d482ad8bde2` — at the commit the document is read at, `git log --oneline -1` prints `598bba392 docs(midi-retro): design for retrospective MIDI capture (owner item 14)`, i.e. the document's own commit, so that command confirms the base only before the commit.
2. **§1, the `writeStrided` call site.** Was: `src/core/audio/TrackRecorder.cpp:154-158`. Now: `:159-160` — `grep -n "writeStrided\|m_ring" src/core/audio/TrackRecorder.cpp` → `159:\tconst auto pushed = m_ring->writeStrided(...)`, `160:\t\tDEFAULT_CHANNELS, …`; 154-158 is the comment and the channel load.
3. **§1, "the file asserts lock-freedom nowhere".** Was: the file asserts lock-freedom nowhere. Now: nothing in the file *machine*-asserts it, but the file *states* it in prose twice — `grep -ni "lock.free" include/RecordRingBuffer.h` → `2: … lock-free single-producer/single-consumer ring buffer` and `41://! Lock-free SPSC ring buffer of mono sample frames (prototype, task #556).`; the machine assertion is still absent (`grep -n "static_assert\|is_lock_free" include/RecordRingBuffer.h` → no output, exit 1). Same distinction now in §9 row 5.
4. **§1, the ring's padding.** Was: "~176 bytes per ring". Now: **192 bytes**, by the declared layout — 40-byte header (`m_capacity` 8 + `m_mask` 8 + `std::vector` header 24), three `alignas(64)` atomics at offsets 64/128/192, `sizeof` rounded up to the 64-byte class alignment: 256 total against 64 bytes of payload (24 + 56 + 56 + 56 = 192 of padding).
5. **§2, the runtime accessor.** Was: `engine->midiClient()` at `src/core/ControlCommandsSettings.cpp:368`. Now: `:373` — `grep -n midiClient src/core/ControlCommandsSettings.cpp` → `370: engine->midiClientName()`, `373: MidiClient* client = engine != nullptr ? engine->midiClient() : nullptr;`; line 368 is `QJsonObject result;`.
6. **§5, the `requiresDecl` precedent (and §8 question 4).** Was: declaring `display` "exactly as `midi.learn_toggle` does (`src/core/ControlCommandsSettings.cpp:403-408`, which refuses typed when started `--no-gui`)". Now: the `display` precedent is `telemetry.consent` and the `device` precedent is `transport.play`; `midi.learn_toggle` is named as the *headless-safe* precedent it actually is — `grep -n "Requires\|requires" src/core/ControlCommandsSettings.cpp` → one hit, the description string at `:260`; `grep -rn 'requiresDecl' src/core/*.cpp` → `ControlCommandsTelemetry.cpp:248 {"display","human"}` and `ControlCommandsTransport.cpp:87 {"device"}` only; `midi.learn_toggle`'s own comment at `:403-408` says "no display, device or human is required … which is why this command is swept headlessly instead of being allowlisted". The lane's two-way decision is unchanged.
7. **§6, the probe's includers.** Was: "Fifteen test sources include it" / "not among the fifteen files". Now: fourteen — `grep -rln "AllocationProbe.h" tests/src` → 15 paths, one of which is `tests/src/core/AllocationProbe.h` itself (its own line-2 comment names the file), so fourteen test sources include it.

**Audit claim that did not reproduce as stated.** The audit's row A6 prints the command `grep -n "lock-free" include/RecordRingBuffer.h` and two matching lines. Run case-sensitively that command prints only one line (`:2`), because `:41` begins "Lock-free"; `-i` is required for both, and the correction above and the audit's substance both hold. Every other §A finding reproduced exactly, including the line citations and the 15-vs-14 grep count.
## 10. Implementation note — commit 3 landed (slice 2, 2026-09-12)

Written by the implementation lane on `next/midi-retro-impl`, on top of slice 1 (`3abae78fb`: the
ring, the two receive seams, the off-by-default arm, the allocation-counter test). Files:
`src/core/ControlCommandsMidi.cpp` (the three commands and their helpers),
`src/core/RetroMidiClipWriter.cpp` + `include/RetroMidiClipWriter.h` (the note matcher, split out so
the core unit test links it without the GUI), `include/RetroMidiCaptureSettings.h` +
`src/core/RetroMidiCaptureSettings.cpp` (the persisted switch), `src/gui/MainWindow.cpp` (the two
menu actions), `include/ControlRegistry.h`/`src/core/ControlRegistry.cpp` (registration),
`src/core/ControlReversibilityTable.cpp` (the three contract rows the A16 test demands), and
`tests/src/core/RetroMidiCaptureCommandsTest.cpp` (14 slots by QtTest's own count: 12 test cases plus
`initTestCase`/`cleanupTestCase`).

**The `requiresDecl` decision §5 left open: all three commands declare NOTHING.** Arming, reading
the ring and writing the window reach a typed result with no display, no device and no human, which
`tests/agent-surface-gate.py` proved by sweeping all three
(`build/tests/agent-surface-report.json`: `midi.retro_capture_arm` → `ok`,
`midi.retro_capture_status` → `ok`, `midi.retro_capture_to_clip` → typed `not_found`, "the capture
window is empty"; the allowlist is unchanged and still holds its one entry). The §5 sentence that
offered `midi.learn_toggle` as the `display` precedent is **superseded**: the corrected precedents
are `telemetry.consent` (`display, human`) and `transport.play` (`device`), and `midi.learn_toggle`
declares nothing — which is exactly the option this feature took.

**The persisted key's home, corrected.** §3.6 put `midi/retrocapture` in the capture object's
constructor; slice 1 showed that constructor runs before `main()` (a file-static `MidiClient`), so
the read is a null dereference there. It now lives in `src/core/RetroMidiCaptureSettings.cpp`, is
written by the arm command only when the mode actually moves, and is applied by
`applyPersistedRetroCaptureArm()` from `MainWindow::finalize()` — the first point where qApp exists
AND a MIDI client is open.

**`writeWindowToClip`'s shape differs from §4 in one respect:** it returns a
`RetroMidiClipWrite` counters struct (notes / unmatchedOns / unmatchedOffs / skipped / consumed /
window bounds) rather than a `bool`, because §3.7 requires the caller to REPORT truncation
(`unmatched_ons`) instead of being told the capture was clean. The matcher itself is §3.7 as
specified: arrival-order FIFO per (channel, key), note-on velocity 0 treated as a release, lengths
clamped to one tick, unmatched note-ons closed at the window's end (the newest tick a NOTE event
carries), non-note events counted and written nowhere.

**Menu.** `Edit > Arm MIDI Capture` (checkable, `midi.retro_capture_arm`) and `Edit > Capture MIDI`
(`midi.retro_capture_to_clip`) sit beside `Edit > MIDI Learn`; both declare their command in
`QAction::data` and their slots invoke that same command (SPEC A11/A15). **No keyboard shortcut was
taken**: §5's open question 3 (`Ctrl+Shift+M` free across the whole shortcut table) is still
unverified, and taking an unverified key is worse than taking none.

**Still UNVERIFIED after this slice** (needs hardware or a live session, not a build): that the ALSA
and raw clients deliver on the thread §2 assumes; that the sequencer tick and the published tick
agree; that `Ctrl+Shift+M` is free; and the MIDI thread's end-to-end allocation profile, which §2
records is already non-zero for `InstrumentTrack`. The probe-backed tests cover the ring and the
capture object, not the whole input path.

