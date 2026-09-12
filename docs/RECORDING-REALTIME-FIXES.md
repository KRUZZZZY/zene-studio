# Recording / realtime-path fixes (branch `post-alpha/recording-realtime`)

Subjects: the four defects the independent audit `feedback/grade-B-recording.md`
ranked as the most reachable, fixed in the order it ranked them by reachability.

* Worktree: `projects/lmms-fl-research/zene-pa-recfix`
* Branch: `post-alpha/recording-realtime`, based on `post-alpha/v0.2`
  (`0c23587d2`)
* Fix commit: `0b3345b7c` — *fix: clip out-of-range floats, and take the capture
  path off the lock and off growth*. This report is a follow-up commit on the
  same branch.
* The audit's verdict was "51 of 51 substantive claims TRUE at both the audit SHA
  and the current tip, and the entire recording path is byte-identical across all
  33 `post-alpha/*` branches", so every defect below was live on every lane.

Build and test were run exactly as the brief specified, from the worktree root:

```
JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4
```

with CI's flags (`-DUSE_WERROR=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
-DTARGET_UARCH=official -DUSE_COMPILE_CACHE=ON -DWANT_DEBUG_CPACK=ON`) plus
`-DWANT_QT6=ON`, which the script prints itself as a DEVIATION (this box has no
Qt5 development files). Results: `configure EXIT=0`, `build EXIT=0` (0 compiler
errors under `-Werror`), `ctest` **27/27 passed, 0 failed**, `local-ci: overall
exit=0`. Every exit code below was measured unpiped.

---

## 1. DEFECT 6 — out-of-range floats wrap instead of clipping

### 1a. Where the wrap actually was (the brief's premise needs one correction)

The brief describes this as "audio corruption on export ... out-of-range floats
currently WRAP" on "our WAV export path". Measured, that is not where the wrap
lives, and the distinction matters:

| write path | clipping today | out-of-range behaviour |
|---|---|---|
| WAV export — `src/core/audio/AudioFileWave.cpp:89` | `sf_command(m_sf, SFC_SET_CLIPPING, nullptr, SF_TRUE)` after `sf_open_fd` | **clips** (`+1.5` → `2147483392`) |
| FLAC export — `src/core/audio/AudioFileFlac.cpp:83` | same command after `sf_open` | **clips** |
| 16-bit export — `AudioDevice::convertToS16` (`src/core/audio/AudioDevice.cpp:104,119`) | `AudioEngine::clip()` applied in C++ | **clips** |
| **recorder — `src/core/audio/TrackRecorder.cpp:162`** | **nothing** | **WRAPS**: `+1.5` → `-1073742336`, `+2.0` → `-512` |

So the export writers were already safe (the audit's §4.10 says the same and
calls the recorder the deviation from the codebase's own convention); the
wrap was on the **recorder's** 24-bit WAV write. The recorder is not reachable
until a track can be armed (D1), so this defect was latent, not always-on
damage — but it is the one that would corrupt every take the moment the arm
path lands, and it is fixed here. See §5 for the reachability statement.

`RecordClipTest` pins both halves: `defaultWritePathWrapsOutOfRangeFloats`
measures the wrap, `clippedAndPreFixPathsDifferOnlyOutOfRange` measures the
export sequence (`SFC_SET_CLIPPING`) clipping the same signal.

### 1b. The fix, and why not `SFC_SET_CLIPPING`

The audit endorsed Report B's proposed one-liner
(`sf_command(m_sf, SFC_SET_CLIPPING, nullptr, SF_TRUE)` after `sf_open`). It
works, but it is **not** behaviour-preserving for in-range audio, which the
brief requires. Measured on libsndfile 1.2.2, with the same in-range signal:

```
in-range recorder, fix as shipped (C++ clamp): -2147483392, -1610612480, -1073741824, -536870912, 0, 536870912, 1073741824, 1610612480, 2147483392
in-range control, no clamping at all          : -2147483392, -1610612480, -1073741824, -536870912, 0, 536870912, 1073741824, 1610612480, 2147483392
```

With `SFC_SET_CLIPPING` instead, in-range **negative** samples move by one
24-bit LSB on the same measurement: `-0.75` comes back `-1610612736` with the
flag and `-1610612480` without it, and `-1.0` gives `-2147483648` versus
`-2147483392` (the `-0.5` sample happened to land on the same value either way,
so the shift is not uniform — which is exactly why it was measured rather than
assumed). One LSB of a 24-bit sample is −144 dBFS: inaudible, but it is a
rewrite of audio that was never out of range, and the brief's rule is "a render
that stays within range must be unchanged".

The fix therefore clamps in C++ on the disk-writer thread
(`TrackRecorder::writerLoop`), which is:

* **bit-exact for every in-range sample**, both signs, up to and including
  exactly ±1.0 (proved by `inRangeTakeIsUnchanged`);
* **off the audio thread** — it runs on the writer thread, after the ring read,
  so it adds nothing to `processInput()`;
* **the mechanism this tree already uses** on its other integer write path
  (`AudioDevice::convertToS16` applies `AudioEngine::clip()` in C++), so the
  recorder does not introduce a third convention;
* **independent of a library command whose semantics are awkward to pin**:
  `SFC_GET_CLIPPING` on a fresh WAV/PCM file does not answer the question (it
  leaves the caller's buffer untouched), which is why the test probes the
  pipeline behaviourally instead.

Both mechanisms agree to within one 24-bit LSB on every sample, so a recorder
take does not "sound different" from an export of the same signal — asserted in
`clippedAndPreFixPathsDifferOnlyOutOfRange`.

### 1c. Proof

`tests/src/core/RecordClipTest.cpp` — **6 passed, 0 failed** (`CLIP_EXIT=0`).
The production path is driven end to end (arm → `processInput` with a hot
block → `disarm` → read back with `sf_readf_int`), not a reimplementation:

```
QINFO  : hotTakeIsClippedNotWrapped() recorder take: 2147483392, 2147483392, -2147483392, 1073741824
PASS   : hotTakeIsClippedNotWrapped()
QINFO  : inRangeTakeIsUnchanged() in-range recorder: -2147483392, -1610612480, -1073741824, -536870912, 0, 536870912, 1073741824, 1610612480, 2147483392
QINFO  : inRangeTakeIsUnchanged() in-range control : -2147483392, -1610612480, -1073741824, -536870912, 0, 536870912, 1073741824, 1610612480, 2147483392
PASS   : inRangeTakeIsUnchanged()
QINFO  : clippedAndPreFixPathsDifferOnlyOutOfRange() pre-fix sequence        : 512, 1073742336, -2147483392, -1073741824, 0, 1073741824, 2147483392, -1073742336, -512
QINFO  : clippedAndPreFixPathsDifferOnlyOutOfRange() recorder fix (C++ clamp): -2147483392, -2147483392, -2147483392, -1073741824, 0, 1073741824, 2147483392, 2147483392, 2147483392
QINFO  : clippedAndPreFixPathsDifferOnlyOutOfRange() export sequence (SFC flag): -2147483648, -2147483648, -2147483648, -1073741824, 0, 1073741824, 2147483392, 2147483392, 2147483392
PASS   : clippedAndPreFixPathsDifferOnlyOutOfRange()
```

**Inverted control, run by actually removing the fix** (the clamp loop deleted
from `TrackRecorder::writerLoop`, everything else unchanged, rebuilt and re-run):

```
QINFO  : hotTakeIsClippedNotWrapped() recorder take: -1073742336, -512, 1073742336, 1073741824
FAIL!  : hotTakeIsClippedNotWrapped() 'got.samples[0] > 0' returned FALSE.
          (+1.5 came back -1073742336: sign flipped, i.e. WRAPPED, not clipped)
CLIP_EXIT=1
```

i.e. with the fix removed the production recorder reproduces the audit's
empirical numbers exactly (`+1.5` → `−1073742336`, `+2.0` → `−512`), and
`inRangeTakeIsUnchanged` keeps passing, confirming the failing slot isolates the
out-of-range behaviour. The fixes were restored with `git checkout --` and the
suite re-run green afterwards.

---

## 2. D9b — a lock on the realtime capture thread

### The line

`src/core/AudioEngine.cpp:173-198` (before the fix): `pushInputFrames()` began
with `requestChangeInModel();` (line 175) and ended with `doneChangeInModel();`
(line 197). `requestChangeInModel()` takes `m_changeMutex` unless the caller is
the rendering thread, and this function runs on the JACK process callback
(`AudioJack.cpp:432`) or the SDL capture callback (`AudioSdl.cpp:187`). Any GUI
thread holding the model lock could therefore block an audio callback — the
audit's rank-1 defect, reachable on every JACK/SDL run.

### The fix

`pushInputFrames()` now performs exactly one bounded store and no locking:

```cpp
void AudioEngine::pushInputFrames( const SampleFrame* _ab, const f_cnt_t _frames ) noexcept
{
	if( _ab == nullptr || _frames == 0 || m_inputStage == nullptr ) { return; }
	m_inputStage->writeBlock( _ab, static_cast<std::size_t>( _frames ) );
}
```

The frames go into `m_inputStage`, a fixed-capacity pre-allocated SPSC ring
(`include/SampleFrameRingBuffer.h`, the stereo sibling of the tree's existing
`RecordRingBuffer.h`). `swapBuffers()` — which already ran once per rendered
period, under `m_changeMutex`, before any play handle reads `inputBuffer()`
(STAGE 1) and before the recorder demuxes it (STAGE 4) — drains the ring into
`m_inputBuffer[m_inputBufferRead]`:

```cpp
void AudioEngine::drainInputStage() noexcept
{
	const auto staged = m_inputStage->available();
	const auto frames = std::min( staged,
		static_cast<std::size_t>( m_inputBufferSize[ m_inputBufferRead ] ) );
	m_inputStage->read( m_inputBuffer[ m_inputBufferRead ], frames );
	m_inputBufferFrames[ m_inputBufferRead ] = static_cast<f_cnt_t>( frames );
}
```

Both threads only ever touch their own index, so the capture thread never
touches `m_inputBuffer` and needs no lock. `m_inputBuffer[2]` is no longer
touched by the capture thread at all, which is also what makes §4 possible.

### Proof — the audio thread no longer blocks

`RecordingRealtimeTest::pushInputFramesDoesNotBlockOnTheModelLock` builds a real
`Engine`, has another thread take and hold the model lock
(`requestChangesGuard()`) for a bounded 600 ms, and measures 64
`pushInputFrames()` calls:

```
PASS   : RecordingRealtimeTest::pushInputFramesDoesNotBlockOnTheModelLock()
```

**Inverted control, run by putting the lock back** (`requestChangeInModel()` /
`doneChangeInModel()` restored around the ring write, rebuilt and re-run):

```
FAIL!  : RecordingRealtimeTest::pushInputFramesDoesNotBlockOnTheModelLock()
          'elapsedMs < holdMs / 2' returned FALSE.
          (pushInputFrames took 600 ms while the model lock was held for 600 ms:
           the capture thread is still blocking on m_changeMutex)
RT_EXIT=1
```

600 ms of blocked audio callback, measured, on the same test that reports
sub-millisecond with the fix in place. That is the defect and its repair in one
pair of runs.

Reachability: **reachable today, on every JACK or SDL run** — this is the one
fix in this set that changes behaviour a user can already hit.

---

## 3. D9c — the clip record path allocated on the audio thread

### The line

`src/core/SampleRecordHandle.cpp:129-140` (before the fix):
`auto buf = new SampleFrame[_frames];` — one heap allocation **per rendered
period** (~86/s at 48 kHz/256-frame periods), appended to a `QList` that grew
for the whole take, with the whole take then assembled into a `SampleBuffer` by
`createSampleBuffer()` **on the audio thread** from the destructor
(`AudioEngine.cpp:227`/`:290` deletes the play handle there).

### The fix

`writeBuffer()` is now one bounded store into a pre-allocated ring:

```cpp
void SampleRecordHandle::writeBuffer( const SampleFrame* _ab, const f_cnt_t _frames )
{
	if (m_accum != nullptr) { m_accum->append( _ab, _frames ); }
}
```

`include/SampleRecordAccumulator.h` (new) stages the frames in a
fixed-capacity, pre-allocated `SampleFrameRingBuffer` and a drain thread owns
the take — the growing `std::vector<SampleFrame>` and the `SampleBuffer`
construction both happen on that thread, never on the audio thread. The
destructor only signals finish, joins, and installs the finished buffer
(`m_clip->setSampleBuffer(...)`); it does not assemble anything.

Two deliberate limits, stated rather than hidden:

* the accumulator and its thread are created in `SampleRecordHandle`'s
  constructor, which `SampleTrack::play()` calls on the audio thread, so the
  *arm* event still allocates (as it did before — `new SampleRecordHandle` is
  itself an audio-thread allocation). What is gone is the **per-period**
  allocation, the unbounded `QList` growth, and the take-sized build at stop.
* the destructor's `join()` is a bounded wait, not an allocation. Moving the
  finalisation to a non-RT owner entirely would need a clip-lifetime decision
  (a detached finaliser would call `SampleClip::setSampleBuffer` after the GUI
  may have freed the clip) and is out of scope here; the audio thread now
  performs **zero** allocations on this path, which is what is proved below.

### Proof

`RecordingRealtimeTest::accumulatorAppendPerformsNoAllocation` arms the tree's
`AllocationProbe.h` (`tests/src/core/AllocationProbe.h`) on the calling thread
and appends 32 blocks of 256 frames:

```
PASS   : RecordingRealtimeTest::accumulatorAppendPerformsNoAllocation()
```

`tlAllocationCount == 0` — measured zero allocations on the append path. The
ring and the drain thread are constructed before the probe is armed.

`accumulatorTakeIsCompleteAndOrdered` then shows the move did not change what is
recorded: the take is complete, in order, carries the recording sample rate, and
dropped zero frames — i.e. the same take the old code produced, assembled
elsewhere.

Reachability: **reachable today** through the hidden clip-arm path the audit
located (`SampleClipView.cpp:169-175`, Ctrl+Shift+Left-click).

---

## 4. D9a — the input buffer grew without bound during a JACK export

### The line

`src/core/AudioEngine.cpp:181-192` (before the fix):

```cpp
	if( frames + _frames > size )
	{
		size = std::max(size * 2, frames + _frames);
		auto ab = new SampleFrame[size];      // doubling, on the JACK process thread
		...
	}
```

While a JACK device is stopped (precisely what an export does —
`ProjectRenderer.cpp:142` calls `setAudioDevice(m_fileDev, false)` and
`AudioJack::stopProcessingImpl()` is empty), `renderNextPeriod()` is skipped so
`swapBuffers()` never runs, nothing resets the write side, and every
`pushInputFrames()` call keeps doubling the buffer. Unbounded `SampleFrame`
growth for the duration of the export, on the JACK process thread.

### The fix

The doubling is gone. The staging ring is allocated once in the constructor and
never resized; a full ring rejects the newest frames and counts them:

* `AudioEngine::inputFramesStaged()` — frames currently held, bounded by
  `AudioEngine::InputStageCapacityFrames` (16384);
* `AudioEngine::inputFramesDropped()` — frames rejected because the ring was
  full (was `m_overflow` inside the ring).

### Proof

`RecordingRealtimeTest::inputStagingIsBoundedWhileTheDeviceIsStopped` stops the
device and pushes 1,024,000 frames (1000 × 1024) through `pushInputFrames()`:

```
QINFO  : inputStagingIsBoundedWhileTheDeviceIsStopped() pushed 1024000 frames: staged 16384 -> 16384, dropped 1024000
PASS   : RecordingRealtimeTest::inputStagingIsBoundedWhileTheDeviceIsStopped()
```

1,024,000 frames offered, 16384 held (exactly the fixed capacity), 1,024,000
rejected, and the conservation check `stagedAfter - stagedBefore + dropped ==
total` holds — no frame vanished silently and none was stored. The pre-fix code
would have held all 1,024,000 frames and kept doubling.

Memory: the ring is 16384 × 8 bytes = 128 KiB, allocated once, versus unbounded
growth before. The per-period destination buffers were reduced from
`DEFAULT_BUFFER_SIZE * 100` (25600 frames) to the same 16384 frames, since the
drain is now bounded by the ring — so the fix is also a small net memory
reduction on the capture path.

Reachability: **reachable today** under JACK during an export.

---

## 5. What I did NOT fix, and why

Ordered by the audit's own reachability ranking, so nothing here is a silent gap.

**Not reachable until a track can be armed** (the audit's C1: no application
code calls `armTrack`/`disarmAll`/`recorder()`, nothing under `src/gui` mentions
`MultiTrackRecorder`). These are real defects and were deliberately left alone
rather than "fixed" blind, because none of them can be exercised by a test on
this tree and a blind fix would be unverifiable:

* **D2** — a crash/kill mid-take leaves a WAV whose header reads empty (needs
  `SFC_SET_UPDATE_HEADER_AUTO`/`SFC_UPDATE_HEADER_NOW`, absent from the tree).
* **D3** — write failures counted but never surfaced; a disk-full take is
  silently truncated and closes as a valid file.
* **D4** — `arm()` publishes `m_armed` (line 75) before `m_ring->reset()` (80),
  the channel set (84), the file open (91) and the writer start (99), so a live
  producer can overlap `reset()` and `m_framesPushed.store(0)` races
  `fetch_add`; only re-arming from a non-audio thread can produce wrong data.
* **D5** — `disarm()` can lose the last input block (≈5.3 ms at 48 kHz, up to
  85 ms after a D9a backlog). The audit's minimal fix (run `disarm()` under
  `requestChangesGuard()`) is correct but `disarm()` also blocks for the drain
  plus an `fsync`, so it must not be called from the audio thread — which is
  exactly where the engine deletes play handles, so this needs a control-path
  decision first.
* **D7** — ring overflow drops are silent and the 65536-frame stall tolerance
  shrinks with sample rate (1.49 s @44.1 k → 0.34 s @192 k).
* **D8** — no device-loss/rate-change handling while armed; nothing in
  `setAudioDevice`, `restoreAudioDevice` or the JACK restart path touches
  `m_recorder`.
* **D6 for hot JACK input into the recorder** is fixed in the writer (§1); what
  is *not* verified is the end-to-end path, because it cannot be armed.

**D1 and D10** are the documentation/claims defects (the headline feature has no
control path while the release notes describe it as working; docs and gate
metadata do not match the tree). The audit ranked them reachable today, but they
are a documentation lane's work, and I was told explicitly not to edit `docs/`
because another lane is editing `KNOWN-LIMITATIONS.md` right now. §7 has the
text I would write, quoted for central application.

**The ALSA backend has no capture path at all.** `src/core/audio/AudioAlsa.cpp`
opens `SND_PCM_STREAM_PLAYBACK` only (line 54) and `snd_pcm_readi` appears
nowhere in the tree. That is a known wall and a separate conversation, as the
brief says; I did not touch it. Under ALSA, `pushInputFrames()` is simply never
called, so `inputBufferFrames()` is 0 and the recorder would record silence —
this is unchanged by my work.

**The "hardware-verified" claim has no reproducible evidence behind it.**
`README.md:19-21` and `KNOWN-LIMITATIONS.md:70-74` claim the capture path is
"hardware-verified", resting on `tests/src/core/TwoTrackAlsaCaptureProbe.cpp`
and `recording/verify_alsa_probe.py`. The probe's CMake target was dropped by
merge `cc09f9379` (present at `a2a373305`, absent from `cc09f9379` onward,
absent at HEAD), `tests/QA-GATES.md:192-195` and `tests/no-tautology-gate.sh`
still describe it as a "registered" helper, and
`RECORDING-PROTOTYPE.md` — referenced by `TwoTrackRecordingHarness.cpp:74`,
`recording/DATA-FLOW.md:45` and the original CMake comment — does not exist
anywhere in the tree. **What it would take to restore that evidence:**

1. restore the `add_executable(TwoTrackAlsaCaptureProbe …)` block to
   `tests/CMakeLists.txt` (with the "deliberately NOT registered with CTest"
   comment), or make it a ctest target and gate it on a capture device being
   present;
2. re-create `RECORDING-PROTOTYPE.md` (or redirect the two references to
   `recording/DATA-FLOW.md`) so the probe's procedure is documented;
3. run it against real hardware **with the target restored**, check the log into
   the tree (no probe output, log or report is checked in today), and re-run
   `recording/verify_alsa_probe.py` so the sample-exact comparison is backed by
   an artifact;
4. only then re-state "hardware-verified", or drop the word until all three
   steps are done.

Note what the probe does **not** cover even when it runs (the audit's C69–C72):
it exercises libasound → `processInput()` → ring → writer, and its reference WAV
is written from the frames the probe itself pushed, so it proves demux/ring/
writer integrity, not fidelity to the device. It never touches `AudioAlsa`,
`AudioEngine`, `pushInputFrames()`, the input double buffer, `m_changeMutex`,
`renderNextPeriod()` scheduling, arm/disarm with a live producer, overflow,
write errors or rate mismatch.

---

## 6. Gates run, and the one that is absent

| gate | script | exit |
|---|---|---|
| 1 — unit tests (ctest) | via `tools/local-ci.sh` | **0** (27/27 passed) |
| 4 — file length | `tests/file-length-gate.sh` | **0** |
| 5 — mutation | `tests/mutation-gate.sh` | **0** (kill score 88.5% ≥ 80%) |
| 6 — no upstream regression | `tests/no-upstream-regression-gate.sh` | **0** (PASS: 34 files in the ledger) |
| 7 — complexity | `tests/complexity-gate.sh` | **0** |
| 8 — duplication | `tests/duplication-gate.sh` | **0** (1.31% vs 5% budget) |
| — no tautology | `tests/no-tautology-gate.sh` | **0** (both new test files counted, 0 tautologies) |
| **9 — fork sources** | `tests/fork-sources-gate.sh` | **ABSENT — not on this branch** |

Gate 9 exists only on lanes descended from `post-alpha/gate-debt`; this branch
is based on `post-alpha/v0.2`, whose `tests/` has `fork-sources.txt` but no
`fork-sources-gate.sh`. I registered the two new headers in
`tests/fork-sources.txt` anyway (they are exactly what the file's documented
regeneration command would produce), so a lane that inherits the gate sees no
violation.

Gate 2 (coverage, `tests/run-coverage.sh`) was **not run**: it needs
`--with-coverage`, i.e. a separate full coverage build. I am flagging it rather
than implying it passed.

Side effect worth recording: `tests/complexity-gate.sh` rewrites
`tests/complexity-baseline.tsv` in the working tree every time it runs
("baseline refreshed") — it also passes against the pristine baseline, so the
rewrite is gate-owned maintenance, not something this change needs. I reverted
that file so the commit touches only what it must.

Ledger updates (the reason strings are the ones in the diff):

```
include/AudioEngine.h          … post-alpha/recording-realtime: capture staging ring (fixed capacity, pre-allocated) replacing the locking, doubling input buffer, and pushInputFrames() taking const frames as noexcept
include/SampleRecordHandle.h   post-alpha/recording-realtime D9c: the clip record path stages frames into a pre-allocated ring and assembles the take on the accumulator's own thread …
src/core/AudioEngine.cpp       post-alpha/recording-realtime D9b/D9a: pushInputFrames() no longer takes m_changeMutex on the JACK/SDL capture thread and no longer doubles the input buffer; swapBuffers() drains the fixed-capacity staging ring instead
src/core/SampleRecordHandle.cpp post-alpha/recording-realtime D9c: writeBuffer() appends to the pre-allocated ring instead of allocating per period, and the destructor installs the take the drain thread already built …
```

`tests/fork-sources.txt` gained `include/SampleFrameRingBuffer.h` and
`include/SampleRecordAccumulator.h`.

---

## 7. Documentation I would write (not applied — `docs/` is another lane's)

The brief says another lane is editing `KNOWN-LIMITATIONS.md` right now and a
second editor would collide, so these are quoted exactly as I would write them,
for central application.

**`README.md:19-21`** — the overstatement is the word `hardware-verified`, not
the "(prototype)" framing. Replace the claim with:

> Two-track capture is a prototype and is not yet armable from the UI. Its
> engine-level component is covered by `TwoTrackRecordingHarness` (synthetic
> input, sample-exact). The real-capture ALSA probe
> (`TwoTrackAlsaCaptureProbe`) is **not** built by this tree today — its CMake
> target was dropped in merge `cc09f9379` and no run log is checked in — so do
> not describe the capture path as hardware-verified until that target, its
> `RECORDING-PROTOTYPE.md` procedure and a checked-in run log are restored.

**`docs/KNOWN-LIMITATIONS.md:70-74`** — "under ALSA hardware input records
silence" is wrong (nothing records at all); replace with:

> - Under ALSA nothing records: the ALSA backend has no capture path
>   (`AudioAlsa.cpp` opens `SND_PCM_STREAM_PLAYBACK` only, and `snd_pcm_readi`
>   appears nowhere in the tree), so `inputBufferFrames()` is always 0 and the
>   recording prototype is fed nothing. This is not "records silence" — the
>   capture path does not exist.
> - "Verified with a real capture device in the project's tests" is withdrawn:
>   the probe's CMake target is gone (dropped in merge `cc09f9379`), no probe
>   output or report is checked in, and `RECORDING-PROTOTYPE.md` — which the
>   probe, the harness and the original CMake comment all reference — does not
>   exist in the tree.

**A new entry for `KNOWN-LIMITATIONS.md`, recording what is now fixed**, so the
next reader does not re-derive it:

> - Out-of-range samples are no longer written as wrap artefacts on the
>   recorder's WAV path: the writer now clamps to [-1, 1] before writing
>   (libsndfile does not clip by default). In-range audio is unchanged, sample
>   for sample; see `tests/src/core/RecordClipTest.cpp`.
> - The capture path takes no lock, allocates nothing per period and cannot grow
>   without bound: `pushInputFrames()` writes into a fixed 16384-frame staging
>   ring (128 KiB, allocated once) that `swapBuffers()` drains once per period,
>   and the clip record path stages frames the same way while a drain thread owns
>   the take. See `tests/src/core/RecordingRealtimeTest.cpp`.

---

## 8. Files touched

| file | change |
|---|---|
| `include/SampleFrameRingBuffer.h` | **new** — SPSC lock-free ring of stereo `SampleFrame`, pre-allocated, drop-newest with a rejected-frame counter (stereo sibling of `RecordRingBuffer.h`) |
| `include/SampleRecordAccumulator.h` | **new** — stages clip recordings into a pre-allocated ring and assembles the take on its own drain thread |
| `include/AudioEngine.h` | staging-ring member + `InputStageCapacityFrames`, `inputFramesStaged()`, `inputFramesDropped()`, `pushInputFrames(const …, noexcept)`, `drainInputStage()` |
| `src/core/AudioEngine.cpp` | ctor allocates the ring once; `pushInputFrames()` lock-free/allocation-free; `swapBuffers()` drains it; per-period input buffers sized to the ring capacity |
| `src/core/audio/TrackRecorder.cpp` | clamp to [-1, 1] on the writer thread before `sf_writef_float` |
| `include/SampleRecordHandle.h` | `unique_ptr<SampleRecordAccumulator>` replaces the `QList` of per-period blocks; `createSampleBuffer()` removed (it had no other caller) |
| `src/core/SampleRecordHandle.cpp` | ctor builds the accumulator; `writeBuffer()` appends to the ring; dtor installs the finished take |
| `tests/src/core/RecordClipTest.cpp` | **new** — DEFECT 6, with the inverted control |
| `tests/src/core/RecordingRealtimeTest.cpp` | **new** — D9a/D9b/D9c: ring invariants, SPSC concurrency, zero-allocation append, complete take, lock independence, bounded staging |
| `tests/CMakeLists.txt` | registers the two tests; `QT_QPA_PLATFORM=offscreen` for `RecordingRealtimeTest` (it builds a real `Engine`) |
| `tests/fork-sources.txt` | the two new headers |
| `tests/upstream-modifications.txt` | reasons for `src/core/AudioEngine.cpp`, `src/core/SampleRecordHandle.cpp`, `include/SampleRecordHandle.h`; extended reason for `include/AudioEngine.h` |

No other worktree, remote or branch was touched. Nothing was pushed.
