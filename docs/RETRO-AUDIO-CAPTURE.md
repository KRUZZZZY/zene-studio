# Retrospective AUDIO capture (0.3.0, feature row 16)

The audio counterpart of `docs/MIDI-RETRO-CAPTURE.md`. Read that first: this half copies its model
deliberately, verb for verb and property for property, and this document only records what an audio window
forces to be different. The engine is `include/RetroAudioCapture.h` + `include/RetroAudioRing.h` +
`src/core/RetroAudioCapture.cpp`; the commands are `record.retro_capture_arm`, `record.retro_capture_status`
and `record.retro_capture_to_take` (`src/core/ControlCommandsRecordingRetro.cpp`).

## 1. The model, restated in one paragraph

**Off by default.** Nothing is recorded until `arm(true)`, and the whole cost on the audio thread while
disarmed is **one relaxed atomic load per rendered period**. Armed, the window is one **bounded,
pre-allocated, drop-oldest** ring that always holds the most recent frames, with a counter for what fell out
of it; the consumer takes a consistent copy through a publication-sequence handshake that never makes the
audio thread wait. Storage is allocated **exactly once**, in the object's constructor, which runs off the
audio thread. The arm flag is **runtime state, not project state**, so arming records no A16 transaction
(the `midi.retro_capture_arm` precedent).

## 2. What audio changes

| | MIDI half | AUDIO half | why |
|---|---|---|---|
| element | one 16-byte POD event | one `SampleFrame` (8 bytes) | the audio window's unit is a frame, so the capacity is frames |
| capacity | 8192 events, "7-13 minutes of the densest playing" | 2^20 frames = 8 MiB, **21.8 s at 48 kHz / 11.9 s at 96 kHz** | for audio the interesting quantity is TIME ("what did I just play"), and a window you can state in seconds is a window a user can plan around |
| the past it names | events, in arrival order | seconds, at the engine's own sample rate | the status command reports both the frames and the seconds, and the seconds are what a caller compares |
| source | the MIDI input threads (`MidiAlsaSeq::run()`, the raw clients' `processParsedEvent()`) | **the engine's own input path**, in `renderNextPeriod()`'s STAGE 4b | a MIDI event arrives when it arrives; the audio the engine *had* is the engine's own input buffer, so this capture needs no device, no client and no extra thread |
| the take | notes written into a clip (`midi.retro_capture_to_clip`) | a **stereo 24-bit WAV** written in one pass with libsndfile | the same encoder the forward recorder uses, so a retrospective take and a recorded take are the same kind of file |
| the bound | `RetroMidiCapture::recordable()` filters system real-time/common bytes | the window records the **stereo bus** (`AudioEngine::inputBuffer()`), i.e. the pair of captured channels the input path was configured to carry | system messages are not music; a silent stereo bus is still what the input path held |

## 3. What it does NOT do in 0.3.0

- **It does not insert the take into the session.** The WAV is written and the command returns its path,
  frame count, length in seconds and the window's own numbers; turning that file into a `SampleClip` is the
  product's own load path, and this group does not invent a second one. The same absence
  `record.recovery_restore` states for a recovered take. Stated in `docs/KNOWN-LIMITATIONS.md`.
- **It is not persisted** in the project or the config: the arm flag is mode state that dies with the
  instance (the MIDI half explains why the persisted key belongs with a command surface that can write it from
  a context where `qApp` exists).
- **It has no interface.** There is no menu item, no toolbar button and no indicator: `record.retro_capture_*`
  is drivable through the control socket only, and that is written down as the feature's UI absence.

## 4. Proof

`tests/src/core/RetroAudioCaptureTest.cpp` (registered ctest `RetroAudioCaptureTest`): off-by-default, the
window being the **last** `capacity()` frames with the overwritten count, **zero allocations on the producer
path** (the programme's dynamic allocation probe, run on the entry point the audio thread calls), the window
copied out oldest-first and complete, the take read back with libsndfile and compared frame by frame with the
retained window, and an **empty window refused, typed, with no file created**.

`tests/control-record-inputs.py` (registered ctest `ControlRecordInputs`) drives the same three commands over
`--control-socket` on the real binary: the mode is off, arms, reports its capacity in frames and seconds, is
disarmed by an explicit `armed: false`, and then either writes the take or — on an instance whose input path
captured nothing — **refuses, typed, instead of writing a zero-length file**.
