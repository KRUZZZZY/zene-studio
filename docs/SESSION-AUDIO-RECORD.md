# Recording audio into a launcher cell (WF-055, 0.5.0 L02 `launcher-rec`)

The MIDI half of WF-055 already existed: `session.capture_to_slot` puts what was just played (the MIDI
retro-capture window) into a new pattern a cell plays. This is the AUDIO half: an audio (sample) track's
cell records forward, the way a launcher records - arm, start on the launch quantisation, stop on the next
quantised stop or after a length, and the take becomes the cell's clip.

## The surface

| id | what it does | A16 class |
|---|---|---|
| `session.record_slot` | arms an EMPTY cell of a sample track's column and launches it (`quantisation`, else the cell's own, else the session's). `bars` (1-64) or `steps` (1-1024, a sixteenth note - MIDI Clock's own beat; one of the two, not both) fixes the length; without either the take runs until the cell stops. `clock_sync: true` (Q050-277) makes the MIDI clock master send START at the take's start tick and STOP at its end, with exactly the span's pulses between (refused with `refused` while `clock.master_set` has not enabled the master and a destination). `cancel: true` drops the take in hand and stops its cell. | not_mutating (a launch) |
| `session.record_slot_status` | `state` (`idle`, `armed`, `recording`, `finishing`, `done`), the cell, `start_tick`, `frames`, `target_frames`, `seconds`, `dropped`, `sample_rate`, `latency_frames`, `clock_sync` and `clock` (`active`, `start_tick`, `stop_tick`, `length_ticks`, `pulses`, `starts`, `stops`: what the clock master sent for the take) | not_mutating |
| `session.record_slot_land` | writes the finished take as a 24-bit WAV (`file`, absolute; a new take in the user's `Recordings` folder when left out) and makes it the cell's audio clip (`name`, else the file's name). Refused until the state is `done`. | true_inverse: one undo step restores the `<session>` block; the WAV stays on disk |

A cell stops recording on whatever stops the cell: `session.stop_slot` (quantised by the cell's own, else
the session's quantisation), another cell launched on the same column, `session.stop_all` /
`session.back_to_arrangement`, or the fixed length (the scheduler gives the cell a stop at
`start + bars` the moment it starts).

## The engine half (`include/SessionAudioRecorder.h`)

One take at a time, owned by the session scheduler (`SessionScheduler::audioRecorder()`):

- **Fed from the launch engine.** `afterLaunchEvents()` tells the recorder when the armed cell STARTED (on
  its grid line) and every path that ends a cell - its own stop, `takeColumnOver()`, the reset - tells it
  when it STOPPED. Nothing else decides when a take starts or ends, so a recorded cell starts on exactly
  the line a played cell would.
- **Fed with the engine's stereo input**, once per period, from `Song::processNextBuffer()` right after
  the scheduler has run - the same frames `SampleRecordHandle` records into an arrangement clip. While
  nothing records this is one atomic load.
- **Stored by the clip recorder's own machinery**, `SampleRecordAccumulator`: a pre-allocated ring (2^18
  frames) the audio thread writes and a drain thread empties into the take, so the audio thread never
  allocates. `arm()` makes the ring and the thread on the control thread.
- **Aligned like an arrangement take (R2.2).** The first `AudioEngine::recordingLatencyFrames()` input
  frames after the start are skipped (the round trip), and the take then runs on past the stop until it
  is exactly as long as the span it was recorded for: `(stop tick - start tick) x frames per tick`, or
  the fixed length. A take recorded for one bar is one bar of frames. A period before the capture has
  delivered anything counts against the skipped head, the rule `SampleRecordHandle` keeps (BUGS_FOUND
  11.29).

**Granularity, stated plainly.** The session scheduler decides launches once per period (it always has:
`SessionScheduler::processAudio`), so a take starts with the period in which its grid line is reached, as
the session's own playback of a cell does. Its LENGTH is exact (whole frames of the musical span); its
START is the period boundary, not the sub-period frame of the line.

## Playback - a launched audio cell sounds (`include/SessionAudioCells.h`)

Before this lane a cell could HOLD an audio file (`session.set_slot type: audio`, the grid's "Audio File..." and
drops) but launching it only took its column over: `publishSessionSlotContent` published "nothing to render" for
every audio cell (board card #597 left it to "a later slice"). A landed take is only "ready to launch" if a
launched audio cell plays, so that slice is here:

- **Publishing** (model thread, every launch path): an audio cell's file is loaded once - and converted to the
  engine's rate there if it differs - and the content table carries a pointer to the clip beside the pattern id.
  A missing or unreadable file publishes no clip (the cell stays silent, as before, and no warning box opens).
- **Rendering**: one persistent `SessionCellPlayHandle` per sample track that holds an audio cell, made on the
  model thread when such a cell is first published, on the track's own bus - so the clip goes through the track's
  devices, fader, pan and mixer channel. Each period it reads the scheduler's view of its column; on a new launch
  (a different clip, scene or start line) it starts from the scheduler's position (the clip's top, its loop start,
  or a legato phase), then copies frames with wrap-around: no allocation, no lock, no resampler on the audio thread.
  Like the input monitor's handle it survives the song-stop clear (`PlayHandle::Type::SessionCellHandle`), and the
  track's destructor removes it.
- **Instrument columns (Q050-013)**: the same handle is made for an INSTRUMENT track's column; the clip bypasses the
  instrument and goes to the track's bus, so fader, pan, mixer channel and effects apply. `InstrumentTrack`'s
  destructor removes it.
- **Stored settings (Q050-013)**: `gain_db` and the loop region (`loop_start` / `loop_length`, in ticks, turned into
  frames of the clip at publish) are applied by the renderer. `transpose`, `detune` (resampling, so duration follows
  pitch as on a sampler) and the tempo warp (`warp: true`, `warp_bpm`: the clip's own tempo; pitch-preserving WSOLA
  to the song's tempo) are baked ONCE on the model thread into a variant of the clip, held per setting with the
  file (cap 256 variants), so the audio thread still only copies frames. Limits: the warp follows the song tempo as
  of the last publish (every launch publishes; a tempo change under a cell that is already playing takes effect on
  its next launch), and WSOLA only tracks periods shorter than 128 frames, as `AudioStretcher.h` states.
- **Lifetime**: the clips of the open project live until the project is closed; `Song::clearProject` retires them
  and frees the ones retired at the previous close, by which time the scheduler's reset has dropped every
  reference the audio thread had.

Proof: `tests/src/core/SessionAudioCellPlaybackTest.cpp` (ctest `SessionAudioCellPlaybackTest`) launches a file
cell and a recorded-and-landed cell through `session.launch_slot` and checks the engine's OUTPUT is the clip frame
for frame, twice round (from the top and looping); built without this path (the recording step alone) both cases
render silence. Its third case is the allocation probe on `SessionCellPlayHandle::play`.

## Interface - the launcher grid (`src/gui/SessionGridMenus.cpp`, `src/gui/SessionGridView.cpp`)

An empty cell's right-click menu offers **Record Audio Into Cell** (until the cell stops), **Record Audio for
Bars...** (a form with `bars`), **Record Audio for Steps...**, **Record Bars with MIDI Clock Start/Stop...**, **Record Steps with MIDI Clock Start/Stop...** (the same forms with `clock_sync` on) and **Cancel Recording** - each declares `controlCommand` `session.record_slot`.
While a take is in hand the cell reads `● Rec` (recording or running on to its length) or `○ Rec` (armed, waiting
for its launch), and its accessible name ends ", recording" / ", armed to record". The column's stop button ends
it (`session.stop_slot`, quantised). The grid polls `session.record_slot_status` with its own refresh, and when a
take that ITS menu started is `done` it runs `session.record_slot_land` itself (a new take in the user's
`samples/Recordings` folder) and says so in its status line; a take started on the socket is left for its owner.
Proof: `tests/src/gui/SessionGridRecordTest.cpp` (ctest `SessionGridRecordTest`).

## Proof

- `tests/src/core/SessionAudioRecordTest.cpp` (ctest `SessionAudioRecordTest`): on the Dummy device with
  the test driving the periods and feeding a known input (frame n holds a value only n has), through the
  agent surface: the take starts on a bar line in the period that reached it; its PCM is the input from
  one round trip after that period, frame for frame, for exactly one bar (`bars: 1`) or exactly the whole
  bars up to a quantised `session.stop_slot`; the landed WAV holds that PCM (within two steps of the
  24-bit file's own quantisation); one `control.undo` empties the cell and nothing else, `control.redo`
  lands it again; the refusals (an instrument column, a column with no track, a filled cell, a second
  take while one is in hand, landing before a take is finished) are typed. Built on the base tree the
  same binary fails all three cases (`no command 'session.record_slot'`).
- `tests/src/core/SessionAudioRecorderTest.cpp` (ctest `SessionAudioRecorderTest`): the class without an
  engine - the round trip, the run-on to the span, a fixed length and an earlier stop, events for another
  cell, the pre-capture period, the cut to the span - and **the allocation probe**: `push()`,
  `slotStarted()` and `slotStopped()` allocate nothing in any state.

## External-synth capture (Q050-277)

A clock-synced take (`clock_sync: true`) opens a *capture span* on the MIDI clock master (`MidiClock::captureStarted`),
from the scheduler's launch event: START goes out carrying the take's start tick and the master pulses on its own time
(the song's transport may be stopped, and its edges are not sent while the span is open); the cell's stop (`bars`/`steps`
length, `session.stop_slot`, another cell taking the column, a reset) ends it (`captureStopped`): the pulses owed by the
span (`length / 2` ticks per pulse) are topped up, STOP goes out carrying the stop tick, and the transport is reported
afresh from the next period (a running transport then sends its own SPP/CONTINUE). A cancel mid-span sends STOP
(`captureAbort`). Proof: `SessionAudioRecordTest` (START/STOP ticks, pulse count, message order, nothing after STOP, a
cancel, the refusals, save and reload), `SessionGridRecordTest` (the menu entry) and `ControlClockCommands`.
