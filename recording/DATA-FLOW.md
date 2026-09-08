# Data-Flow Note — Two-Track Simultaneous Recording Prototype

> Task: AI-KOS #556 (mission `lmms-recording-mission`) · Branch `feat/two-track-recording` off upstream `4e677cb6c`
> Written **before** any code change, 2026-09-08, from a read-only survey of the clone.
> Scope: which thread calls what, in the existing engine and in the prototype.

## 1. Threads in the target build (ALSA backend, Linux)

| # | Thread | Created by | Calls |
|---|---|---|---|
| T1 | **Audio thread** | `AudioAlsa` (QThread; `AudioDevice::startProcessing()`) | `AudioAlsa::run()` → `AudioEngine::renderNextBuffer()` → `AudioEngine::renderNextPeriod()` (takes `m_changeMutex`) → stages 0-3 → `Song::processNextBuffer()` |
| T2 | **AudioEngineWorkerThread pool** | `renderStageInstruments()` (`startAndWaitForJobs()`) | Runs PlayHandle jobs. `SampleRecordHandle::play()` executes here, **not** on T1. The "audio thread" is therefore T1 + T2 as one serialised unit per period. |
| T3 | **Input source thread** | backend-dependent | **Absent in this build.** `AudioAlsa.cpp` contains no `snd_pcm_readi`; `AudioEngine::pushInputFrames()` is called only from `AudioJack.cpp:432` and `AudioSdl.cpp:187`. ALSA is output-only, so `inputBuffer()` is silent/zero under ALSA. |
| T4 | **Disk writer thread(s)** | prototype (`std::thread`, one per armed track, SCHED_OTHER) | Pops frames from the ring buffer, calls libsndfile. All allocation/syscalls live here. |
| T5 | **GUI/main thread** | Qt | Arm/disarm, input-channel selection, start/stop, polls overflow counters at 10 Hz. |

## 2. Existing capture primitive call chain (what the prototype replaces)

1. `Song::processNextBuffer()` (T1) creates a `SampleRecordHandle` when `SampleClip::m_recordModel` is set.
2. `SampleRecordHandle::play()` (T2) reads `Engine::audioEngine()->inputBuffer()` — the double-buffered input (`AudioEngine::swapBuffers()` flips read/write indices once per period) — and copies it with `new SampleFrame[_frames]` into a RAM list.
3. On stop, `~SampleRecordHandle()` → `createSampleBuffer()` → `SampleClip::setSampleBuffer()`.

Limits confirmed in code: the copy **allocates on the audio path** (`SampleRecordHandle.cpp:writeBuffer`, `new SampleFrame[_frames]`); it records the **whole interleaved device buffer** with no channel selection; one handle per clip; `AudioSampleRecorder` is an entire `AudioDevice` subtype (full-device capture), not per-track.

## 3. Prototype data flow (implemented on this branch)

**Producer — audio thread (T1/T2), realtime, no alloc / no lock / no syscall:**
`MultiTrackRecorder::processInput(const SampleFrame* in, f_cnt_t n)`
→ for each hardcoded track `t` in {0,1}: `armed.load(acquire)` → demux `in[frame][t.inputChannel]` → `RecordRingBuffer::write()`.
Ring full ⇒ **drop incoming frame + `m_overflow.fetch_add(1, relaxed)`** (never blocks, never allocates).

**Consumer — disk writer thread (T4), one per armed track:**
`RecordRingBuffer::read(scratch, batch)` → `sf_writef_float()` → 24-bit WAV via libsndfile.

**Control — GUI thread (T5):**
`arm()` allocates the ring + opens the file + spawns T4 (all off the audio thread); `disarm()` sets a stop flag, T4 drains and closes, then joins.

**Arm/input state:** prototype uses `std::atomic<bool> m_armed` + `std::atomic<int> m_inputChannel` per track (2 hardcoded tracks in `MultiTrackRecorder`), standing in for per-`SampleTrack` arm/input-selection state (phase 2 wires `SampleClip::m_recordModel` to these atomics).

## 4. Invariants held (mixer/SPEC-dynamic-routing.md §5)

1. No allocation, no locks, no syscalls on the producer path — ring is pre-allocated at arm time.
2. T4 is SCHED_OTHER; the producer never waits on it.
3. Arm/disarm/channel changes are atomics set from T5.
4. Overflow degrades by dropping (never blocks). Implemented policy: **drop-newest** (drop the incoming frame); the spec's §3.3 wording "drops-oldest" would require either a second writer to the read index or per-block sequence numbers — a documented deviation, see RECORDING-PROTOTYPE.md.

## 5. What this note does NOT claim

- No ALSA capture exists in LMMS at this commit; the prototype's producer entry point is fed by a synthetic source (harness) or a standalone ALSA capture thread (probe), not by `AudioAlsa::run()`.
- `SampleRecordHandle` is not modified; it is the reference implementation the prototype replaces.
