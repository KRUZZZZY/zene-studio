<!--
  In-repo copy of the specification this repository's code and tests cite (DOC-5, 2026-09-15).
  This is a PINNED SNAPSHOT, not a live document:
    source   : the program workspace, RECORDING-PROTOTYPE.md
    sha256   : 1c2bf51384aa69454dac3e0757e22cb4eb5240beb0a0b7534557f87b3a4f2ce4
    bytes    : 43569
    why this file: the two-track recording prototype; cited by tests/TwoTrackRecordingHarness.cpp and docs/CLIP-CAPTURE-DESIGN.md
  The workspace copy remains the working copy and is where the document is edited; refreshing
  this snapshot is a deliberate act, recorded in the commit that does it. If a citation in this
  repository and this file ever disagree, reconcile them in one commit - the citations name the
  file by bare name (e.g. `SPEC-zene-studio.md`), and this directory is where that name resolves.
  See docs/specs/README.md for the convention and for the cited documents NOT yet committed.
-->
# Two-Track Simultaneous Recording — Prototype Report (task #556)

**Mission:** `lmms-recording-mission` · **Board task:** #556
**Repo:** `lmms-fl-research/lmms-recording` (LMMS fork clone)
**Branch:** `feat/two-track-recording` (off upstream `4e677cb6c`, *not pushed*)
**Build:** `build/`, `-DWANT_QT6=ON`, ALSA backend, `-j4`

**One-line status:** the two-track capture machinery (per-track arm + input
selection, lock-free SPSC ring buffer, per-track disk writer) is implemented,
builds green (`REAL_EXIT=0`), passes 10/10 ctest, and records a **synthetic**
two-channel source to two WAV files with bit-exact digests and zero
producer-side allocations. A **real** 2-channel hardware capture
(`hw:1,0`, UGREEN camera) was also run through the same recorder path and
matched its reference sample-exactly. The synthetic harness is *not* a
hardware capture, and the LMMS ALSA backend still has no capture path — see
§6 and §8.

---

## 1. Data-flow note (written **before** any code change)

Committed as `recording/DATA-FLOW.md` in commit `e278907d8`. Verbatim:

> # Data-Flow Note — Two-Track Simultaneous Recording Prototype
>
> > Task: AI-KOS #556 (mission `lmms-recording-mission`) · Branch `feat/two-track-recording` off upstream `4e677cb6c`
> > Written **before** any code change, 2026-09-08, from a read-only survey of the clone.
> > Scope: which thread calls what, in the existing engine and in the prototype.
>
> ## 1. Threads in the target build (ALSA backend, Linux)
>
> | # | Thread | Created by | Calls |
> |---|---|---|---|
> | T1 | **Audio thread** | `AudioAlsa` (QThread; `AudioDevice::startProcessing()`) | `AudioAlsa::run()` → `AudioEngine::renderNextBuffer()` → `AudioEngine::renderNextPeriod()` (takes `m_changeMutex`) → stages 0-3 → `Song::processNextBuffer()` |
> | T2 | **AudioEngineWorkerThread pool** | `renderStageInstruments()` (`startAndWaitForJobs()`) | Runs PlayHandle jobs. `SampleRecordHandle::play()` executes here, **not** on T1. The "audio thread" is therefore T1 + T2 as one serialised unit per period. |
> | T3 | **Input source thread** | backend-dependent | **Absent in this build.** `AudioAlsa.cpp` contains no `snd_pcm_readi`; `AudioEngine::pushInputFrames()` is called only from `AudioJack.cpp:432` and `AudioSdl.cpp:187`. ALSA is output-only, so `inputBuffer()` is silent/zero under ALSA. |
> | T4 | **Disk writer thread(s)** | prototype (`std::thread`, one per armed track, SCHED_OTHER) | Pops frames from the ring buffer, calls libsndfile. All allocation/syscalls live here. |
> | T5 | **GUI/main thread** | Qt | Arm/disarm, input-channel selection, start/stop, polls overflow counters at 10 Hz. |
>
> ## 2. Existing capture primitive call chain (what the prototype replaces)
>
> 1. `Song::processNextBuffer()` (T1) creates a `SampleRecordHandle` when `SampleClip::m_recordModel` is set.
> 2. `SampleRecordHandle::play()` (T2) reads `Engine::audioEngine()->inputBuffer()` — the double-buffered input (`AudioEngine::swapBuffers()` flips read/write indices once per period) — and copies it with `new SampleFrame[_frames]` into a RAM list.
> 3. On stop, `~SampleRecordHandle()` → `createSampleBuffer()` → `SampleClip::setSampleBuffer()`.
>
> Limits confirmed in code: the copy **allocates on the audio path** (`SampleRecordHandle.cpp:writeBuffer`, `new SampleFrame[_frames]`); it records the **whole interleaved device buffer** with no channel selection; one handle per clip; `AudioSampleRecorder` is an entire `AudioDevice` subtype (full-device capture), not per-track.
>
> ## 3. Prototype data flow (implemented on this branch)
>
> **Producer — audio thread (T1/T2), realtime, no alloc / no lock / no syscall:**
> `MultiTrackRecorder::processInput(const SampleFrame* in, f_cnt_t n)`
> → for each hardcoded track `t` in {0,1}: `armed.load(acquire)` → demux `in[frame][t.inputChannel]` → `RecordRingBuffer::write()`.
> Ring full ⇒ **drop incoming frame + `m_overflow.fetch_add(1, relaxed)`** (never blocks, never allocates).
>
> **Consumer — disk writer thread (T4), one per armed track:**
> `RecordRingBuffer::read(scratch, batch)` → `sf_writef_float()` → 24-bit WAV via libsndfile.
>
> **Control — GUI thread (T5):**
> `arm()` allocates the ring + opens the file + spawns T4 (all off the audio thread); `disarm()` sets a stop flag, T4 drains and closes, then joins.
>
> **Arm/input state:** prototype uses `std::atomic<bool> m_armed` + `std::atomic<int> m_inputChannel` per track (2 hardcoded tracks in `MultiTrackRecorder`), standing in for per-`SampleTrack` arm/input-selection state (phase 2 wires `SampleClip::m_recordModel` to these atomics).
>
> ## 4. Invariants held (mixer/SPEC-dynamic-routing.md §5)
>
> 1. No allocation, no locks, no syscalls on the producer path — ring is pre-allocated at arm time.
> 2. T4 is SCHED_OTHER; the producer never waits on it.
> 3. Arm/disarm/channel changes are atomics set from T5.
> 4. Overflow degrades by dropping (never blocks). Implemented policy: **drop-newest** (drop the incoming frame); the spec's §3.3 wording "drops-oldest" would require either a second writer to the read index or per-block sequence numbers — a documented deviation, see RECORDING-PROTOTYPE.md.
>
> ## 5. What this note does NOT claim
>
> - No ALSA capture exists in LMMS at this commit; the prototype's producer entry point is fed by a synthetic source (harness) or a standalone ALSA capture thread (probe), not by `AudioAlsa::run()`.
> - `SampleRecordHandle` is not modified; it is the reference implementation the prototype replaces.

---

## 2. What was implemented, per file

All files are new except the five marked *(modified)*. Diffstat in §7.

| File | Lines | What it does |
|---|---:|---|
| `recording/DATA-FLOW.md` | 50 | The design note above, written first. |
| `include/RecordRingBuffer.h` | 180 | Lock-free SPSC ring buffer. Storage allocated once in the constructor (`m_data(m_capacity, 0.f)`); capacity rounded up to a power of two so the index wrap is a mask. Producer entry points `write()` / `writeBlock()` / `writeStrided()` use only relaxed loads/stores plus acquire/release on the two indices; full ⇒ drop-newest + `m_overflow.fetch_add`. `writeStrided()` demuxes one channel out of an interleaved `SampleFrame` block without a scratch buffer. |
| `include/TrackRecorder.h` | 130 | One capture stream: owns a `RecordRingBuffer`, an `std::atomic<bool> m_armed`, an `std::atomic<int> m_inputChannel`, an `std::atomic<uint64_t> m_framesPushed`, counters, an `SNDFILE*` handle and the writer `std::thread`. `processInput()` is the realtime entry point; `arm()` / `disarm()` do all allocation, file I/O and thread management off the audio thread. Includes `<sndfile.h>` and uses `SNDFILE*` (no forward-declared private struct). |
| `src/core/audio/TrackRecorder.cpp` | 184 | Writer loop: drains the ring in 8192-frame batches into a pre-allocated scratch vector and writes 24-bit mono WAV via `sf_writef_float()`; ~2 ms poll when idle; drains and closes on disarm. |
| `include/MultiTrackRecorder.h` | 86 | Owns the two hardcoded `TrackRecorder`s (track 0 → input channel 1, track 1 → input channel 0, cross-mapped so a demux bug cannot pass). Exposes `processInput()` for the engine and arm/disarm/status for the control thread. |
| `src/core/audio/MultiTrackRecorder.cpp` | 101 | Fans the engine input block out to both tracks; `processInput()` is `noexcept` and only calls the per-track entry points. |
| `include/AudioEngine.h` *(modified)* | +9 | Includes `MultiTrackRecorder.h`, adds `multiTrackRecorder()` accessor and the prototype members. |
| `src/core/AudioEngine.cpp` *(modified)* | +7 | Adds the stage-4 demux after `renderStageMix()`, commented as realtime-safe by contract. |
| `src/core/CMakeLists.txt` *(modified)* | +2 | Compiles `TrackRecorder.cpp` and `MultiTrackRecorder.cpp`. |
| `tests/src/core/AllocationProbe.h` | 78 | Test-only global `operator new/delete` overrides that count allocations per thread id, so a test can assert that the producer thread allocated zero times. |
| `tests/src/core/RecordRingBufferTest.cpp` | 258 | QtTest invariants: capacity, empty-on-construction, single-frame and block order/value preservation, full ⇒ drop-newest + overflow counter, wrap-around, strided demux, reset, concurrent producer/consumer with no loss/duplication, and `ProducerPath_DoesNotAllocate`. |
| `tests/src/core/TwoTrackRecordingHarness.cpp` | 315 | **Offline synthetic harness** (clearly labelled in its header and its output): drives `MultiTrackRecorder::processInput()` with a generated two-channel signal at realtime pace, then verifies the two WAV files sample-by-sample and by digest. |
| `tests/src/core/TwoTrackAlsaCaptureProbe.cpp` | 266 | **Real hardware probe**: opens an ALSA capture device with libasound, feeds real interleaved frames to the same `MultiTrackRecorder::processInput()` from a thread standing in for the engine's audio thread, and writes a reference stereo WAV from the same frame buffer for sample-exact comparison. |
| `tests/CMakeLists.txt` *(modified)* | +15 | Registers the three test sources and links the test binaries against the static sndfile. |
| `recording/verify_harness.py` | 119 | Independent Python cross-check of the synthetic harness WAVs (re-derives the signal, decodes the WAVs with the stdlib `wave` module, compares digests). |
| `recording/verify_alsa_probe.py` | 143 | Independent Python cross-check of the hardware probe output: asserts each track WAV equals its reference channel sample-by-sample. |

**Not modified:** `AudioSampleRecorder.{h,cpp}`, `SampleRecordHandle.{h,cpp}`,
`SampleClip.cpp`, `AudioDevice.h`, `AudioAlsa.cpp` — the prototype is additive
and leaves the existing recording primitives untouched as the reference
implementation.

---

## 3. Build (ALSA backend, Qt6)

```console
$ cd lmms-recording
$ cmake -B build -S . -DWANT_QT6=ON
-- Configuring done (46.4s)
-- Generating done (0.5s)
-- Build files have been written to: /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-recording/build

$ cmake --build build -j4 > /tmp/rec-final-build.log 2>&1; echo "REAL_EXIT=$?"
REAL_EXIT=0
$ tail -3 /tmp/rec-final-build.log
[ 99%] Built target carlarack
[ 99%] Built target vestige
[100%] Built target vsteffect
```

The build was run **without a pipe**, so `REAL_EXIT` is cmake's own exit code,
not a pipeline tail's. Backend flags in `build/CMakeCache.txt`:

```
WANT_ALSA:BOOL=ON
WANT_QT6:BOOL=ON
```

What this build actually enables (`build/lmmsconfig.h`):

```console
$ grep -nE "LMMS_HAVE_(ALSA|JACK|WEAKJACK|PULSEAUDIO|SDL)" build/lmmsconfig.h
19:#define LMMS_HAVE_ALSA
21:#define LMMS_HAVE_JACK
22:#define LMMS_HAVE_JACK_PRENAME
23:#define LMMS_HAVE_WEAKJACK
33:#define LMMS_HAVE_PULSEAUDIO
34:/* #undef LMMS_HAVE_SDL */
```

ALSA and PulseAudio are compiled in; SDL is compiled out; JACK is compiled in
through the bundled weakjack shim (§6.2).

**Honesty note on measurement.** Two earlier builds on this branch were red
and one of them was *mismeasured*: `cmake --build build -j4 2>&1 | tail -120;
echo "BUILD_EXIT=$?"` printed the exit status of `tail`, not of cmake, so a
failing build reported `BUILD_EXIT=0`. That was caught in review. Since then
every build in this report uses the pipe-free form above. The two red builds
were: (1) four libsndfile type errors in `TrackRecorder.cpp` (`cannot convert
'SNDFILE*' {aka 'sf_private_tag*'} to 'SNDFILE_tag*'`) — fixed by including
`<sndfile.h>` and declaring the handle as `SNDFILE*`; (2) five
`f_cnt_t`-not-found errors in the harness/probe — fixed by qualifying
`lmms::f_cnt_t`.

---

## 4. Proof without hardware — offline synthetic harness

> **This section is SYNTHETIC. The samples come from a generated signal, not
> from a microphone or line input. No hardware capture is involved here.**

`tests/src/core/TwoTrackRecordingHarness.cpp` drives the real capture path —
`MultiTrackRecorder::processInput()` → `RecordRingBuffer::writeStrided()` →
writer thread → libsndfile → WAV — with a generated two-channel source:
96 000 frames (2 s @ 48 kHz) in 512-frame blocks paced at realtime (~10.7 ms
per block), with the two channels carrying different signals and the tracks
cross-mapped (track 0 ← channel 1, track 1 ← channel 0). It asserts:

* **(a) frame counts** — file frame count, ring frames pushed and disk frames
  written all equal 96 000 exactly, with zero overflow and zero write errors;
* **(b) no loss/duplication** — every decoded sample is compared to the
  re-derived expected signal, and an FNV-1a digest over the decoded floats
  must equal the digest over the expected floats;
* **(c)** the producer thread must have performed **0** allocations, measured
  by the test-only `AllocationProbe`.

### 4.1 Harness output (verbatim)

```console
$ rm -rf /tmp/lmms-recording-harness && ./build/tests/TwoTrackRecordingHarness
=============================================================
  SYNTHETIC TWO-TRACK RECORDING HARNESS (task #556)
  *** SYNTHETIC SOURCE - NOT A HARDWARE CAPTURE ***
  frames=96000 (2 s @ 48000 Hz), block=512 frames
  output dir: /tmp/lmms-recording-harness
=============================================================
[OK] arm track 0 -> input channel 1
[OK] arm track 1 -> input channel 0
[OK] both tracks report armed

producer thread allocations during capture loop: 0 (expected 0)
[OK] audio-thread path did not allocate (dynamic probe)

--- track 0: /tmp/lmms-recording-harness/track0_ch1.wav (records input channel 1) ---
  counters : pushed=96000 recorded=96000 overflow=0 writeErrors=0
  file     : frames=96000 channels=1 rate=48000 format=0x10003
[OK] WAV header is mono 24-bit PCM at 48000 Hz
[OK] file frame count == expected frame count
[OK] frames accepted by the ring == expected
[OK] frames written to disk == expected
[OK] ring buffer never overflowed
[OK] no sndfile write errors
  decoded digest  : 0xc1fb0990bb990b25
  expected digest : 0xc1fb0990bb990b25
  max |error|     : 0 (half 24-bit LSB = 5.96e-08)
[OK] decoded sample digest == expected sample digest
[OK] every sample matches the synthetic source (no shift, loss or duplication)

--- track 1: /tmp/lmms-recording-harness/track1_ch0.wav (records input channel 0) ---
  counters : pushed=96000 recorded=96000 overflow=0 writeErrors=0
  file     : frames=96000 channels=1 rate=48000 format=0x10003
[OK] WAV header is mono 24-bit PCM at 48000 Hz
[OK] file frame count == expected frame count
[OK] frames accepted by the ring == expected
[OK] frames written to disk == expected
[OK] ring buffer never overflowed
[OK] no sndfile write errors
  decoded digest  : 0x26ce0e21c1b165a5
  expected digest : 0x26ce0e21c1b165a5
  max |error|     : 0 (half 24-bit LSB = 5.96e-08)
[OK] decoded sample digest == expected sample digest
[OK] every sample matches the synthetic source (no shift, loss or duplication)
[OK] the two tracks carry different signals (channel demux really happened)

=============================================================
  RESULT: PASS - synthetic two-channel source recorded to two
  WAV files with no loss, duplication, reordering or allocation.
  (synthetic source; no hardware capture involved)
=============================================================
```

Exit code: `HARNESS_EXIT=0`.

### 4.2 Independent Python cross-check (verbatim)

A separate process re-derives the expected signal and decodes the two WAVs
with the Python standard library, so the check does not share code with the
C++ harness:

```console
$ python3 recording/verify_harness.py /tmp/lmms-recording-harness
=== INDEPENDENT PYTHON CROSS-CHECK OF THE SYNTHETIC HARNESS OUTPUT ===

--- track 0: /tmp/lmms-recording-harness/track0_ch1.wav (expected input channel 1) ---
[OK] header: channels == 1 (got 1)
[OK] header: 24-bit samples (got 24-bit)
[OK] header: sample rate == 48000 (got 48000)
[OK] header: frames == 96000 (got 96000)
[OK] every sample equals the synthetic source (mismatches=0, max|err|=0)
  python digest          : 0xc1fb0990bb990b25
  python expected digest : 0xc1fb0990bb990b25
  harness digest         : 0xc1fb0990bb990b25
[OK] python digest matches the re-derived expected signal
[OK] python digest matches the harness's digest
[OK] python expected digest matches the harness's expected digest
[OK] harness reported frames recorded == expected

--- track 1: /tmp/lmms-recording-harness/track1_ch0.wav (expected input channel 0) ---
[OK] header: channels == 1 (got 1)
[OK] header: 24-bit samples (got 24-bit)
[OK] header: sample rate == 48000 (got 48000)
[OK] header: frames == 96000 (got 96000)
[OK] every sample equals the synthetic source (mismatches=0, max|err|=0)
  python digest          : 0x26ce0e21c1b165a5
  python expected digest : 0x26ce0e21c1b165a5
  harness digest         : 0x26ce0e21c1b165a5
[OK] python digest matches the re-derived expected signal
[OK] python digest matches the harness's digest
[OK] python expected digest matches the harness's expected digest
[OK] harness reported frames recorded == expected
[OK] harness reported 0 producer-thread allocations (got 0)

=== PYTHON CROSS-CHECK RESULT: PASS ===
```

Exit code: `PY_HARNESS_EXIT=0`.

### 4.3 No allocation / no locking on the producer side

Three independent lines of evidence.

**(i) Source-level audit.** The producer path is `TrackRecorder::processInput`
(`src/core/audio/TrackRecorder.cpp:130`):

```cpp
void TrackRecorder::processInput(const SampleFrame* input, f_cnt_t frames) noexcept
{
	if (input == nullptr || frames == 0 || !m_armed.load(std::memory_order_acquire))
	{
		return;
	}

	// Realtime-safe: only a ring-buffer store and one relaxed atomic add.
	// No allocation, no locks, no syscalls (asserted by the offline harness
	// and the RecordRingBufferTest allocation probe).
	const auto channel = m_inputChannel.load(std::memory_order_relaxed);
	const auto pushed = m_ring->writeStrided(input->data() + channel,
		DEFAULT_CHANNELS, static_cast<std::size_t>(frames));
	m_framesPushed.fetch_add(pushed, std::memory_order_relaxed);
}
```

and the ring buffer's only allocation is in its constructor
(`include/RecordRingBuffer.h:65`, `m_data(m_capacity, 0.f)`); the write side
(`write()` at :74, `writeBlock()` at :91, `writeStrided()` at :99) contains
no `new`, no `malloc`, no mutex — only `std::atomic` loads/stores with
relaxed/acquire/release ordering.

**(ii) Machine-level audit (`objdump`).** Instruction census of the compiled
function:

```console
$ objdump -dC --disassemble='lmms::TrackRecorder::processInput(lmms::SampleFrame const*, unsigned long)' \
    build/src/CMakeFiles/lmmsobjs.dir/core/audio/TrackRecorder.cpp.o
0000000000000690 <lmms::TrackRecorder::processInput(lmms::SampleFrame const*, unsigned long)>:
 690:	f3 0f 1e fa          	endbr64
 694:	48 85 f6             	test   %rsi,%rsi
 697:	0f 84 93 00 00 00    	je     730 <lmms::TrackRecorder::processInput(lmms::SampleFrame const*, unsigned long)+0xa0>
 69d:	48 85 d2             	test   %rdx,%rdx
 6a0:	0f 84 8a 00 00 00    	je     730 <lmms::TrackRecorder::processInput(lmms::SampleFrame const*, unsigned long)+0xa0>
 6a6:	0f b6 47 30          	movzbl 0x30(%rdi),%eax
 6aa:	84 c0                	test   %al,%al
 6ac:	0f 84 7e 00 00 00    	je     730 <lmms::TrackRecorder::processInput(lmms::SampleFrame const*, unsigned long)+0xa0>
 6b2:	55                   	push   %rbp
 6b3:	48 89 f1             	mov    %rsi,%rcx
 6b6:	53                   	push   %rbx
 6b7:	48 63 47 34          	movslq 0x34(%rdi),%rax
 6bb:	48 8b 1f             	mov    (%rdi),%rbx
 6be:	48 8d 34 85 00 00 00 	lea    0x0(,%rax,4),%rsi
 6c5:	00 
 6c6:	48 8b 43 40          	mov    0x40(%rbx),%rax
 6ca:	4c 8b 83 80 00 00 00 	mov    0x80(%rbx),%r8
 6d1:	4c 03 03             	add    (%rbx),%r8
 6d4:	49 29 c0             	sub    %rax,%r8
 6d7:	49 39 d0             	cmp    %rdx,%r8
 6da:	4c 0f 47 c2          	cmova  %rdx,%r8
 6de:	4d 85 c0             	test   %r8,%r8
 6e1:	74 55                	je     738 <lmms::TrackRecorder::processInput(lmms::SampleFrame const*, unsigned long)+0xa8>
 6e3:	48 89 c5             	mov    %rax,%rbp
 6e6:	4c 8b 5b 08          	mov    0x8(%rbx),%r11
 6ea:	4c 8b 53 10          	mov    0x10(%rbx),%r10
 6ee:	4d 8d 0c 00          	lea    (%r8,%rax,1),%r9
 6f2:	48 f7 dd             	neg    %rbp
 6f5:	48 8d 34 ee          	lea    (%rsi,%rbp,8),%rsi
 6f9:	48 01 f1             	add    %rsi,%rcx
 6fc:	0f 1f 40 00          	nopl   0x0(%rax)
 700:	f3 0f 10 04 c1       	movss  (%rcx,%rax,8),%xmm0
 705:	4c 89 de             	mov    %r11,%rsi
 708:	48 21 c6             	and    %rax,%rsi
 70b:	48 83 c0 01          	add    $0x1,%rax
 70f:	f3 41 0f 11 04 b2    	movss  %xmm0,(%r10,%rsi,4)
 715:	49 39 c1             	cmp    %rax,%r9
 718:	75 e6                	jne    700 <lmms::TrackRecorder::processInput(lmms::SampleFrame const*, unsigned long)+0x70>
 71a:	4c 89 4b 40          	mov    %r9,0x40(%rbx)
 71e:	49 39 d0             	cmp    %rdx,%r8
 721:	72 19                	jb     73c <lmms::TrackRecorder::processInput(lmms::SampleFrame const*, unsigned long)+0xac>
 723:	f0 4c 01 47 38       	lock add %r8,0x38(%rdi)
 728:	5b                   	pop    %rbx
 729:	5d                   	pop    %rbp
 72a:	c3                   	ret
 72b:	0f 1f 44 00 00       	nopl   0x0(%rax,%rax,1)
 730:	c3                   	ret
 731:	0f 1f 80 00 00 00 00 	nopl   0x0(%rax)
 738:	48 89 43 40          	mov    %rax,0x40(%rbx)
 73c:	4c 29 c2             	sub    %r8,%rdx
 73f:	f0 48 01 93 c0 00 00 	lock add %rdx,0xc0(%rbx)
 746:	00 
 747:	eb da                	jmp    723 <lmms::TrackRecorder::processInput(lmms::SampleFrame const*, unsigned long)+0x93>
```

Instruction census (the same disassembly piped through a counter):

```console
$ objdump -dC --disassemble='lmms::TrackRecorder::processInput(lmms::SampleFrame const*, unsigned long)' \
    build/src/CMakeFiles/lmmsobjs.dir/core/audio/TrackRecorder.cpp.o | grep -oE '(call|ret|lock|movss)' | sort | uniq -c
      2 lock
      2 movss
      2 ret
```

The census contains **no `call` line at all** (a zero count is omitted by `uniq -c`). Line by line: `700`–`718` is the copy loop (`movss` load at `700`, `movss` store at `70f`, `jne` at `718`) — one sample copied per iteration, no calls; `723` is `lock add %r8,0x38(%rdi)`, the pushed-frame statistic (x86 atomic RMW — cannot block); `73f` is `lock add %rdx,0xc0(%rbx)`, the ring's dropped-frame counter on the overflow path. The two `ret`s are the early-exit path (`730`) and the normal return (`72a`).

**Zero `call` instructions** in the whole function. The only `lock`-prefixed
instructions are `lock add` on the statistics counter and the ring's overflow
counter — x86 atomic RMW, which cannot block. The only `call` in
`MultiTrackRecorder::processInput` is the call to `TrackRecorder::processInput`
itself:

```console
$ objdump -drC --disassemble='lmms::MultiTrackRecorder::processInput(lmms::SampleFrame const*, unsigned long)' \
    build/src/CMakeFiles/lmmsobjs.dir/core/audio/MultiTrackRecorder.cpp.o
0000000000000090 <lmms::MultiTrackRecorder::processInput(lmms::SampleFrame const*, unsigned long)>:
  90:	f3 0f 1e fa          	endbr64
  94:	41 54                	push   %r12
  96:	49 89 d4             	mov    %rdx,%r12
  99:	55                   	push   %rbp
  9a:	48 89 f5             	mov    %rsi,%rbp
  9d:	53                   	push   %rbx
  9e:	48 89 fb             	mov    %rdi,%rbx
  a1:	e8 00 00 00 00       	call   a6 <lmms::MultiTrackRecorder::processInput(lmms::SampleFrame const*, unsigned long)+0x16>
			a2: R_X86_64_PLT32	lmms::TrackRecorder::processInput(lmms::SampleFrame const*, unsigned long)-0x4
  a6:	48 8d 7b 70          	lea    0x70(%rbx),%rdi
  aa:	4c 89 e2             	mov    %r12,%rdx
  ad:	5b                   	pop    %rbx
  ae:	48 89 ee             	mov    %rbp,%rsi
  b1:	5d                   	pop    %rbp
  b2:	41 5c                	pop    %r12
  b4:	e9 00 00 00 00       	jmp    b9 <lmms::MultiTrackRecorder::processInput(lmms::SampleFrame const*, unsigned long)+0x29>
			b5: R_X86_64_PLT32	lmms::TrackRecorder::processInput(lmms::SampleFrame const*, unsigned long)-0x4
```

The only `operator new` / `sf_*` symbols in `TrackRecorder.cpp.o` are
undefined references belonging to `arm()` / `disarm()` / the writer thread —
never reached from `processInput()`.

**(iii) Dynamic audit.** `AllocationProbe.h` overrides global
`operator new`/`delete` and counts per thread; the harness and the probe both
print `producer thread allocations during capture loop: 0` (see §4.1 and
§6.3), and
`RecordRingBufferTest::ProducerPath_DoesNotAllocate()` asserts the same in
ctest.

### 4.4 Why the synthetic signal is exactly representable

The first version of the harness used a full-scale ±0.75 signal and reported a
one-LSB mismatch on exactly the value −0.75 (3000 samples). A direct
libsndfile probe showed the cause is libsndfile's own float→PCM_24
conversion, which is asymmetric at that one value:

```console
$ /tmp/sfprobe
float in       int24      x*2^23     float out      out-in (LSB)
0.750000000    6291455    6291456    0.749999881    -1
-0.750000000   -6291455   -6291456   -0.749999881   1
0.500000000    4194304    4194304    0.500000000    0
-0.500000000   -4194304   -4194304   -0.500000000   0
0.703125000    5898240    5898240    0.703125000    0
-0.703125000   -5898240   -5898240   -0.703125000   0
0.046875000    393216     393216     0.046875000    0
-0.046875000   -393216    -393216    -0.046875000   0
1.000000000    8388607    8388608    0.999999881    -1
-1.000000000   -8388607   -8388608   -0.999999881   1
0.250000000    2097152    2097152    0.250000000    0
-0.250000000   -2097152   -2097152   -0.250000000   0
```

This is a library quantisation artefact, not a lost or altered sample, but it
would make a bit-exact digest assertion impossible. The synthetic signal was
therefore restricted to exact dyadic values with |x| ≤ 45/64 = 0.703125
(multiples of 1/64), which round-trip bit-exactly; the assertion is then
unambiguous and any real defect (shift, drop, duplication, reorder) still
fails it. The hardware probe sidesteps the issue entirely by comparing the
track WAVs against a reference WAV written through the same library from the
same frame buffer (§6.3).

---

## 5. Tests (QtTest via ctest)

`RecordRingBufferTest` is a QtTest binary; `TwoTrackRecordingHarness` is a
plain test binary. Both are registered in `tests/CMakeLists.txt` and run by
ctest from `build/tests`:

```console
$ cd build/tests && ctest --output-on-failure
Test project /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/lmms-recording/build/tests
      Start  1: ArrayVectorTest
 1/10 Test  #1: ArrayVectorTest ..................   Passed    0.02 sec
      Start  2: AudioBufferTest
 2/10 Test  #2: AudioBufferTest ..................   Passed    0.02 sec
      Start  3: AutomatableModelTest
 3/10 Test  #3: AutomatableModelTest .............   Passed    1.25 sec
      Start  4: MathTest
 4/10 Test  #4: MathTest .........................   Passed    0.02 sec
      Start  5: ProjectVersionTest
 5/10 Test  #5: ProjectVersionTest ...............   Passed    0.02 sec
      Start  6: RecordRingBufferTest
 6/10 Test  #6: RecordRingBufferTest .............   Passed    0.03 sec
      Start  7: RelativePathsTest
 7/10 Test  #7: RelativePathsTest ................   Passed    0.02 sec
      Start  8: TimelineTest
 8/10 Test  #8: TimelineTest .....................   Passed    1.24 sec
      Start  9: TwoTrackRecordingHarness
 9/10 Test  #9: TwoTrackRecordingHarness .........   Passed    2.03 sec
      Start 10: AutomationTrackTest
10/10 Test #10: AutomationTrackTest ..............   Passed    1.25 sec

100% tests passed, 0 tests failed out of 10

Total Test time (real) =   5.91 sec
```

Exit code: `CTEST_EXIT=0`. The QtTest detail:

```console
$ ./build/tests/RecordRingBufferTest
********* Start testing of RecordRingBufferTest *********
Config: Using QtTest library 6.4.2, Qt 6.4.2 (x86_64-little_endian-lp64 shared (dynamic) release build; by GCC 13.2.0), ubuntu 24.04
PASS   : RecordRingBufferTest::initTestCase()
PASS   : RecordRingBufferTest::Capacity_IsPowerOfTwoAndAtLeastRequested()
PASS   : RecordRingBufferTest::Empty_OnConstruction()
PASS   : RecordRingBufferTest::WriteRead_SingleFrame()
PASS   : RecordRingBufferTest::WriteRead_BlockPreservesOrderAndValues()
PASS   : RecordRingBufferTest::Full_DropsNewestAndCountsOverflow()
PASS   : RecordRingBufferTest::WrapAround_PreservesOrder()
PASS   : RecordRingBufferTest::WriteStrided_DemuxesOneChannel()
PASS   : RecordRingBufferTest::Reset_DiscardsFramesAndClearsOverflow()
PASS   : RecordRingBufferTest::ConcurrentProducerConsumer_NoLossNoDuplication()
PASS   : RecordRingBufferTest::ProducerPath_DoesNotAllocate()
PASS   : RecordRingBufferTest::cleanupTestCase()
Totals: 12 passed, 0 failed, 0 skipped, 0 blacklisted, 6ms
********* Finished testing of RecordRingBufferTest *********
```

### 5.1 Accounting for the "pre-existing 9"

The brief said 9 pre-existing tests must still pass. This build has **8**
pre-existing ctest entries, not 9, and all 8 pass. Evidence: at the base
commit, `tests/CMakeLists.txt` lists exactly eight `*Test.cpp` files —

```console
$ git show 4e677cb6c:tests/CMakeLists.txt | grep -oE "[A-Za-z]+Test\.cpp"
ArrayVectorTest.cpp
AudioBufferTest.cpp
AutomatableModelTest.cpp
MathTest.cpp
ProjectVersionTest.cpp
RelativePathsTest.cpp
TimelineTest.cpp
AutomationTrackTest.cpp
```

— and the ninth test in the tree, `smftest`
(`plugins/MidiImport/portsmf/test/`), is never added to the build:

```console
$ cd build && cmake --build . --target smftest
gmake: *** No rule to make target 'smftest'.  Stop.
```

So: **8/8 pre-existing pass, 10/10 including the two new ones.**

---

## 6. Hardware verdict

### 6.1 The box *does* have capture devices

```console
$ arecord -l
**** List of CAPTURE Hardware Devices ****
card 1: Camera [UGREEN Camera], device 0: USB Audio [USB Audio]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 2: Mini [Razer Seiren Mini], device 0: USB Audio [USB Audio]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 3: PCH [HDA Intel PCH], device 0: ALC623 Analog [ALC623 Analog]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 3: PCH [HDA Intel PCH], device 2: ALC623 Alt Analog [ALC623 Alt Analog]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
```

`hw:1,0` really is a 2-channel capture device:

```console
$ arecord -D hw:1,0 --dump-hw-params -f S16_LE -d 1 /dev/null
HW Params of device "hw:1,0":
--------------------
ACCESS:  MMAP_INTERLEAVED RW_INTERLEAVED
FORMAT:  S16_LE
SUBFORMAT:  STD MSBITS_MAX
SAMPLE_BITS: 16
FRAME_BITS: 32
CHANNELS: 2
RATE: [8000 48000]
PERIOD_TIME: [1000 1000000]
```

### 6.2 But LMMS's ALSA backend cannot capture

`src/core/audio/AudioAlsa.cpp` has no capture path, and the compiled object
confirms it at symbol level — `snd_pcm_writei` is there, `snd_pcm_readi` is not:

```console
$ grep -n "snd_pcm_readi" src/core/audio/AudioAlsa.cpp
(no output; exit 1)

$ nm -u build/src/CMakeFiles/lmmsobjs.dir/core/audio/AudioAlsa.cpp.o | grep -oE "snd_pcm_[a-z_]+" | sort -u
snd_pcm_close
snd_pcm_hw_params
snd_pcm_hw_params_any
snd_pcm_hw_params_free
snd_pcm_hw_params_get_buffer_size
snd_pcm_hw_params_get_period_size
snd_pcm_hw_params_malloc
snd_pcm_hw_params_set_access
snd_pcm_hw_params_set_buffer_size_near
snd_pcm_hw_params_set_channels
snd_pcm_hw_params_set_format
snd_pcm_hw_params_set_period_size_near
snd_pcm_hw_params_set_rate
snd_pcm_open
snd_pcm_poll_descriptors
snd_pcm_poll_descriptors_count
snd_pcm_prepare
snd_pcm_resume
snd_pcm_sw_params
snd_pcm_sw_params_current
snd_pcm_sw_params_free
snd_pcm_sw_params_malloc
snd_pcm_sw_params_set_avail_min
snd_pcm_sw_params_set_start_threshold
snd_pcm_writei
```

`AudioJack.cpp.o` references zero raw `jack_` symbols — but that is *not*
because JACK is compiled out. LMMS links JACK through its bundled **weakjack**
shim, so the object references 17 `WJACK_*` symbols instead:

```console
$ nm -u build/src/CMakeFiles/lmmsobjs.dir/core/audio/AudioJack.cpp.o | grep -c "U jack_"
0

$ nm -u build/src/CMakeFiles/lmmsobjs.dir/core/audio/AudioJack.cpp.o | grep -c "U WJACK_"
17

$ pgrep -a jackd
(no output; exit 1)
```

The JACK backend is therefore compiled and *does* have an input path
(`src/core/audio/AudioJack.cpp:423-432` copies each input port's frames into
`m_inputFrameBuffer` and calls `pushInputFrames`), but it needs a running JACK
server and none is running on this box. The ALSA backend — the one this build
uses at runtime — has no capture path at all. **Therefore no end-to-end
"LMMS records two hardware inputs" test is possible at this commit.** The
hardware test below drives the prototype's real audio-thread entry point from a
standalone libasound capture thread instead, which is stated explicitly in the
probe's own banner.

### 6.3 Real hardware capture through the prototype path

```console
$ rm -rf /tmp/lmms-recording-alsa && ./build/tests/TwoTrackAlsaCaptureProbe hw:1,0 3 /tmp/lmms-recording-alsa
=============================================================
  REAL HARDWARE TWO-TRACK CAPTURE PROBE (task #556)
  device=hw:1,0 seconds=3 frames=144000
  NOTE: libasound capture, driving the prototype's audio-thread
  entry point directly (LMMS ALSA backend has no capture path).
=============================================================
granted: rate=48000 channels=2 period=512 buffer=4096 format=S16_LE
[OK] device really has 2 capture channels
[OK] device runs at 48000 Hz
[OK] arm track 0 -> input channel 1
[OK] arm track 1 -> input channel 0

captured frames      : 144000 (expected 144000)
xruns                : 0
capture-thread allocs: 0 (expected 0)
[OK] captured exactly the requested number of frames
[OK] capture path did not allocate (dynamic probe)
track 0 (channel 1): pushed=144000 recorded=144000 overflow=0 writeErrors=0 file=/tmp/lmms-recording-alsa/track0_ch1.wav
[OK] track 0 pushed all frames
[OK] track 0 recorded all frames
[OK] track 0 ring never overflowed
[OK] track 0 had no write errors
track 1 (channel 0): pushed=144000 recorded=144000 overflow=0 writeErrors=0 file=/tmp/lmms-recording-alsa/track1_ch0.wav
[OK] track 1 pushed all frames
[OK] track 1 recorded all frames
[OK] track 1 ring never overflowed
[OK] track 1 had no write errors
[OK] reference stereo WAV written

reference: /tmp/lmms-recording-alsa/reference_stereo.wav (stereo ground truth of the same frames)
tracks   : /tmp/lmms-recording-alsa/track0_ch1.wav, /tmp/lmms-recording-alsa/track1_ch0.wav

=============================================================
  RESULT: PASS - real hardware capture of 144000 frames
  (device hw:1,0, 2 channels, xruns=0)
  Sample-exact comparison against reference_stereo.wav:
  run recording/verify_alsa_probe.py
=============================================================
```

Exit code: `PROBE_EXIT=0`. The sample-exact cross-check against the reference
stereo WAV (same frames written directly by libsndfile):

```console
$ python3 recording/verify_alsa_probe.py /tmp/lmms-recording-alsa
=== SAMPLE-EXACT CROSS-CHECK OF THE REAL HARDWARE CAPTURE ===
capture dir: /tmp/lmms-recording-alsa

reference : reference_stereo.wav channels=2 bits=24 rate=48000 frames=144000
[OK] reference is stereo (got 2 channels)
[OK] reference is 24-bit (got 24-bit)
  reference ch0 rms = 0.007990
  reference ch1 rms = 0.008266

--- track0_ch1.wav (must equal reference right channel 1) ---
  channels=1 bits=24 rate=48000 frames=144000 rms=0.008266
[OK] track file is mono (got 1)
[OK] track file is 24-bit (got 24-bit)
[OK] sample rate matches reference (48000 vs 48000)
[OK] frame count matches reference (144000 vs 144000)
[OK] decoded sample count matches reference
[OK] every sample equals the reference right channel (mismatches=0, max|err|=0)
  track digest     : 0xbf5ca1620a86b400
  reference digest : 0xbf5ca1620a86b400
[OK] digest matches the reference channel

--- track1_ch0.wav (must equal reference left channel 0) ---
  channels=1 bits=24 rate=48000 frames=144000 rms=0.007990
[OK] track file is mono (got 1)
[OK] track file is 24-bit (got 24-bit)
[OK] sample rate matches reference (48000 vs 48000)
[OK] frame count matches reference (144000 vs 144000)
[OK] decoded sample count matches reference
[OK] every sample equals the reference left channel (mismatches=0, max|err|=0)
  track digest     : 0xa9cbb50d2a7931db
  reference digest : 0xa9cbb50d2a7931db
[OK] digest matches the reference channel

=== RESULT: PASS ===
```

Exit code: `PY_ALSA_EXIT=0`. The two channels carry different real signals
(RMS 0.007990 vs 0.008266), and the per-track files are byte-for-byte
identical in decoded samples to the channel they were assigned — the demux is
real, not a copy of the same buffer. A separate earlier simultaneous
two-device capture (camera `hw:1,0` + Seiren) also succeeded, so two distinct
physical inputs exist on this box.

**Backlog item:** add a capture path to the ALSA backend
(`snd_pcm_readi` → `AudioEngine::pushInputFrames()`) — or run under a JACK
server — so the two-track recorder can be exercised end-to-end inside LMMS
with hardware input. Until then, hardware capture into the engine is
impossible.

### 6.4 Two physical inputs, captured simultaneously (raw ALSA)

The probe above uses one 2-channel device. To prove that *two distinct
physical inputs* exist and can run at the same time, two independent `arecord`
processes were started concurrently (`/tmp/two-input-capture.sh`):

```console
$ bash /tmp/two-input-capture.sh; echo "TWO_DEVICE_EXIT=$?"
camera(pid 182225) exit=0
seiren(pid 182226) exit=0
elapsed=3.255125145 s
=== arec-cam.log ===
Recording WAVE 'hw-camera-2ch.wav' : Signed 16 bit Little Endian, Rate 48000 Hz, Stereo
=== arec-seiren.log ===
Recording WAVE 'hw-seiren-1ch.wav' : Signed 16 bit Little Endian, Rate 48000 Hz, Mono
-rw-r--r-- 1 kruzzzzy kruzzzzy 576044 Sep  8 22:34 hw-camera-2ch.wav
-rw-r--r-- 1 kruzzzzy kruzzzzy 288044 Sep  8 22:34 hw-seiren-1ch.wav
TWO_DEVICE_EXIT=0
```

(Process IDs, elapsed time and file timestamps differ between runs; the block
above is one real run, and the WAV analysis below was taken from that same
run's files.)

Both files decode to exactly 144000 frames (3.000 s) of real audio:

```console
$ python3 -c "import wave, struct, math
for f in ['hw-camera-2ch.wav','hw-seiren-1ch.wav']:
    w = wave.open(f); n=w.getnframes(); c=w.getnchannels(); sw=w.getsampwidth(); fr=w.getframerate()
    data = w.readframes(n); w.close()
    s = struct.unpack('<%dh' % (len(data)//2), data)
    rms = math.sqrt(sum(x*x for x in s)/len(s))/32768.0
    print(f'{f}: channels={c} sampwidth={sw} rate={fr} frames={n} duration={n/fr:.3f}s rms={rms:.6f}')"
hw-camera-2ch.wav: channels=2 sampwidth=2 rate=48000 frames=144000 duration=3.000s rms=0.014297
hw-seiren-1ch.wav: channels=1 sampwidth=2 rate=48000 frames=144000 duration=3.000s rms=0.002227
```

So the hardware *can* do two-input capture. What it cannot do is drive
**LMMS's own engine** with it (§6.2) — that is the backlog item.

---

## 7. Commits and diffstat

Local commits on `feat/two-track-recording`, **not pushed**:

```
dcf39d8fc6b1dc372bab657677b491262d4c46e3  Add tests and offline harness for two-track recording
3f12dbf58533d751045e652eb887aea6e0c96e4b  Add two-track simultaneous recording prototype
1dd9840d0c1da96693dc012f673aa68c0ba1cbab  Add lock-free SPSC ring buffer for audio-thread recording
e278907d8503751490a02a81ed452f6207081c70  Add two-track recording data-flow note
4e677cb6c  (upstream base) Don't compress man page in during build (#8494)
```

`git log --oneline -5` on the branch:

```console
$ git log --oneline -5
dcf39d8fc Add tests and offline harness for two-track recording
3f12dbf58 Add two-track simultaneous recording prototype
1dd9840d0 Add lock-free SPSC ring buffer for audio-thread recording
e278907d8 Add two-track recording data-flow note
4e677cb6c Don't compress man page in during build (#8494)
```

Working tree clean after the commits (`git status --short` empty).
Diffstat vs the upstream base:

```
include/AudioEngine.h                       |   9 +
 include/MultiTrackRecorder.h                |  86 ++++++++
 include/RecordRingBuffer.h                  | 180 ++++++++++++++++
 include/TrackRecorder.h                     | 130 ++++++++++++
 recording/DATA-FLOW.md                      |  50 +++++
 recording/verify_alsa_probe.py              | 143 +++++++++++++
 recording/verify_harness.py                 | 119 +++++++++++
 src/core/AudioEngine.cpp                    |   7 +
 src/core/CMakeLists.txt                     |   2 +
 src/core/audio/MultiTrackRecorder.cpp       | 101 +++++++++
 src/core/audio/TrackRecorder.cpp            | 184 ++++++++++++++++
 tests/CMakeLists.txt                        |  15 ++
 tests/src/core/AllocationProbe.h            |  78 +++++++
 tests/src/core/RecordRingBufferTest.cpp     | 258 +++++++++++++++++++++++
 tests/src/core/TwoTrackAlsaCaptureProbe.cpp | 266 +++++++++++++++++++++++
 tests/src/core/TwoTrackRecordingHarness.cpp | 315 ++++++++++++++++++++++++++++
 16 files changed, 1943 insertions(+)
```

---

## 8. Not verified (explicit)

1. **No end-to-end capture inside LMMS.** Nothing was recorded through
   `AudioAlsa::run()` — the ALSA backend has no capture path (§6.2). The
   hardware probe drives `MultiTrackRecorder::processInput()` from a
   standalone libasound thread, which is *not* the same as the engine's own
   audio thread. The engine-side stage-4 demux is wired but was never
   exercised by a live input device.
2. **No GUI/`SampleClip` integration.** Arm and input-selection are atomics
   on two hardcoded tracks; `SampleClip::m_recordModel` and
   `SampleRecordHandle` are untouched. No UI, no undo/redo, no project
   serialization of arm state.
3. **No simultaneous *two-device* capture through the prototype.** The probe
   records one 2-channel device; a camera+Seiren simultaneous capture was
   verified with `arecord` alone (§6.4), not through `MultiTrackRecorder`.
4. **No long-duration or under-load testing.** The harness runs 2 s, the
   probe 3 s, both with zero xruns/overflow. Behaviour under sustained load,
   disk stalls, or writer starvation (ring full ⇒ drop-newest) is untested
   beyond the unit test that forces an overflow.
5. **No realtime-scheduler validation.** No `SCHED_FIFO`/priority testing and
   no measurement of worst-case producer latency.
6. **No other backends.** JACK/PulseAudio paths were not exercised at runtime
   (no `jackd` running; ALSA is the configured backend), and SDL is compiled
   out of this build (`/* #undef LMMS_HAVE_SDL */` in `build/lmmsconfig.h`).
7. **`smftest` was not run.** It is not part of this build (§5.1), so it is
   neither passing nor failing here.
8. **Bit-exactness is asserted only for dyadic sample values** (|x| ≤ 45/64)
   in the synthetic harness, because of the libsndfile ±1 LSB quirk at ±0.75
   (§4.4). The hardware path is bit-exact against its own reference WAV, not
   against an independent decoder.
9. **The 24-bit WAV files were not auditioned.** Only numerical comparison;
   no listening test.

## 9. Known deviations from the spec

- **Overflow policy is drop-newest** (the incoming frame is dropped) rather
  than the spec's "drops-oldest" wording; rationale in the data-flow note §4.
- **Track count is hardcoded to two**, as the brief specifies for the
  prototype.
- **The writer polls at ~2 ms** instead of waiting on a condition variable,
  to keep the producer path lock-free and the writer simple; this costs a
  little idle CPU and is a prototype choice.
