# The recording engine surface — the capture IN path, the record routes, and the retro window (0.3.0)

Feature rows **64** (arbitrary input count / multiple simultaneous inputs), **14** (multi-track recorder) and
**16** (retrospective audio capture) of `docs/FEATURE-LIST-0.3.0.md`. This is the design record for the three
of them together, because they are one chain: an input path decides how many channels exist, a record route
picks one of them, and the retro window is the same frames kept backwards for a while.

Read with `docs/MIDI-RETRO-CAPTURE.md` (the MIDI half of retrospective capture, the model the audio half
copies) and `docs/RECORDING-REALTIME-FIXES.md` (the realtime rules the record path already keeps).

## 1. What was measured wrong, and what replaced it

The audit's own words, which the code now answers:

- row 14: *"a real 2-track recorder is in the tree with tests … but no `record.*` group drives the recorder;
  and the id that should cover it, `track.set_arm`, is a registered refusal stub — the feature and the command
  contradict each other"*;
- row 64: *"Arbitrary input count / multiple simultaneous inputs — to build — the default Linux backend is
  playback-only today (`AudioAlsa` has no capture path)"*;
- `docs/COVERAGE-MATRIX-2026-09-13.md` §5: *"3 registered-but-always-refusing stubs"* — one of them was
  `track.set_arm`.

Three measurements bounded the work, all re-verified against this tree before a line was written:
`src/core/audio/AudioAlsa.cpp` contained **no** `snd_pcm_readi`; `AudioEngine::pushInputFrames()` existed but
was called only from the JACK and SDL backends; and `TrackRecorder::setInputChannel()` clamped to
`[0, DEFAULT_CHANNELS)` because `SampleFrame` is exactly two floats, so the whole input path was stereo by
type. The last one is the reason an N-channel path is a new type rather than a wider `SampleFrame`.

## 2. The shape, and why each piece is where it is

```
ALSA capture thread            AudioEngine (render thread)              record.* (UI thread)
────────────────────           ───────────────────────────              ────────────────────
snd_pcm_readi (blocking,       pushInputFramesWide ─┐                   record.input_set   → config file
 on its OWN thread) ──┐        m_wideInputStage ◄───┘ (SPSC ring)        record.input_get_state → live
                      │        swapBuffers(): drainWideInputStage()      record.arm_track / disarm_*
                      ├──────► m_inputBuffer[]  (stereo bus, unchanged)  record.get_state
                      │        renderNextPeriod() STAGE 4:
                      │          recorder.processInputInterleaved() ───► TrackRecorder k → WAV (one per route,
                      │          recorder.processInput() [no wide input]   + a take journal beside each)
                      └──────► m_retroCapture.push() ──────────────────► record.retro_capture_to_take → WAV
```

- **`include/AudioInputPath.h`** — the plan (`device`, `channels`, the `left`/`right` pair) and the live
  state a backend publishes. Process-wide because there is one input device per process and the command
  handler has no handle on the backend class. The four keys live under the config class `audioinput`, which
  is backend-neutral on purpose.
- **`include/InputChannelRing.h` + `include/AudioWideInputStage.h`** — the N-channel staging between a capture
  thread and the render thread. The width travels with the samples, which it could not do inside a
  `SampleFrame`. Same contract as the stereo staging ring (`SampleFrameRingBuffer`): allocated once in the
  constructor, lock-free, no syscall on the producer, drop-newest with a counter, drained once per period.
- **`src/core/audio/AudioAlsa.cpp`** — the capture path: a second PCM opened on its **own thread**, because
  `snd_pcm_readi()` blocks and a blocking read on the playback thread would stall the render. FLOAT is
  preferred to S16_LE; the channel count is the configured one; the loop waits with a **bounded 200 ms**
  timeout so the stop flag is honoured on a device that has gone quiet. Everything it touches is allocated
  before the thread starts.
- **`include/MultiTrackRecorder.h` / `TrackRecorder.h`** — N routes (fixed at construction, default 2, bound
  `MaxRoutes = 16`), each able to select **any** input channel in `[0, inputChannelCapacity)`, and each
  writing one mono 24-bit WAV with a take journal beside it. `MaxRoutes` bounds the pre-allocated rings
  (~4 MiB at 16 routes).
- **`include/RetroAudioCapture.h` / `RetroAudioRing.h`** — the audio half of retrospective capture: OFF by
  default, one bounded ring of the last 2^20 frames (~21.8 s at 48 kHz), drop-oldest, a publication-guarded
  snapshot, and a one-pass stereo 24-bit WAV writer. Model and differences are stated in the header.

## 3. What the commands are

| id | class (A16) | what it does |
|---|---|---|
| `record.input_get_state` | not_mutating | the plan, the device's own answer, the engine's two input stages |
| `record.input_set` | true_inverse | writes the plan; `restart_required`, previous plan as the inverse |
| `record.get_state` | not_mutating | every route: arm, channel, take, journal, counters |
| `record.arm_track` | snapshot | arms one route; inverse = `record.disarm_track` |
| `record.disarm_track` / `record.disarm_all` | not_mutating | stop the capture, retire the journal, keep the take |
| `track.set_arm` | snapshot | arms the route a song track's position maps to (was a refusal until 0.3.0) |
| `record.retro_capture_arm` / `_status` | not_mutating | the window's mode and its state |
| `record.retro_capture_to_take` | not_mutating | writes the retained window to a WAV |

## 4. The honest bounds

- **The real-interface half is hardware-bound and unverified on this box.** Whether a sound card opens, how
  many channels it grants and whether it delivers frames are *this machine's* answers, not properties of the
  feature. They are **reported, never assumed**: `record.input_get_state` carries `capture_capable`,
  `capture_open`, `capture_reason`, the granted channel count and rate, and the `bus_frames` / `wide_frames` /
  `input_frames_staged` counters. On a build with no capture device every one of them is `0` or `false`, and
  the ctest asserts that the three device fields are **internally consistent** (a backend with no capture path
  never claims a reason, a backend whose device refused always does) rather than equal to any particular
  value — this box's answer is not the acceptance criterion.
- `record.input_set` is **next-start**: the device is opened at startup and the route capacity is fixed when
  the engine is built. The command says so (`restart_required`), and the ctest proves it by measuring both
  sides of a restart rather than asserting it.
- A retrospective audio take is **not inserted into the session** — `docs/KNOWN-LIMITATIONS.md`.
- The engine's **stereo bus** is still `SampleFrame`-shaped (`DEFAULT_CHANNELS = 2`): the N captured channels
  reach the recorders through the wide path, and the bus carries the configured pair for every other input
  consumer. Widening the bus itself is not in 0.3.0.

## 5. Proof, as registered ctests

- `RecordingInputPathTest` — N routes × N channels, real takes, each route's WAV sample-exactly its own
  channel of the interleaved block; the stereo entry point unchanged; the channel and route refusals; the plan
  validation. **No hardware.**
- `RetroAudioCaptureTest` — off by default, bounded, drop-oldest with the count, zero allocations on the
  producer path (the dynamic probe), the window out in order, the take holding exactly the retained frames,
  and an empty window refused rather than written. **No hardware.**
- `ControlRecordInputs` (`tests/control-record-inputs.py`) — the **real binary over `--control-socket`**:
  registration of all nine new ids, the arbitrary input count proved **across a restart**, a route armed for
  input channel 7 (refused before the restart, accepted after), `track.set_arm`'s real capture taken back by
  `control.undo`, the retro window's arm/status/take, every typed refusal, and the A16 classes read back from
  `control.transactions`.
