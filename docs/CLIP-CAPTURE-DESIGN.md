# Clip-and-capture wave — design (#611)

**Status: design only, no code.** Read-only work on `post-alpha/clip-capture-spec` (base
`0c23587d2`). Every claim about the tree carries a `path:line` that was read; claims taken from a
parallel evidence sweep rather than my own read are marked **[sweep]**. Claims I could not verify
are marked **UNVERIFIED** — they are questions for the implementation lane, not prose.

This document designs the wave the product boarded as **#611** "clip-and-capture wave (xl, yellow)"
(`docs/STATUS.md:216-224`). It owns: non-destructive clip editing (trim, slip, fades, crossfades,
clip gain); take lanes with non-destructive comping; punch in/out; arbitrary input count and input
monitoring — audio first. It lands *before* #597 (warp), #598 (Session View drag-drop) and #600
(comping).

The task's own acceptance criteria (from the board, #611 spec) are quoted where a design choice is
driven by them.

---

## 1. What exists, with evidence

### 1.1 An audio clip is a `SampleClip`, and its source audio is a refcounted immutable buffer

- `Clip` is the base for every clip type: `include/Clip.h:49`
  `class LMMS_EXPORT Clip : public Model, public JournallingObject`.
- Its whole persisted state is four fields and two models: `include/Clip.h:167-180` —
  `m_track`, `m_name`, `TimePos m_startPosition` (`:170`), `TimePos m_length` (`:171`),
  `TimePos m_startTimeOffset` (`:172`), `m_mutedModel` (`:174`), `m_soloModel` (`:175`),
  `m_autoResize` (`:176`), `m_color` (`:180`).
- `SampleClip` adds the sample and a frame offset: `include/SampleClip.h:45`
  `class SampleClip : public Clip`; `include/SampleClip.h:101-104`
  `Sample m_sample; BoolModel m_recordModel; bool m_isPlaying; int m_startFrameOffset;`.
- The audio data is shared by reference, not copied. `Sample` holds
  `std::shared_ptr<const SampleBuffer> m_buffer` (`include/Sample.h:111`), backed by
  `std::vector<SampleFrame> m_data` (`include/SampleBuffer.h:91`). Copying a `Sample` copies the
  `shared_ptr` — `src/core/Sample.cpp:48` `m_buffer(other.m_buffer)`. **This is the non-destructive
  primitive that already exists**: two clips can reference one buffer at zero audio cost.
- `Sample` already carries an editable window plus gain and pitch:
  `include/Sample.h:92-98` `startFrame()`, `endFrame()`, `loopStartFrame()`, `loopEndFrame()`,
  `amplification()`, `frequency()`, `reversed()`; atomics at `include/Sample.h:112-118`;
  setters at `include/Sample.h:100-107`.

### 1.2 A clip's position and length are ticks; the audio window is frames

- Position/length are `TimePos` in ticks, clamped at zero on move:
  `src/core/Clip.cpp:115-126` (`TimePos newPos = std::max(0, pos.getTicks());` at `:117`;
  `requestChangeInModel()`/`doneChangeInModel()` at `:120`/`:122`; `emit positionChanged()` at `:124`).
- `changeLength` is a bare assignment with an equality guard: `src/core/Clip.cpp:138-145`.
- The two clocks are bridged by `Engine::framesPerTick()`:
  `src/core/SampleClip.cpp:257-260` `sampleLength()` returns
  `static_cast<int>(m_sample.sampleSize() / Engine::framesPerTick(m_sample.sampleRate()))`.
- **The window is currently derived from playback position, not authored.** In
  `src/tracks/SampleTrack.cpp:112-133`: `_start >= sClip->startPosition() + sClip->startTimeOffset()`
  (`:114`), then `sampleStart = bufferFramesPerTick * (_start - startPosition - startTimeOffset)`
  (`:117`), `clipFrameLength` (`:118`), `sampleBufferLength` (`:119`),
  `samplePlayLength = clipFrameLength > sampleBufferLength ? sampleBufferLength : clipFrameLength`
  (`:122`), and finally **the read path mutates the clip**:
  `sClip->setSampleStartFrame( sampleStart );` (`:126`) and `sClip->setSamplePlayLength( samplePlayLength );` (`:127`).
- Those setters write straight into the clip's own `Sample`:
  `src/core/SampleClip.cpp:265-268` (`m_sample.setStartFrame`) and
  `src/core/SampleClip.cpp:273-276` (`m_sample.setEndFrame`).
- And this runs on the audio thread: `src/core/Song.cpp:340`
  `track->play(getPlayPos(), framesToPlay, frameOffsetInPeriod, clipNum);` sits in
  `Song::processNextBuffer()` (`src/core/Song.cpp:207`), which
  `AudioEngine::renderStageNoteSetup()` calls at `src/core/AudioEngine.cpp:241` (stage list at
  `src/core/AudioEngine.cpp:328`). **A playback pass overwrites the clip's own editable window.**
  This is the defect the wave exists to remove — §2.5.
- Playback length comes from the window, not from the clip: `src/core/SamplePlayHandle.cpp:136-140`
  `totalFrames()` = `(m_sample->endFrame() - m_sample->startFrame()) * (outRate / sampleRate)`. The
  buffer is rendered by `src/core/SamplePlayHandle.cpp:108` `m_sample->play(workingBuffer, &m_state, frames)`
  (comment at `:106-107` confirms this path is previews, SampleTracks and the metronome).
- The window terminates playback in `Sample::render`: `src/core/Sample.cpp:143`
  `if (state->m_frameIndex < 0 || state->m_frameIndex >= m_endFrame) { return frame; }`, and gain is
  already applied per frame at `src/core/Sample.cpp:169-170`
  `* m_amplification`.

### 1.3 Slip already exists, and it is already non-destructive — but it is fragile

- `Clip::startTimeOffset()` / `setStartTimeOffset()` — `include/Clip.h:141-142`;
  a plain assignment at `src/core/Clip.cpp:214-225`.
- `SampleClip` overrides it to keep the frame-level copy in step — which is the tell that two
  representations of the same quantity exist:
  `src/core/SampleClip.cpp:250-254` `m_startFrameOffset = startTimeOffset * Engine::framesPerTick();`
- It is **reset** in three places, and every one of them is a data-loss path for a user's slip:
  `src/core/SampleClip.cpp:157` (`setSampleFile`), `src/core/SampleClip.cpp:236` (`updateLength`),
  and recomputed wholesale in `src/core/SampleClip.cpp:243-248` (`tempoChanged`).
- `updateLength()` is the landmine: `src/core/SampleClip.cpp:225-240` — if `getAutoResize()` it does
  `changeLength(sampleLength())` and `setStartTimeOffset(0)`. A tempo change, a time-signature
  change, or a load therefore **snaps a clip back to the full sample length with zero slip**. The
  `autoresize` attribute (`saveSettings` `src/core/SampleClip.cpp:295`) is what protects a manually
  resized clip, because the GUI sets `setAutoResize(false)` on every manual resize
  (`src/gui/clips/ClipView.cpp:904, 913, 923, 986`, and on split `:1463, 1468`).
- The left-edge drag is already slip-in-place and already non-destructive:
  `src/gui/clips/ClipView.cpp:965-987` — `positionOffset` (`:965`), `movePosition` (`:968`),
  `changeLength` (`:969`), then `setStartTimeOffset(m_clip->startTimeOffset() + positionOffset)` (`:984`).
- `reverseSample` is the one existing sample-window edit and it repairs the offset arithmetically:
  `src/gui/clips/SampleClipView.cpp:378-384`, with
  `m_clip->setStartTimeOffset(m_clip->length() - m_clip->startTimeOffset() - m_clip->sampleLength())` (`:381`).

### 1.4 How the Song Editor draws and edits clips

- Drawing: `src/gui/clips/SampleClipView.cpp:231-373`. The waveform starts at
  `offsetStart = m_clip->startTimeOffset() / ticksPerBar * pixelsPerBar()` (`:304`) and its width is
  `sampleLength = m_clip->sampleLength() * ppb / ticksPerBar` (`:305`). The thumbnail is handed
  `amplification` and `reversed` (`:313-318`), so gain already affects *rendering* — but nothing sets
  `amplification` anywhere in the clip path (verified: the only `setAmplification` caller in the tree
  is `tests/reference/AudioFileProcessor.cpp:344`, reference code that is not in the build).
- **No fades.** There is no fade drawing, no fade handle and no crossfade anywhere: a grep for
  `fade|crossfade|clipgain|clip_gain` over `src/` and `include/` returns only `FadeButton` (the
  instrument-track mute/note glow widget — `src/gui/tracks/FadeButton.cpp:2`) and `Instrument.cpp`'s
  own note-attack fade (`src/core/Instrument.cpp:149-174`). Clip gain has no model and no UI.
- Editing gestures: `include/ClipView.h:190-200` is the complete action set —
  `None, Move, MoveSelection, Resize, ResizeLeft, Split, CopySelection, ToggleSelected`. There is no
  Trim, Fade, Gain or Slip action. Handlers: `src/gui/clips/ClipView.cpp:621` (press),
  `:792` (move), `:1021` (release), `:886-1000` (resize/split), `:1070` (context menu).
- The context menu is Delete/Cut/Copy/Paste/Mute + clip colour + auto-resize
  (`src/gui/clips/ClipView.cpp:1070-1152`); `SampleClipView` adds Reverse sample, automation ghost
  and (opt-in) split-to-stems (`src/gui/clips/SampleClipView.cpp:86-116`). Nothing about trim, slip,
  fades or gain.
- Split is already non-destructive for audio: `src/gui/clips/ClipView.cpp:1448-1472` clones
  (`:1460`), halves the lengths (`:1462`, `:1466`) and adjusts the right clip's offset (`:1467
  m_clip->startTimeOffset() - m_clip->length()`). Both halves share the buffer (§1.1). There is a
  `destructiveSplitClip` hook for MIDI (`include/ClipView.h:258-261`); audio does not use it.
- **Recording state is drawn but disabled**: `src/gui/clips/SampleClipView.cpp:355`
  `// recording sample tracks is not possible at the moment`, with the "Rec" indicator commented out
  at `:357-368`.

### 1.5 The recorder, and how a take is bounded

- Per-track stream: `src/core/audio/TrackRecorder.cpp`. `arm()` (`:72-101`) resets the ring, opens
  **mono** 24-bit WAV — `info.channels = 1;` (`:89`), `SF_FORMAT_WAV | SF_FORMAT_PCM_24` (`:90`) —
  and starts the disk-writer thread (`:99`). `disarm()` (`:106-125`) sets the stop flag, drains,
  `sf_write_sync` + `sf_close` (`:121-122`) and joins.
- Audio-thread entry is a one-channel demux: `src/core/audio/TrackRecorder.cpp:130-144`, with
  `m_ring->writeStrided(input->data() + channel, DEFAULT_CHANNELS, frames)` (`:141-142`), documented
  realtime-safe at `:137-139`.
- The ring is a lock-free SPSC buffer allocated once in its constructor
  (`include/RecordRingBuffer.h:62-67`), drop-newest on overflow (`:78-82`, `:110-113`), with the
  contract written out at `include/RecordRingBuffer.h:41-57`. Capacity 65,536 frames and a 4,096-frame
  write batch: `include/TrackRecorder.h:66-68`.
- **A take's bounds are wall time between `arm()` and `disarm()`, and nothing else.** There is no
  position argument anywhere on the capture path: `processInput(const SampleFrame* input, f_cnt_t frames)`
  (`include/TrackRecorder.h:92-94`, `src/core/audio/TrackRecorder.cpp:130`). No punch region, no
  pre-roll, no sample-accurate start. The prototype's own record says arm time only:
  `projects/lmms-fl-research/RECORDING-PROTOTYPE.md:59` (quoted **[sweep]**),
  "`arm()` allocates the ring + opens the file + spawns T4 … `disarm()` sets a stop flag, T4 drains and closes".
- The other, older recorder is the destructive one: `SampleRecordHandle` accumulates
  `new SampleFrame[_frames]` blocks (`src/core/SampleRecordHandle.cpp:131`) and, on destruction,
  **replaces the clip's whole buffer** — `src/core/SampleRecordHandle.cpp:53`
  `if (!m_buffers.empty()) { m_clip->setSampleBuffer(createSampleBuffer()); }`. It reads the engine
  input buffer directly (`src/core/SampleRecordHandle.cpp:68-69`) and is created from
  `SampleTrack::play` when a clip has `isRecord()` (`src/tracks/SampleTrack.cpp:149-157`). It
  allocates per period on the audio path — the prototype's finding
  (`RECORDING-PROTOTYPE.md:844-847` **[sweep]**) and the reason the new path exists.
- `AudioSampleRecorder` is a third, unwired path: it copies all `DEFAULT_CHANNELS` into allocated
  blocks (`src/core/audio/AudioSampleRecorder.cpp:87-97` **[sweep]**) and has no caller outside its
  own translation unit **[sweep]**.

### 1.6 What the mixer/engine input path can actually address today

- The engine has exactly one input buffer, double-buffered:
  `include/AudioEngine.h:389-393` (`m_inputBuffer[2]`, `m_inputBufferFrames[2]`, `m_inputBufferSize[2]`,
  read/write indices); accessors `include/AudioEngine.h:239-241` and `:244-246`.
- It is filled only by `pushInputFrames`: declared `include/AudioEngine.h:237`, defined
  `src/core/AudioEngine.cpp:173-198`. **Exactly two callers exist in the tree**, both backend
  callbacks: `src/core/audio/AudioJack.cpp:432` and `src/core/audio/AudioSdl.cpp:187` (verified by
  grep; JACK's `jack_port_get_buffer` at `:425` **[sweep]**).
- **`pushInputFrames` is not realtime-safe.** It takes a lock —
  `src/core/AudioEngine.cpp:175` `requestChangeInModel();` and `:197` `doneChangeInModel();` — and it
  reallocates the input buffer with a bare `new` on growth: `src/core/AudioEngine.cpp:184`
  `auto ab = new SampleFrame[size];` followed by `memcpy`/`delete[]` (`:185-186`). It is called from
  the JACK and SDL device callbacks, i.e. the capture thread.
- **ALSA has no capture path.** The only `snd_pcm_open` in `src/core/audio/AudioAlsa.cpp` is
  playback: `src/core/audio/AudioAlsa.cpp:54` `snd_pcm_open(..., SND_PCM_STREAM_PLAYBACK, 0)` — no
  `snd_pcm_readi` anywhere in the file. The consequence is stated in the engine's own comment:
  `src/core/AudioEngine.cpp:333-337`, "Under the ALSA backend `inputBufferFrames()` is always 0
  (no capture path in AudioAlsa); JACK/SDL backends push input frames via `pushInputFrames()`."
- That comment sits directly above the single recorder feed: `src/core/AudioEngine.cpp:338`
  `m_recorder.processInput(m_inputBuffer[m_inputBufferRead], m_inputBufferFrames[m_inputBufferRead]);`.
- SDL captures: `src/core/audio/AudioSdl.cpp:99`/`:104` open the capture device
  (`SDL_OpenAudioDevice(..., 1, ...)`, iscapture=1), callback at `:177-187` ends in
  `pushInputFrames` (`:187`), and the setup widget exposes an input device combo (`:204`, `:208`).
  PortAudio opens an input stream but its callback discards input **[sweep]**. JACK, SDL only.
- **Arbitrary input counts are not reachable today — the count is a compile-time constant.**
  `include/MultiTrackRecorder.h:55` `static constexpr int NumTracks = 2;` and
  `src/core/audio/MultiTrackRecorder.cpp:36-41` with its own comment: "Hardcoded prototype mapping:
  track 0 <- input channel 0, track 1 <- input channel 1". The channel index is clamped to the
  compile-time constant: `src/core/audio/TrackRecorder.cpp:57`
  `std::clamp(channel, 0, static_cast<int>(DEFAULT_CHANNELS) - 1)`, with
  `include/lmms_constants.h:38` `DEFAULT_CHANNELS = ch_cnt_t{2}`. So exactly channels 0 and 1 are
  addressable. The ALSA setup widget pins its channel box to that constant as well:
  `src/gui/AudioAlsaSetupWidget.cpp:74` `m->setRange(DEFAULT_CHANNELS, DEFAULT_CHANNELS);`.
- There is no device-level input API: `include/AudioDevice.h:57-59` is the whole of it
  (`supportsCapture()` returning `m_supportsCapture`, default false at
  `src/core/audio/AudioDevice.cpp:34`, member `include/AudioDevice.h:100`). No input enumeration, no
  input device name, no input callback on the base class.
- The program's own documents say the same thing, and are the local authority here:
  `docs/KNOWN-LIMITATIONS.md:70-74` — "in the app the audio input only reaches it through the JACK
  and SDL backends. The ALSA backend has no capture path, so under ALSA hardware input records
  silence. There is no arbitrary input count and no input monitoring."; and
  `specs/SPEC-two-track-recording.md:13` — "**Out of scope:** full recording UI (transport/arm
  toolbar), punch regions, take management, monitoring paths". The prototype's backlog item is
  `RECORDING-PROTOTYPE.md:739-743` **[sweep]**: "add a capture path to the ALSA backend
  (`snd_pcm_readi` → `AudioEngine::pushInputFrames()`) — or run under a JACK server … Until then,
  hardware capture into the engine is impossible."

### 1.7 Serialisation conventions (what the project file is and who writes it)

- Wrapper: root `<lmms-project>` with `version` and `type` — `src/core/DataFile.cpp:136-138`
  (`createElement( "lmms-project" )`, `setAttribute( "version", m_fileVersion )`,
  `setAttribute( "type", typeName( type ) )`); the document name is the same string (`:128`).
- **The version is derived from the number of upgrade methods**, so appending one bumps the format:
  `src/core/DataFile.cpp:133` `m_fileVersion( UPGRADE_METHODS.size() )`, the table at `:73`. Load
  parses the attribute and runs migrations: `:2166-2176` and `:2205`
  `if (m_fileVersion < UPGRADE_METHODS.size()) { upgrade(); }`; `upgrade()` at `:2087-2099`.
- Song save: `src/core/Song.cpp:1215-1246` `saveProjectFile`, `DataFile dataFile( DataFile::Type::SongProject )`
  (`:1219`), `saveState( dataFile, dataFile.content() )` (`:1227`), `dataFile.writeFile(...)` (`:1246`).
  Song's node name is `"song"` (`include/Song.h:281-284`). Load is `src/core/Song.cpp:1008`, with the
  track containers found by tag and restored at `:1096-1123`.
- **The clip serialiser the wave must follow is `SampleClip::saveSettings` / `loadSettings`**, driven
  by the track: `src/core/Track.cpp:221-224`
  `// now save settings of all Clip's` → `for (const auto& clip : m_clips) { clip->saveState(doc, element); }`.
  (That is `saveState`, not `saveSettings` — `saveState` is the `SerialisingObject` wrapper.) Load
  mirrors it: `src/core/Track.cpp:284-303`, with `createClip( TimePos( 0 ) )` + `clip->restoreState(...)`
  at `:297-299`. Clip node names: `"sampleclip"` (`include/SampleClip.h:64-67`), and per-type for
  `MidiClip` (`include/MidiClip.h:105`), `PatternClip` (`include/PatternClip.h:45`),
  `AutomationClip` (`include/AutomationClip.h:179`).
- The attributes `SampleClip` writes today: `src/core/SampleClip.cpp:281-312` — `pos` (`:289`),
  `len` (`:291`), `muted` (`:292`), `src` (`:293`), `off` (`:294`), `autoresize` (`:295`),
  `data` (base64, only when `src` is empty, `:299`), `sample_rate` (`:302`), `color` (`:305`),
  `reversed` (`:309`) — **and the file says exactly where the gap is**: `src/core/SampleClip.cpp:311`
  `// TODO: start- and end-frame`. `loadSettings` (`:317-356`) never reads a window either. So today
  a trim window would not survive save/load: the model has nowhere to put it.
- Resource embedding is a whitelist keyed by element name:
  `src/core/DataFile.cpp:67-70` `ELEMENTS_WITH_RESOURCES = { { "sampleclip", {"src"} }, { "audiofileprocessor", {"src"} } }`,
  used by `copyResources` (`:446`, rewrite to `"local:resources/…"` at `:515`) behind
  `writeFile(filename, withResources)` (`include/DataFile.h:77`, `src/core/DataFile.cpp:315`, bundle
  directory at `:341-345`). A new element with no `src` attribute needs no entry here.
- The precedent for round-trip testing is `tests/src/core/SlideNotesTest.cpp:161`
  `projectWithoutSlidesRoundTripsUnchanged`, which asserts a **byte-identical** re-save at `:213-214`.
  `tests/src/core/RelativePathsTest.cpp`, `ProjectVersionTest.cpp` and
  `MixerRoutingBackwardCompatTest.cpp:184,256-259` cover the path, version and back-compat halves.
- #611's own constraint (board spec): "Do not change the project-file format in a way old builds
  cannot load; additive XML only, behind a versioned element."

### 1.8 Take lanes, comping, punch and monitoring do not exist in any form

- `grep -rniE "\blane"` and `comping|\bcomp\b` over `src/` and `include/`: **0 hits** (**[sweep]**,
  reproduced); `take(s)` appears only in English prose in comments. The only parallel capture is the
  fixed two-stream `MultiTrackRecorder`.
- There is no input channel or input device selector for the recorder anywhere in `src/gui`; the
  only `inputChannel` hits in the GUI are MIDI (`ControllerConnectionDialog`, `InstrumentMidiIOView`)
  **[sweep]**. Arm and input selection are atomics with no UI
  (`RECORDING-PROTOTYPE.md:844-847` **[sweep]**: "No UI, no undo/redo, no project serialization of
  arm state").
- No monitoring: grep for `monitor` over `src/` and `include/` outside profiler/remote-plugin uses
  returns nothing. The gap analysis is blunt about it — `ableton-gap`/`REPORT.md:35` and
  `findings-gap-analysis.md:52` record "Audio input monitoring ❌ Not present" (`REPORT.md`,
  `findings-gap-analysis.md` **[sweep]** — program research, i.e. inside the workspace but outside
  this tree).
- Timeline transport state exists and is the natural host for a punch range:
  `include/Timeline.h:104-107` `m_loopBegin`, `m_loopEnd`, `m_loopEnabled`, `m_pos`. The time line
  widget owns the drag actions: `include/TimeLineWidget.h:186-194`
  `enum class Action { NoAction, MovePositionMarker, MoveLoopBegin, MoveLoopEnd, MoveLoop, SelectSongClip }`,
  with the handler `src/gui/editors/TimeLineWidget.cpp:233-255` (`MoveLoopBegin`/`MoveLoopEnd` at
  `:251-252`). Constructed at `src/gui/editors/SongEditor.cpp:99` (`m_timeLine`) and `:113`
  (`m_positionLine`). **There is no punch state** — no `punchBegin`, no armed range.
- The Session View model is **not in this tree**: `SessionModel`/`ClipSlot`/`Scene` and
  `<session version=` have 0 hits in `src/` and `include/` (**[sweep]**); only `docs/STATUS.md:30-33`
  records it, on another branch, "**Not on `main`; no UI.**"
- The browser's drag-and-drop half that #598 waits on is half-built: `SampleClipView` already accepts
  `StringPairDrag` keys `"samplefile,sampledata"` (`src/gui/clips/SampleClipView.cpp:133-134`,
  handling at `:147-158`), and the file browser already produces `"samplefile"` drags
  (`src/gui/FileBrowser.cpp:893` **[sweep]**). Clips drag as `"clip_<tracktype>"` with a `DataFile`
  XML payload (`src/gui/clips/ClipView.cpp:446-452`, producer `:830-832`).

### 1.9 The recording mission's own design (KB, quoted as prior art)

The KB mission `lmms-recording-mission` (type `mission`, created 2026-09-08) already specifies the
shape of the capture half, and this document does not contradict it: "Phase 1 adds an
AudioInputDevice abstraction: mixer channels get input-routing models (which hardware input(s),
armed state, monitor state), and a recording transport controller coordinates count-in, punch-in/out
regions on the timeline, and take management. … Phase 2 adds the editor: waveform display with
slip/trim/fade … plus per-clip pitch/time-stretch hooks." Its success criteria name the same four
channels this wave is measured at ("No dropouts at 48kHz/24-bit with 4 simultaneous input channels").
`specs/SPEC-two-track-recording.md:31-32` flags the input router as a **PROPOSAL**
(`include/InputRouter.h`, "the mixer-mission's per-channel input routing generalizes it later") —
i.e. the generalisation is planned but unbuilt.

---

## 2. The model

### 2.1 The separation the current code lacks

There are two different things conflated in `SampleClip` today:

1. **Where the frames come from** — a shared immutable `SampleBuffer` plus a frame range inside it.
   Authored, must be persisted, must be reproducible.
2. **Where the frames land in time** — the clip's position, length, slip and (later) warp mapping.
   Authored, must be persisted.

Playback currently recomputes (1) *from* (2) on the audio thread, writing back into the clip
(`src/tracks/SampleTrack.cpp:117-127`). Invert that: **(2) is derived from (1), once, and (1) is
never written by the read path.**

### 2.2 Data structures

**`SampleWindow`** (new, `include/SampleWindow.h`) — a value type, the editable window on the source:

```cpp
struct SampleWindow {
    f_cnt_t sourceIn  = 0;   // first source frame this clip plays
    f_cnt_t sourceOut = 0;   // one past the last; sourceOut > sourceIn
};
```

`sourceOut` is exclusive and frames are the unit because the source is frames
(`include/Sample.h:92-93`). Ticks are the unit for everything on the timeline (`TimePos`). The
conversion is `Engine::framesPerTick(rate)` and it is the *only* place tempo enters the model —
which is what makes #597 a drop-in (see §2.4).

**`ClipEdits`** (new, `include/ClipEdits.h`) — everything else the wave adds, all defaults-neutral so
an untouched clip serialises as today:

```cpp
struct ClipEdits {
    float    gain         = 1.0f;    // linear; persisted as dB
    TimePos  fadeInTicks  = 0;
    TimePos  fadeOutTicks = 0;
    FadeShape fadeInShape  = FadeShape::Linear;   // Linear | Exponential | EqualPower
    FadeShape fadeOutShape = FadeShape::Linear;
};
```

Placement: `gain` and both fades belong on `Clip`, not `SampleClip`, because a MIDI clip can carry a
fade-out too and the base class already owns cross-type state (`include/Clip.h:167-180`). The window
belongs on `SampleClip`, next to `m_sample` (`include/SampleClip.h:101`).

**Take lanes** — `Lane` is a **child relationship, not a new track type**:

```cpp
class Lane {                 // include/Lane.h
    int m_index;                    // 0..n-1, stable within the track
    QString m_name;
    BoolModel m_muted, m_solo;      // reuse the existing per-clip model pattern
    // clips live in the owning Track's clipVector with a lane tag
};
```

and `Clip` gains `int laneIndex() const / setLaneIndex(int)` (default 0). Rationale: `Track` already
holds an unordered `clipVector` with no ordering contract (`include/Track.h:73`, `:227`), every clip
is created through `Track::createClip` (`src/tracks/SampleTrack.cpp:185-190`), and the drawing code
already does all vertical positioning through Qt layout rather than explicit `y()`
(`src/gui/tracks/TrackContentWidget.cpp:213`, `:264`). A lane tag keeps one clip list per track,
keeps `Track::addClip`/`removeClip` (`src/core/Track.cpp:341`, `:357`) unchanged, and makes
"visibility" a filter in the lane-aware content widget. The alternative — lanes as child tracks —
would double the track plumbing (`TrackView`, `TrackContainerView::realignTracks`
`src/gui/editors/TrackContainerView.cpp:258-267`) and put take lanes into the mixer, which is wrong.

**Comp** — the composite is a first-class object, not a merged buffer:

```cpp
struct CompRegion { TimePos begin, end; int laneIndex; TimePos sourceOffsetInLane; };
class CompClip : public Clip {           // one clip, N region references
    std::vector<CompRegion> m_regions;   // sorted, non-overlapping, contiguous
};
```

`CompClip::nodeName()` returns `"compclip"`; its regions serialise as child elements. It renders by
delegating each region's frames to the referenced lane clip's window — the source buffers stay
shared and untouched, which is what makes "the ability to change the source region later" (task
spec) and #600's acceptance ("source takes are never destructively edited") the same mechanism.

### 2.3 Invariants (each is a test)

1. **I1 — the read path never writes the model.** No function called from `Song::processNextBuffer`
   (`src/core/Song.cpp:207`) or any `PlayHandle::play` may mutate a `Clip`, its `Sample`, or
   `SampleBuffer::m_data`. Play handles take a **snapshot** of the window at construction.
2. **I2 — the buffer is immutable after load.** `shared_ptr<const SampleBuffer>` already enforces the
   type (`include/Sample.h:111`); nothing may `const_cast` it. File hash before/after any edit is the
   proof (acceptance (a)).
3. **I3 — every edit is a field.** Trim, slip, fade, crossfade and gain appear only as members of
   `ClipEdits`/`SampleWindow`; no editing operation writes samples and no editing operation touches
   the file on disk.
4. **I4 — the window is well-formed.** `0 <= sourceIn < sourceOut <= bufferFrames`, enforced in one
   setter; a violated clamp is a rejected edit, not a clamped one.
5. **I5 — timeline length and window length agree.** `(sourceOut - sourceIn)` frames equals the
   clip's tick length at its current mapping ± 1 tick. A trim changes both together, atomically, in
   one journalled operation.
6. **I6 — a comp region is gapless and ordered.** Regions sorted by `begin`, `region[i].end ==
   region[i+1].begin`, first `begin == 0`, last `end == clip length`.
7. **I7 — lane indices are stable** across save/load and across unrelated reorderings; a `CompRegion`
   naming a missing lane is a load error, not a silent default.
8. **I8 — realtime safety.** The capture path allocates nothing and locks nothing (the
   `RecordRingBufferTest` allocation probe pattern, `tests/src/core/RecordRingBufferTest.cpp:238`).
   Take-lane and comp state changes are queued to the control thread, per the task spec.
9. **I9 — serialisation is additive.** Every new attribute has a default that reproduces today's
   behaviour; a clip with no new attributes is byte-identical to one written today
   (`tests/src/core/SlideNotesTest.cpp:213-214` is the pattern).

### 2.4 How the window relates to the audio and to warp (#597)

The mapping the model exposes is *one* function, and it is the seam #597 attaches to:

```cpp
// Clip-level, replacing the arithmetic in src/tracks/SampleTrack.cpp:117-122
virtual f_cnt_t sourceFrameAt(TimePos timelinePos) const;   // linear today
virtual TimePos timelinePosAt(f_cnt_t sourceFrame) const;   // inverse, for trimming
```

Today's linear implementation is exactly the existing code relocated:
`sourceIn + framesPerTick(rate) * (timelinePos - startPosition - startTimeOffset)`, clamped to
`[sourceIn, sourceOut)`. #597 adds `WarpMarkers` as a child element and a second implementation of
the same two virtuals, keyed off the clip's source tempo. **Nothing else in the clip model changes
when warp lands** — which is the only reason it is worth freezing these two signatures now. Warp
markers are stored against the *source frame* they pin, so they are independent of trim: trimming
moves `sourceIn`/`sourceOut` and the markers stay where they are on the audio.

### 2.5 What makes it non-destructive

Formally, an edit is non-destructive here iff all of:

- **the source is shared, never copied** — `shared_ptr<const SampleBuffer>`
  (`include/Sample.h:111`, copy at `src/core/Sample.cpp:48`), so a split, a trim or a comp adds
  references and zero bytes;
- **the audio bytes are never written** — the only writers in the tree are the recorders
  (`SampleRecordHandle::writeBuffer` `src/core/SampleRecordHandle.cpp:131`, the new recorder's
  disk-writer thread `src/core/audio/TrackRecorder.cpp:162`) and they write *new* files;
- **every edit is an invertible field on the clip** — revert the field, the audio is bit-identical;
- **playback is a pure function of the model** — I1. The current code violates exactly this one:
  `src/tracks/SampleTrack.cpp:126-127` writes the window during playback, so a "trim" survives only
  until the next play pass, and two clips sharing a buffer cannot be trimmed independently.

Acceptance (a)'s "verified by file hash before/after" is satisfiable only once I1 holds, because
today a play pass is enough to change the clip's own state.

### 2.6 Project-file representation

Follow `SampleClip::saveSettings`'s shape — attributes on `<sampleclip>`, child elements for lists,
no new root, no new required attribute (`src/core/SampleClip.cpp:281-312`; serialiser entry
`src/core/Track.cpp:224`). Additive only, every attribute defaulting to today's behaviour (I9):

```xml
<sampleclip pos="0" len="192" muted="0" src="take1.wav" off="0" autoresize="0"
            srcin="4800" srcout="48000"        <!-- NEW: SampleWindow, frames; default 0 / buffer size -->
            gain="-3.0"                          <!-- NEW: dB, default 0.0 -->
            fadein="0" fadeout="96"              <!-- NEW: ticks, default 0 -->
            fadeinshape="0" fadeoutshape="0"     <!-- NEW: enum, default 0 = linear -->
            lane="1">                            <!-- NEW: take lane index, default 0 -->
    <!-- child elements a later slice may add here: <warp> (#597), <crossfade> -->
</sampleclip>
```

and the composite as its own element beside `sampleclip`, one region per child:

```xml
<compclip pos="96" len="384" muted="0" lane="0">
    <region begin="0"   end="192" lane="0" srcpos="0"/>
    <region begin="192" end="384" lane="2" srcpos="0"/>
</compclip>
```

Notes the implementer needs:

- **No `UPGRADE_METHODS` entry is required** for these additions. The version attribute is derived
  from the upgrade table (`src/core/DataFile.cpp:133`, `:73`) and an old build reading a clip with
  unknown attributes ignores them. A migration method is required only if an existing attribute's
  *meaning* changes — the design avoids that by keeping `len`/`off` authoritative and making
  `srcin`/`srcout` additional (I5 keeps them consistent on the write side).
- The `autoresize` attribute already exists (`src/core/SampleClip.cpp:295`) and is what stops
  `updateLength()` (`src/core/SampleClip.cpp:225-240`) from snapping a trimmed clip back to full
  length. **Every trim must persist `autoresize="0"`**, or the trim dies on the next load. That is a
  one-line dependency and it is the most likely silent regression in the whole wave.
- `loadSettings` order matters: `setSampleFile` resets the offset (`src/core/SampleClip.cpp:157`),
  and `setSampleFile` is called at `:324-329` *before* `changeLength`/`setStartTimeOffset` at
  `:341-343`. New window fields must be applied **after** `changeLength` and must not be routed
  through `setStartTimeOffset`.
- No entry in `ELEMENTS_WITH_RESOURCES` (`src/core/DataFile.cpp:67-70`) is needed: takes are written
  to files by the recorder and referenced by `src`, exactly like an imported sample.
- `compclip` is a new clip node name; `Track`'s load path keys off `nodeName()`
  (`src/core/Track.cpp:284-303`, node names `include/SampleClip.h:64-67`), so it must be registered
  there and in the clipboard path (`src/gui/clips/ClipView.cpp:526-573`, `createClipDataFiles`).

---

## 3. Per-feature decision table

`exists` = what is in the tree today, with the line that proves it. `change` = the smallest honest
delta. Risks are the *specific* ways this wave can fail, not generic ones.

| # | Feature | Exists today | Missing | Change | Risk | Test that proves it |
|---|---|---|---|---|---|---|
| 1 | **Trim** (window start/end on the source) | Window fields `Sample`::startFrame/endFrame (`include/Sample.h:92-93`, atomics `:112-113`); length-only resize with `setAutoResize(false)` (`src/gui/clips/ClipView.cpp:903-923`); the window is *computed at playback* and written back (`src/tracks/SampleTrack.cpp:117-127`) | Authored, persisted window; a trim gesture; right-edge semantics that move `sourceOut` instead of re-deriving it | `SampleWindow` on `SampleClip` (§2.2); `SampleTrack::play` becomes read-only; right-edge drag sets `sourceOut`; persist `srcin`/`srcout` + `autoresize="0"` | The playback rewrite (`src/tracks/SampleTrack.cpp:126-127`) silently re-derives the window on the next play pass, so the trim appears to work and then reverts; a second clip sharing the buffer is affected by the first | Clip A trimmed, clip B (clone, same buffer) untrimmed → both render their own windows; byte-hash of the source file unchanged; round-trip after `saveProjectFile` |
| 2 | **Slip** (offset into the source) | Fully present: `m_startTimeOffset` (`include/Clip.h:172`), accessors (`:141-142`), left-edge drag adding the delta (`src/gui/clips/ClipView.cpp:965-987`), persisted as `off` (`src/core/SampleClip.cpp:294`), drawn (`src/gui/clips/SampleClipView.cpp:304`), repaired on reverse (`:381`) | Invariance: it is reset by `setSampleFile` (`src/core/SampleClip.cpp:157`), `updateLength` (`:236`) and `tempoChanged` (`:245`) | Move the offset into `SampleWindow`'s timeline mapping so slip is a model field, not a derived one; keep the `off` attribute for compatibility | A tempo change rewrites the slip (`src/core/SampleClip.cpp:243-248`) — the existing behaviour is "slip is a tempo artifact". Changing that changes existing projects' sound (forbidden by AGENTS.md rule 5) | Slip set, tempo changed, slip preserved and audio position unchanged; existing project files byte-identical on re-save (I9) |
| 3 | **Fade in / out** | Nothing. Grep for `fade` in `src/`+`include/` returns only `FadeButton` (`src/gui/tracks/FadeButton.cpp:2`) and note-attack code (`src/core/Instrument.cpp:149-174`) | Curve model, storage, audio application, drawing, handles | `ClipEdits` fades (§2.2); apply in the play handle, **not** in `Sample::render` (that function is shared by previews and the metronome — `src/core/SamplePlayHandle.cpp:106-108`); draw in `SampleClipView::paintEvent` (`src/gui/clips/SampleClipView.cpp:231-373`) | Applying the ramp in `Sample::render` (`src/core/Sample.cpp:136-176`) would fade metronome clicks and browser previews; a per-period ramp computed with `std::pow`/`std::exp` per frame is an audio-thread cost | Fade-out of N ticks produces a monotonic RMS envelope over exactly N ticks (`mcp__lmms_lab__lmms_measure` over the window); preview path unaffected (negative control); allocation probe on the render path |
| 4 | **Crossfade between adjacent clips** | Nothing | Overlap semantics, the curve, drawing the overlap | No new audio primitive: a crossfade is *the outgoing clip's `fadeOut` + the incoming clip's `fadeIn` over the same tick range*, using equal-power shapes; the GUI pairs them when two clips on one lane overlap | Turning a crossfade into a "fade region" object is over-modelling; but *without* a link, dragging one clip silently breaks the crossfade. Decide the link (see OQ-6) | Two clips overlapping by N ticks sum to unity gain within ±0.1 dB at mid-overlap; no click at the junction (sample-difference test across the boundary) |
| 5 | **Clip gain** | The carrier exists and is applied: `Sample::m_amplification` (`include/Sample.h:116`) multiplied per frame at `src/core/Sample.cpp:170`; it already affects the waveform drawing (`src/gui/clips/SampleClipView.cpp:316`) | A model, a UI, persistence, and a per-clip default | Wire a `FloatModel` to `SampleClip`'s `Sample`; persist `gain` in dB (§2.6); add a drag/extended context menu on the clip | `Sample::m_amplification` is a plain atomic read per frame (fine), but a `FloatModel` write from the GUI thread onto an object the audio thread reads needs the same care as `Clip::changeLength`'s `requestChangeInModel` (`src/core/Clip.cpp:120-122`); also: gain must be **per clip**, and today `Sample` is owned per `SampleClip` (`include/SampleClip.h:101`) so this happens to hold — a future shared-`Sample` optimisation would silently break it | Gain −6 dB halves amplitude and the file hash is unchanged; two clips sharing one buffer keep independent gain |
| 6 | **Take lanes** | Nothing (0 grep hits, §1.8). Nearest thing is the 2-stream `MultiTrackRecorder` (`include/MultiTrackRecorder.h:55`) | Lane container, lane tags, ordering, mute/solo per lane, vertical allocation, recorder→lane wiring, lane persistence | `Lane` + `Clip::laneIndex` (§2.2); lane-aware drawing in `TrackContentWidget` (`src/gui/tracks/TrackContentWidget.cpp:213`, `:264`); recorder writes a lane-tagged take; persist `lane` | Vertical space: today clip height is the whole content height minus one (`src/gui/tracks/TrackContentWidget.cpp:213`) and track stacking is a `QVBoxLayout` (`src/gui/editors/TrackContainerView.cpp:103-106`) — lane-aware geometry means touching layout code that has no test coverage (`src/gui` is at 0.22% per `docs/CONVENTIONS.md:98-101`) | Record 4 takes to 4 lanes → 4 clips on 4 indices, each playable alone; save/reload preserves lane index and order; mute lane *k* removes exactly lane *k* from the mix |
| 7 | **Comping** (composite selection) | Nothing | Region model, region editing, source substitution, audition, persistence | `CompClip` + `CompRegion` (§2.2); region resolution at playback composes the referenced windows; #600 owns the region-editing UI | The comp must not copy audio (that would make it destructive and make "change the source region later" impossible); audition must not restart the take from zero each region; and a comp whose lane is later re-recorded must keep pointing at the old take (I7) | 4 takes → comp of 2 regions → reload → **identical composite** (waveform/spectral compare, not just a field compare); source takes' file hashes unchanged; per-region audition plays the right take |
| 8 | **Punch in / out** | Nothing. `Timeline` has loop state only (`include/Timeline.h:104-107`); the time line's action set has no punch entry (`include/TimeLineWidget.h:186-194`); capture has no position input at all (`include/TrackRecorder.h:92-94`) | Punch range model + UI, a transport-gated capture window, pre-roll, and the frame-count guarantee | `punchBegin`/`punchEnd`/`punchEnabled` on `Timeline`; capture gated by transport ticks where the engine feeds the recorder (`src/core/AudioEngine.cpp:338` is the single call site) and by `arm()` writing the take's start frame; the recorder needs a frame offset, not a new API | The capture path has no position: `frames` is the only temporal input (`src/core/audio/TrackRecorder.cpp:130-144`). Gating in `AudioEngine` means the gate must not allocate (the existing feed is already lock-free per `src/core/AudioEngine.cpp:333-339`); gating must also be robust to a period that straddles the punch boundary (partial block) | Punch over N ticks at 48 kHz captures exactly the expected frame count ± one period, asserted on `TrackRecorder::framesRecorded()` (`include/TrackRecorder.h:99`); a take armed outside the region captures 0 frames |
| 9 | **Arbitrary input count** | Hardcoded 2: `NumTracks = 2` (`include/MultiTrackRecorder.h:55`), mapping (`src/core/audio/MultiTrackRecorder.cpp:36-41`), channel clamp to `DEFAULT_CHANNELS` (`src/core/audio/TrackRecorder.cpp:57`, `include/lmms_constants.h:38`), ALSA widget pinned to 2 (`src/gui/AudioAlsaSetupWidget.cpp:74`) | A runtime count, routing from N inputs to N recorders, an input selector UI, and a backend that actually delivers N channels | Runtime count in `MultiTrackRecorder` (bounded max, or a dynamic vector sized off the audio thread); input selection models per the KB mission's `AudioInputDevice` plan; **and a capture path in the backend that is the default on Linux** (`src/core/audio/AudioAlsa.cpp:54` is playback-only) | `pushInputFrames` is called from the capture thread and **allocates + locks** (`src/core/AudioEngine.cpp:175`, `:184`, `:197`) — generalising the count multiplies that violation; the interleaved input buffer is `SampleFrame` (stereo) so >2 channels need a wider frame or an N-buffer layout, which touches `pushInputFrames` and both backends (`AudioJack.cpp:432`, `AudioSdl.cpp:187`) | The existing offline harness extended to 8 inputs: `tests/src/core/TwoTrackRecordingHarness.cpp:221` feeds `MultiTrackRecorder::processInput` from a stand-in audio thread and verifies the WAVs — same harness, 8 files, distinct synthetic tone per input, zero overflow |
| 10 | **Input monitoring** | Nothing (§1.8). PDC can report latency for chains/graphs (`docs/STATUS.md:17`) | A monitor path: input frames → the track's audio bus, per-input on/off, with the device latency declared to the PDC graph | A monitor `PlayHandle` fed from `AudioEngine::inputBuffer()` (`include/AudioEngine.h:239-241`) into the armed track's bus handle (`SampleTrack` already owns one: `src/tracks/SampleTrack.cpp:52`, and it is handed to play handles at `src/core/SamplePlayHandle.cpp:62`); a per-track monitor model; latency reported to `LatencyCompensation` | Feedback loop (monitor → master out → input) is a user-facing hazard the design must default-off; and PDC compensates *plugin* latency, so a capture-latency term may not have a home in the graph — **UNVERIFIED, see OQ-8** | Monitor on: a synthetic input appears at the track's bus output within a stated latency; monitor off: exactly silence; the reported round-trip latency matches the PDC figure (acceptance (e)) |

**MIDI, explicitly.** Trim and slip already exist for MIDI clips through the same `startTimeOffset`
(`src/gui/clips/MidiClipView.cpp:925-930` on split; boundary drawing at
`src/gui/clips/MidiClipView.cpp:380-381`, `src/gui/PianoRoll.cpp:903-904`) — the model work above must
not regress it. **Fades, crossfades and clip gain are audio-only in this wave**: a MIDI clip's
analogue is note velocity/expression, which is a different edit and is not scoped. **Take lanes and
comping are type-agnostic at the container level** (the per-lane document says "audio and MIDI",
#600 spec) but this wave builds the *audio* comp path; a MIDI comp needs note-level merging across
takes, which is **not in this wave**. **Punch for MIDI** is a different mechanism (gating
`MidiClient` events, not a WAV writer) and is **not in this wave**. **Input count and monitoring are
audio-only.**

---

## 4. Sequencing

### 4.1 What must land first, and why

**Slice 0 — the read-only playback path and the clip window. This gates everything else.**
Nothing in trim, slip, fades or comping can hold while `SampleTrack::play` rewrites the clip's
window on every play pass (`src/tracks/SampleTrack.cpp:126-127`) and `updateLength`/`tempoChanged`
reset the length and slip (`src/core/SampleClip.cpp:235-236`, `:245`). Every editing lane's first
bug report would otherwise be "my trim reverted", and it would be true. #597 also attaches here
(§2.4), so this slice is the *interface* freeze.

Files: `include/Clip.h`, `include/SampleClip.h`, `include/SampleWindow.h` (new),
`include/SamplePlayHandle.h`, `src/core/SampleClip.cpp`, `src/tracks/SampleTrack.cpp`,
`src/core/SamplePlayHandle.cpp`, `tests/src/tracks/SampleClipWindowTest.cpp` (new).

**Slice 1 — serialisation, immediately after Slice 0 (may overlap; it is a different file region).**
The moment a window exists it must survive a save, which means `src/core/SampleClip.cpp:281-356` plus
the `autoresize="0"` dependency (§2.6). Until this lands, every other slice's manual testing is
thrown away on reload.

Files: `src/core/SampleClip.cpp`, `tests/src/core/SlideNotesTest.cpp`-style round-trip test (new
file: `tests/src/core/ClipSerialisationTest.cpp`).

### 4.2 Parallel tracks after Slice 0

| Track | Slices | Files | Depends on |
|---|---|---|---|
| A (editing) | Fades + crossfade + gain | `include/ClipEdits.h` (new), `src/core/ClipEdits.cpp` (new), `src/core/SamplePlayHandle.cpp`, `src/gui/clips/ClipView.cpp`, `src/gui/clips/SampleClipView.cpp`, `tests/src/core/ClipEditsTest.cpp` (new) | Slice 0 (window), Slice 1 (gain/fade persistence) |
| B (capture) | Punch in/out | `include/Timeline.h`, `src/core/Timeline.cpp`, `src/gui/editors/TimeLineWidget.cpp`, `include/TrackRecorder.h`, `src/core/audio/TrackRecorder.cpp`, `src/core/AudioEngine.cpp`, `tests/src/core/RecordRingBufferTest.cpp`/`MultiTrackRecorderTest.cpp` (extend) | nothing in A; **independent of Slice 0** |
| C (capture) | Arbitrary input count | `include/MultiTrackRecorder.h`, `src/core/audio/MultiTrackRecorder.cpp`, `src/core/audio/TrackRecorder.cpp`, `src/core/AudioEngine.cpp` + `include/AudioEngine.h` (input buffer layout), `src/core/audio/AudioJack.cpp`, `src/core/audio/AudioSdl.cpp`, `src/gui/SetupDialog*`, ALSA/PortAudio/SDL setup widgets | **B and C touch `TrackRecorder`/`MultiTrackRecorder`/`AudioEngine` — do not run them as two lanes without a file-set split.** B owns `TrackRecorder`, C owns `MultiTrackRecorder` + the input buffer |
| D (lanes) | Take lanes | `include/Lane.h` (new), `src/core/Lane.cpp` (new), `include/Track.h`, `src/core/Track.cpp`, `src/gui/tracks/TrackContentWidget.cpp`, `src/gui/tracks/TrackView.cpp`, `src/core/SampleClip.cpp` (`lane` attr) | Slice 1 (persistence), and benefits from B/C (a take needs a bounded capture) |
| E (monitoring) | Input monitoring | `include/InputMonitorPlayHandle.h` (new), `src/core/InputMonitorPlayHandle.cpp` (new), `src/core/AudioEngine.cpp`, `include/SampleTrack.h`/`src/tracks/SampleTrack.cpp`, `include/LatencyCompensation.h` (see OQ-8), `src/gui/...` | C (a defined input) and the PDC interface answer |
| F (comp) | Comp model | `include/CompClip.h` (new), `src/core/CompClip.cpp` (new), `src/core/Track.cpp`, `src/gui/clips/ClipView.cpp` (clipboard path `:526-573`) | D (lanes must exist); the *UI* is #600's |

**Collision hotspots** (the parent should check lanes against these): `src/core/SampleClip.cpp`
(Slices 0, 1, 6), `src/core/audio/TrackRecorder.cpp` + `include/MultiTrackRecorder.h`
(multiple capture slices), `src/core/AudioEngine.cpp` (the single recorder feed at `:338` and the
input buffer at `:389-393`), `src/gui/clips/ClipView.cpp` (every editing gesture), `tests/fork-sources.txt`
(all new sources must be added there — acceptance (g)).

### 4.3 What this unblocks

- **#597 warp** — unblocked by Slice 0 alone, provided the two mapping virtuals (§2.4) are frozen in
  it. Warp then adds `<warp>` as a child element and a non-linear mapping; no clip-model change.
- **#600 comping** — unblocked by D + F (lanes plus the comp object). The region-editing UI stays
  #600's.
- **#598 Session View drag-drop** — needs *nothing* from the clip model. The drop half already exists
  in this tree: `SampleClipView` accepts `"samplefile,sampledata"` (`src/gui/clips/SampleClipView.cpp:133-134`,
  `:147-158`) and `FileBrowser` produces it (`src/gui/FileBrowser.cpp:893` **[sweep]**); clips drag as
  `"clip_<type>"` with a `DataFile` payload (`src/gui/clips/ClipView.cpp:446-452`, `:830-832`).
  #611's contribution is only that a dragged clip must carry its window/gain/fades in that payload —
  which is what `Clip::copyStateTo` already does via save/restore state
  (`src/core/Clip.cpp:162-177`). **Say this plainly to the parent: #598's dependency on this wave is
  one line of payload, not a slice.**

---

## 5. The honest size

### 5.1 Estimated lines per slice

Estimates, not measurements — **UNVERIFIED** until built (a build was forbidden; five sibling lanes
are compiling).

| Slice | New/modified files | Est. insertions | Notes |
|---|---|---|---|
| 0 Window + read-only playback | 8 | 350–450 | `SampleTrack::play` (`:112-141`) is replaced, not extended |
| 1 Serialisation | 2–3 | 120–180 | plus the `autoresize="0"` ordering fix |
| 2 Fades / crossfade / gain | 6–7 | 550–750 | two of these are GUI (drawing + handles) |
| 3 Punch in/out | 7 | 300–400 | gates the capture path, does not rewrite it |
| 4 Arbitrary input count | 8–10 | 400–600 | incl. input-buffer layout change and per-backend work |
| 5 Take lanes | 8–10 | 700–950 | the lane-aware geometry in `TrackContentWidget` is the expensive half |
| 6 Input monitoring | 6–8 | 400–600 | plus a PDC answer that may itself be work |
| 7 Comp model | 4–5 | 400–550 | model only; UI is #600 |
| **Total** | **~30 files, 12+ new** | **~3,200–4,500** | |

Plus, unavoidably: every new source in `tests/fork-sources.txt` (acceptance (g)), Gate 7's 500-line
file ratchet and Gate 4's CCN ≤ 10 against new files (`docs/CONVENTIONS.md:19-21`) — a 950-line
`TrackContentWidget` change in a file already over the wire will need `--reanchor` with a recorded
reason or a split.

### 5.2 Is it xl?

**No — as boarded it is larger than xl, by roughly 2–2.5×.** The comparison is in the program's own
units: PR #5 / task #594, the Session View *data layer only*, is "1,677 insertions across 15 files"
and is one boarded task (`docs/STATUS.md:30-33`). #611's estimate above is 2–2.5× that, and it
spans three subsystems with three different realtime contracts (clip model; capture engine;
monitoring path) plus a GUI half that the quality gates **cannot police** — `src/gui` sits at 0.22%
line coverage, 44 covered lines out of 19,820 (`docs/CONVENTIONS.md:98-101`).

Three specific reasons the naive reading ("it's just editing") understates it:

1. **Two of the six requirements are a recording engine, not editing.** Acceptance (d) "capture works
   at 8 inputs, not 2" and (e) "a monitored input is audible with the reported round-trip latency
   matching the PDC graph's figure" are engine work with hardware implications, and (f) "a killed
   process mid-record leaves a recoverable take" is crash recovery — a subsystem that does not exist
   in any form (`src/core/audio/TrackRecorder.cpp:106-125` closes a file; nothing records where a
   take was).
2. **The default Linux backend records nothing.** ALSA has no capture path
   (`src/core/audio/AudioAlsa.cpp:54`), so (c), (d) and (e) are untestable without JACK or SDL, and
   `docs/KNOWN-LIMITATIONS.md:70-74` already tells users so. Adding `snd_pcm_readi` is *not* listed in
   #611's scope, but without it the wave's headline acceptance criteria run only on a machine with a
   JACK server.
3. **`pushInputFrames` must be fixed before the input count can grow.** It allocates and locks on the
   capture thread (`src/core/AudioEngine.cpp:175`, `:184`, `:197`), and the interleaved frame type is
   2-channel `SampleFrame`. Generalising to N inputs multiplies both problems.

**Recommendation:** keep #611 as the parent wave, but board five children and give them to five
lanes (0+1 as one, 2, 3, 4, 5, 6, 7 as the sizing above; realistically 4–6 lanes with B/C and D/E
serialised on their shared files). Attempting it as one lane will produce a partly-wired tree that
compiles and does not record — the exact failure mode AGENTS.md rule 9 exists for.

### 5.3 What is explicitly NOT in this wave

- **Warp / time-stretch** — #597. This wave only freezes the mapping interface (§2.4).
- **Bounce-in-place, freeze, stem export, LUFS** — separate backlog items (`docs/STATUS.md:91-92`).
- **Session View UI, clip launch, scenes** — #598 / #594.
- **Comp region-editing UI** — #600 owns it; this wave owns the model.
- **ALSA capture** (`snd_pcm_readi` in `src/core/audio/AudioAlsa.cpp`) — see OQ-9; it gates the
  acceptance criteria on Linux but is not named in #611's scope. **Escalate this, do not assume.**
- **Recording crash recovery** (acceptance (f)) — a take-file naming convention and a recovery scan
  do not exist; this is new work and should be its own slice or child.
- **Instrument hosting, out-of-process hosting, plugin scanning** — other waves.
- **Undo/redo depth and drag coalescing** — the wave adds journalled operations
  (`src/core/Clip.cpp` `requestChangeInModel` pattern, `src/gui/clips/ClipView.cpp:1457`), but the
  known undo-depth gap (`docs/STATUS.md:247-250`) stays open.
- **MIDI comping, MIDI punch, MIDI clip gain** — stated in §3.

---

## 6. Open questions the implementation lane must answer before editing

Each names the file:line that will have to change. These are ordered by how early they block.

**OQ-1 — Does the window live on `Sample` or on `Clip`? (blocks Slice 0)**
Today it is `Sample::m_startFrame`/`m_endFrame` (`include/Sample.h:112-113`), reached through
`setSampleStartFrame`/`setSamplePlayLength` (`src/core/SampleClip.cpp:265-276`), and each `SampleClip`
owns its own `Sample` (`include/SampleClip.h:101`) so it *acts* per-clip. Two candidate answers:
keep it on `Sample` and make the play path read-only, or move the authored window onto `SampleClip`
and leave `Sample`'s fields as the render-time scratch they already are. **The second is the design
above**, because the first leaves two writers of one quantity. Files that change either way:
`src/tracks/SampleTrack.cpp:112-141`, `src/core/SampleClip.cpp:265-276`, `include/Sample.h:92-113`.

**OQ-2 — Where are fades applied, given `Sample::render` is shared with previews and the metronome?**
`src/core/SamplePlayHandle.cpp:106-108` explicitly notes this path is "previews, SampleTracks and the
metronome", and `Sample::render` (`src/core/Sample.cpp:136-176`) is the only per-frame loop. Applying
a fade there fades the metronome. Applying it in `SamplePlayHandle::play` (`src/core/SamplePlayHandle.cpp:80-115`)
needs the curve available with no allocation and must be a no-op for previews. **Decide, then state
which call sites are excluded.**

**OQ-3 — Who owns clip gain after the migration, and how is it written from the GUI thread?**
`Sample::setAmplification` (`include/Sample.h:105`) is the carrier and is already applied
(`src/core/Sample.cpp:170`), but nothing sets it in the clip path and it is not persisted
(`src/core/SampleClip.cpp:281-312`). The GUI write needs the `requestChangeInModel`/
`doneChangeInModel` discipline the tree uses for structural changes (`src/core/Clip.cpp:120-122`).

**OQ-4 — What is the contract for `autoresize` on a trimmed clip? (highest-probability regression)**
`src/core/SampleClip.cpp:225-240` resets length and slip when `getAutoResize()`, and
`src/core/SampleClip.cpp:243-248` recomputes the offset on every tempo change. If a trim does not
persist `autoresize="0"` the trim is lost on the next load; if it does, a tempo change no longer
re-fits the clip — which is *probably* right but changes existing behaviour and needs an explicit
decision (AGENTS.md rule 5: a migration that changes output is a FAIL).

**OQ-5 — How does a `CompRegion` identify its take across a save/load?**
`sampleclip` has no id attribute (`src/core/SampleClip.cpp:281-312`) and `Track` reloads clips in
document order into whatever container exists (`src/core/Track.cpp:284-303`, `:297-299`). A lane
index alone is not stable if lanes are reordered or a lane is deleted and re-recorded. **A stable
take id (new attribute on `sampleclip`) or a documented immutability rule is required before the comp
model is written.**

**OQ-6 — Is a crossfade a computed pair of fades or a first-class object?**
§3 row 4 proposes the pair. The cost is that dragging one of the two clips breaks the crossfade
silently. A `<crossfade>` child element linking two clip ids is more code and more state. **Decide,
because it determines whether `ClipEdits` needs a link field and whether the drag path
(`src/gui/clips/ClipView.cpp:844-855`) must look at neighbours.**

**OQ-7 — Punch gating: where, and how does a straddling period behave?**
`TrackRecorder::processInput` takes only `(input, frames)` — `include/TrackRecorder.h:92-94`,
`src/core/audio/TrackRecorder.cpp:130-144` — and has no transport position. The single engine call
site is `src/core/AudioEngine.cpp:338`. Either the gate is applied there (a subrange of `frames`,
which must allocate nothing and must be exact at the boundary) or `TrackRecorder` gains a start-frame
parameter. **The partial-block case is the acceptance test** (c) hangs on: "captures exactly the
armed region, with a test that asserts the captured frame count".

**OQ-8 — Does the PDC graph have a term for capture/monitor latency? (UNVERIFIED)**
Acceptance (e) requires the monitored input's reported round-trip latency to "match the PDC graph's
figure", and `docs/STATUS.md:17` says PDC covers chain/graph latency from what each *effect* reports
(`src/core/LatencyCompensation.cpp`). A capture device's latency is not an effect's latency. **Somebody
must read `include/LatencyCompensation.h` and answer whether an input-monitor latency can be
expressed in that graph or needs a new term.** I did not read it; this is a gap in this document.

**OQ-9 — Is adding an ALSA capture path in scope?**
`src/core/audio/AudioAlsa.cpp:54` opens playback only. Without `snd_pcm_readi`,
`docs/KNOWN-LIMITATIONS.md:70-74` stands and acceptance (c)/(d)/(e) can only be demonstrated under
JACK/SDL. This is a scope decision for the parent, not a design one — **escalate.**

**OQ-10 — Where do takes live on disk, and what makes one recoverable?**
`TrackRecorder::arm` writes to a caller-supplied `filePath` (`src/core/audio/TrackRecorder.cpp:72-91`;
`include/TrackRecorder.h:81-87`). There is no naming convention, no take directory, no manifest, and
`disarm()` deletes nothing either (`src/core/audio/TrackRecorder.cpp:106-125`). Acceptance (f)
("a killed process mid-record leaves a recoverable take") needs a convention *and* an on-startup
scan. **Nothing here exists.**

**OQ-11 — Does the arbitrary-input-count change fit `SampleFrame`, or does the input buffer need a
new type?**
`m_inputBuffer` is `SampleFrame*` (`include/AudioEngine.h:389`) — 2 channels
(`include/lmms_constants.h:38`). N inputs need either a wider frame type or N buffers, and both
change `pushInputFrames` (`src/core/AudioEngine.cpp:173-198`) and its two callers
(`src/core/audio/AudioJack.cpp:432`, `src/core/audio/AudioSdl.cpp:187`). **Decide the layout before
writing the N-input slice; it is not a local change.**

**OQ-12 — What is the canonical unit for the source window, frames or ticks?**
`TimePos` is the house time type on clips (`include/Clip.h:170-172`) and frames are the unit of the
source (`include/Sample.h:92-93`). Mixing them is where the tempo dependency that #597 will break
lives. The design above stores **frames** and converts through `Engine::framesPerTick()` at the
mapping boundary (§2.4) — **confirm that is consistent with what #597's lane intends, or freeze the
other choice now.**

**OQ-13 — Is `TwoTrackAlsaCaptureProbe.cpp` in the build?** It has a `main()`
(`tests/src/core/TwoTrackAlsaCaptureProbe.cpp:76`) and is referenced only by the no-tautology gate
(`tests/no-tautology-gate.sh:34`, `:71`) — it is **not** registered in `tests/CMakeLists.txt`
(which lists `MultiTrackRecorderTest.cpp:15`, `RecordRingBufferTest.cpp:18`,
`TwoTrackRecordingHarness.cpp:28`). Whatever the answer, the 8-input acceptance test should extend
`TwoTrackRecordingHarness` (`tests/src/core/TwoTrackRecordingHarness.cpp:221`, wired through
`MultiTrackRecorder::processInput` at `:31`), not the probe.

---

## Appendix — evidence index

**Read directly for this document:** `include/Clip.h`, `src/core/Clip.cpp`, `include/SampleClip.h`,
`src/core/SampleClip.cpp`, `include/Sample.h`, `src/core/Sample.cpp`, `include/SampleBuffer.h`,
`src/tracks/SampleTrack.cpp`, `src/core/SamplePlayHandle.cpp`, `src/core/SampleRecordHandle.cpp`,
`include/ClipView.h`, `src/gui/clips/ClipView.cpp:790-1023, 1443-1474`,
`src/gui/clips/SampleClipView.cpp`, `src/core/audio/TrackRecorder.cpp`, `include/TrackRecorder.h`,
`include/RecordRingBuffer.h`, `src/core/audio/MultiTrackRecorder.cpp`, `include/MultiTrackRecorder.h`,
`src/core/AudioEngine.cpp:173-199, 300-355`, `include/AudioDevice.h:50-105`,
`src/core/audio/AudioDevice.cpp:34`, `include/Timeline.h:98-112`,
`src/gui/editors/SongEditor.cpp:95-116`, `src/gui/editors/TimeLineWidget.cpp:231-255`,
`src/gui/AudioAlsaSetupWidget.cpp:74`, `src/core/audio/AudioSdl.cpp` (capture lines),
`include/Song.h`, `src/core/Song.cpp:300-352, 1215-1246`, `src/core/Track.cpp:211-303`,
`include/Track.h`, `src/core/DataFile.cpp:67-70, 127-151, 2087-2205`, `docs/STATUS.md`,
`docs/CONVENTIONS.md`, `docs/KNOWN-LIMITATIONS.md`, `tests/src/core/RecordRingBufferTest.cpp:236-258`,
`tests/CMakeLists.txt`, `tests/fork-sources.txt`, `POST-ALPHA-PLAN.md`,
`specs/SPEC-two-track-recording.md`, `RECORDING-PROTOTYPE.md:836-860, 736-746`, KB articles
`lmms-recording-mission` (read) and `lmms-complete-daw-program` (search hit), board specs for
#611/#597/#598/#600.

**Marked `[sweep]` (gathered by a parallel evidence pass, not re-read here):** `AudioJack.cpp:425`
and its `jack_port_get_buffer`; PortAudio's discarded input; `AudioSampleRecorder.cpp:87-97` and its
absence of callers; the `src/gui` absence of an input-channel selector; `FileBrowser.cpp:893`;
`TimeLineWidget.h` action enum; `SongEditor.h:137-138`; the zero-hit greps for `lane`/`comp`;
`SessionModel` absence; the `ableton-gap/*`, `REPORT.md`, `findings-gap-analysis.md` and
`BACKLOG.md`/`DAW-GAP-ANALYSIS.md` quotations; `SlideNotesTest.cpp` round-trip details.

**UNVERIFIED (no source read; treated as questions in §6):** whether `LatencyCompensation` can carry
a capture-latency term (OQ-8); whether an ALSA capture path is in #611's scope (OQ-9, a scope
question); the line-count estimates in §5.1 (no build permitted).
