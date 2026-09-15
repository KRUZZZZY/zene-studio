<!--
  In-repo copy of the specification this repository's code and tests cite (DOC-5, 2026-09-15).
  This is a PINNED SNAPSHOT, not a live document:
    source   : the program workspace, specs/SPEC-two-track-recording.md
    sha256   : a12afbd648c364176ce54651c56fe32d57952b91cbf8550503b83a88cc8f7bed
    bytes    : 7371
    why this file: a specification whose citation class is the program workspace's specs/ directory; cited by docs/CLIP-CAPTURE-DESIGN.md as the recording design's authority
  The workspace copy remains the working copy and is where the document is edited; refreshing
  this snapshot is a deliberate act, recorded in the commit that does it. If a citation in this
  repository and this file ever disagree, reconcile them in one commit - the citations name the
  file by bare name (e.g. `SPEC-zene-studio.md`), and this directory is where that name resolves.
  See docs/specs/README.md for the convention and for the cited documents NOT yet committed.
-->
# SPEC: Two-Track Simultaneous Recording Prototype

> **Task:** AI-KOS task #556 (program `lmms-fl-replacement-program`, mission `lmms-recording-mission`)
> **Status:** Draft · **Version:** 1.0 · **Written:** 2026-09-08
> **Sources:** findings-codebase.md, REPORT.md plan P1, mixer/SPEC-dynamic-routing.md §5 (realtime invariants), PR #8074 (experimental seed, Linux/ALSA, unmerged), direct source verification against clone 4e677cb

---

## 1. Scope & Goals

**In scope (prototype only):** two hardware inputs record simultaneously to two `SampleClip`s; both play back in the arrangement; dropout-free at 48 kHz / 24-bit; ALSA and JACK.

**Out of scope:** full recording UI (transport/arm toolbar), punch regions, take management, monitoring paths, Windows (WASAPI). These are mission phase 2+.

## 2. Current State in Code (verified vs clone 4e677cb)

| Component | Where | State |
|---|---|---|
| `AudioSampleRecorder` | include/AudioSampleRecorder.h:41 — `class AudioSampleRecorder : public AudioDevice` | EXISTS. An entire AudioDevice subtype that captures to file — full-device capture, not per-track |
| `SampleRecordHandle` | include/SampleRecordHandle.h:45 — `class SampleRecordHandle : public PlayHandle`; impl src/core/SampleRecordHandle.cpp | EXISTS. Per-play-handle capture primitive — the right granularity for per-track recording |
| Per-clip record flag | include/SampleClip.h:48 (`mapPropertyFromModel(bool,isRecord,setRecord,m_recordModel)`), :102 (`BoolModel m_recordModel`) | EXISTS. Clips already have a serialized record model — arm-state storage is already in the data model |
| Programmatic clip fill | include/SampleClip.h:83 — `void setSampleBuffer(std::shared_ptr<const SampleBuffer> sb)` | EXISTS. Recorded audio lands in a clip via this path |
| Clip class | include/SampleClip.h:45 — `class SampleClip : public Clip` | EXISTS. Note: REPORT.md's "AudioClip" name is stale — the class is `SampleClip` |
| Disk writer | include/AudioFileFlac.h:31, include/AudioFileWave.h:32 (libsndfile) | EXISTS — proven file-write path to reuse for streaming capture |
| Multi-input device capture | src/core/AudioEngine.cpp / AudioDevice implementations | PARTIAL — devices read interleaved capture buffers; per-channel demux to per-track handles is MISSING |

**What's missing (the actual work):** (1) demuxing the device capture buffer into N per-track input streams; (2) arming model on mixer channels or tracks; (3) a lock-free path from audio thread → disk-writer thread; (4) driving `SampleRecordHandle`-style capture per armed track simultaneously.

## 3. Design

### 3.1 Input routing model (PROPOSAL)
`InputRouter` (PROPOSAL: include/InputRouter.h): maps `hardwareInputIndex -> armedTrack`. Prototype hardcodes a 2-entry map from config; the mixer-mission's per-channel input routing generalizes it later.

### 3.2 Arm state
Prototype reuses `SampleClip::m_recordModel` (SampleClip.h:102) as the arm flag per target clip — no new model classes needed for the spike.

### 3.3 Lock-free transfer
SPSC ring buffer per armed track (same invariant class as mixer spec §5.2):
- Frame layout: `float[2]` interleaved, matching `SampleFrame` storage (`std::array<sample_t, DEFAULT_CHANNELS>`, DEFAULT_CHANNELS=2 — lmms_constants.h:38)
- Capacity: power-of-2 ≥ 65536 frames (~1.4 s at 48 kHz) — pre-allocated at arm time, never on the audio thread
- Audio thread: single producer, `write()` only, drops-oldest policy on overflow with an atomic overflow counter (never blocks)
- Disk thread: single consumer, batches ≥4096 frames per sndfile write
- Overflow counter checked by UI thread at 10 Hz → xrun indicator

### 3.4 Growing clip
Record to a temp WAV via `AudioFileWave` (sndfile path, AudioFileWave.h:32); on stop, decode via `SampleDecoder` (src/core/SampleDecoder.cpp:30, same sndfile dependency) into a `SampleBuffer` and attach with `SampleClip::setSampleBuffer` (SampleClip.h:83). Rationale: SampleClip's buffer model is immutable-shared; streaming into a live `SampleBuffer` would fight the design and risk the playback path. Temp-file roundtrip is simpler and io-safe.

### 3.5 Timestamps
Capture start aligned to transport start position; clips placed at transport-start TimePos. Sample-accurate alignment with already-playing tracks is an open question (OQ-2).

## 4. Realtime-Path Invariants (inherited from mixer/SPEC-dynamic-routing.md §5)

1. No allocation, no locks, no syscalls on the audio thread — ring buffer is pre-allocated
2. Disk thread is `SCHED_OTHER`; audio thread never waits on it
3. All arm/disarm state changes via atomics set from GUI thread
4. Ring buffer overflow degrades (drop) — never blocks

## 5. Test Plan (prototype-grade, manual but concrete)

| # | Test | Pass criterion |
|---|---|---|
| T1 | Record 60 s: 2 inputs → 2 clips (ALSA) | 2 clips non-silent (RMS > -60 dB), lengths within 1 sample of each other |
| T2 | Same via JACK | Same |
| T3 | Playback both clips with 8-instrument project running | No audible dropouts; `AudioEngineProfiler` no xrun spike attributable to capture |
| T4 | Stress: unplug/plug input mid-record | Overflow counter increments; no crash; partial audio intact |
| T5 | 24-bit verify | Temp WAV bit depth = 24 (sndfile info), noise floor consistent with 24-bit |

## 6. Phased Steps (with gates)

1. **G1** — Read-path spike: log device capture buffers per channel in AudioEngine worker; verify 2 distinct non-silent streams on disk log. *Gate: log shows both inputs at expected RMS.*
2. **G2** — Ring buffer + disk writer (headless, no GUI): capture 60 s to 2 WAVs. *Gate: T1/T2 pass with a CLI test harness.*
3. **G3** — Attach to SampleClips at transport position, playback. *Gate: T3 passes.*
4. **G4** — 24-bit + stress (T4/T5). *Gate: all tests pass; PROTOTYPE-NOTES.md written.*

## 7. Risks

| Risk | Mitigation |
|---|---|
| ALSA capture delivers channels in device-dependent order | Probe with loopback test in G1 before designing demux |
| Disk stall blocks consumer → overflow | Batches + big capacity; measure worst-case write latency in G2 |
| AudioEngine worker-thread ownership of capture differs ALSA/JACK | G1 spike covers both before G2 design freeze |
| `SampleRecordHandle` semantics don't compose for 2 simultaneous handles | G2 tests 2 concurrent handles explicitly; fall back to plain ring-buffer design (handles used only as reference) |

## 8. Open Questions (undecidable without spike — do not invent answers)

- **OQ-1:** Does `SampleRecordHandle` (SampleRecordHandle.h:45) support two concurrent instances on different tracks, or is it single-capture by design? (PR #8074 review needed + G2 test)
- **OQ-2:** Is sample-accurate alignment with running tracks achievable without engine-level capture-time insertion? (Depends on where AudioEngine exposes capture position)
- **OQ-3:** ALSA vs JACK capture-thread ownership — is input delivered on the audio callback thread or a device thread per backend? (G1 answers)

## 9. Sources

- Clone 4e677cb: include/AudioSampleRecorder.h:41; include/SampleRecordHandle.h:45; src/core/SampleRecordHandle.cpp; include/SampleClip.h:45,48,83,102; include/AudioFileWave.h:32; include/AudioFileFlac.h:31; src/core/SampleDecoder.cpp:30; include/lmms_constants.h:38
- PR #8074 (seed, state volatile — re-verify); REPORT.md plan P1; mixer spec §5 invariants
