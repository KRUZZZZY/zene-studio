# Zene Studio 0.3.0-alpha — release notes (in progress)

**An engine-and-agent release.** Everything that can exist in the backend and be driven through the
control surface is folded in; the interface stays deliberately minimal. The release's own promise, in one
sentence: **everything is operable through `--control-socket` and the MCP bridge; almost nothing is
operable from the interface.** Read `docs/KNOWN-LIMITATIONS.md` before you install — where a capability
has no interface, that page says so rather than leaving you to find out.

> **Status of this file.** 0.3.0-alpha is in preparation; this file is being written as the release's
> capabilities land, one section per merged lane, so that the notes and the build agree when the tag is
> cut. Each capability claim below names the engine change, the control-surface command group and the
> test that proves it — the rule this project holds every release to. Anything not yet verifiable is
> marked, not asserted.

## The shape of 0.3.0

0.3.0 is an **engine-and-agent release, not a UI release**. Everything that can exist in the backend and be
driven through the control surface is folded in; the interface stays deliberately minimal; and the taste work
(stock devices, factory content, the design system) plus every UX/UI item is deferred. Stated as one line:

> **Everything is operable through `--control-socket` and the MCP bridge; almost nothing is operable from the
> interface.**

## Warp marker editing (`warp.*`) — added 2026-09-13

- **New: the warp map has an editing surface.** `warp.list`, `warp.add`, `warp.move`, `warp.remove` and
  `warp.set` are registered control-surface commands, so an agent can pin a frame of a sample clip's audio to a
  timeline position, move and delete markers, replace the whole marker list, and choose the clip's warp tempo
  mode (`follow` the project, or `source` — lead with the tempo the clip was recorded at) and its declared
  source tempo in BPM. Each mutating call records its SPEC A16 reversibility class (`true_inverse`), how it is
  reversed (the clip's own ProjectJournal checkpoint) and its before-state, so one `control.undo` takes the
  edit back.
- **The engine half already shipped in 0.2.1.** The markers, the monotonic map, the `<warp>` element and the
  measured render are #597's and are unchanged by this addition; see `docs/WARP.md` for the implementing lane's
  report and its numbers. Nothing about the stretching was touched here.
- **UI absence — one line: warp marker editing is drivable through the socket, not from the interface.**
  There is no marker handle, no drag, no grid snap and no groove template; nothing in the interface can place
  the first marker. `docs/KNOWN-LIMITATIONS.md` carries the same sentence.
- **Still true, and still not fixed:** the stretch is resampling, so a warp changes pitch (2× is an octave
  up); there is no pitch-preserving time-stretch.

## Export: TPDF dither and an explicit sample-rate-conversion quality

- **Dither, off by default.** The WAV export can apply TPDF (triangular-PDF) dither immediately before
  quantisation. TPDF is the distribution that makes the total quantisation error's variance exactly
  LSB²/4 with zero mean *independent of the signal*, so a low-level passage stops being quantised into a
  correlated distortion. It is **off unless you ask for it** — the reproducibility claim above depends on
  byte-identical renders, and an always-on dither would falsify it — and it is **deterministic**, seeded
  from a constant, so a dithered render is reproducible too.
- **An explicit SRC quality.** The sample-rate converter the render uses is now selectable:
  `linear` (the default, and the converter this engine has always used), or libsamplerate's
  `sinc_fastest` / `sinc_medium` / `sinc_best`. The default is the historical converter, so a render that
  asks for nothing is unchanged.
- **Engine:** `include/ExportDither.h` + `src/core/ExportDither.cpp`,
  `include/SrcQuality.h` + `include/ExportRenderSettings.h` + `src/core/ExportRenderSettings.cpp`, the
  `OutputSettings` fields, `AudioFileWave::writeBuffer`, and `Sample::play`.
- **Control surface:** the `export.*` group — `export.get_settings`, `export.set_dither`,
  `export.set_src_quality` — with argument/result schemas and A16 reversibility metadata (both setters are
  `true_inverse`: an action checkpoint restores the previous value, so `control.undo` reverses them).
- **Proof:** `ExportDitherTest` (the TPDF moments, the decorrelation of the quantisation error with an
  inverted control, determinism, and a byte-level off-by-default assertion on real WAVs) and
  `AudioResamplerRatioTest` (the converter ratio convention, and the chosen quality reaching the
  resampler). Both are registered ctests.
- **The converter ratio convention was corrected in the documentation, not in the code.** Three prose
  sites claimed libsamplerate's `src_ratio` was inverted relative to the engine's documented
  output/input convention, and `docs/WARP.md` §3.1 concluded from that a live defect on the
  mismatch-rate path. Measured on the library this build links, `src_ratio` *is* output/input
  (`src_ratio = 2.0` consumes 4096 input frames and generates 8192 output frames), so the engine was
  always right and the "fix" the note implied would have made every 48 kHz source in a 44.1 kHz project
  play ~8.8 % fast and sharp. The convention is now pinned by `AudioResamplerRatioTest`, and
  `docs/WARP.md` §3.1 carries the correction.
- **Interface absence:** drivable through the socket, not from the interface. Neither setting appears on
  the export dialog.

## Provenance

Written during the 0.3.0-alpha programme from the merged lane branches on `release/0.3.0`; the base of
record is `post-alpha/integration` @ `70f2d087c`. This file is extended by each lane's merge, and
re-checked claim by claim against the built artefact before the tag is cut
(`[VERIFY AT FREEZE]` marks a claim that must be re-checked against the built artefact; nothing carrying
that marker is published as-is, and no unverified claim is published without one).

## Rack macros and key/velocity zones (`rack.*`) — added 2026-09-13

- **New: the rack has a command surface, and with it the two things #599 left out.** Before this, a rack could
  only exist in a project file that already contained a `<rack>` element — there was no way to build one at all.
  `rack.get_state`, `rack.add_chain`, `rack.remove_chain` and `rack.set_selected` now drive the chains and the
  chain selector, and each mutating call records its SPEC A16 reversibility class, its before-state and how it
  is reversed, so one `control.undo` takes the edit back.
- **New: macros.** A rack macro is a **named, persisted scalar (0..1) that drives existing parameters**, each
  through its own **range window**. `rack.macro_add` creates one, `rack.macro_target_add` binds a parameter
  (addressed as the rack chain, the `fx-<n>` device and the parameter's name, so a bind survives a reload), and
  `rack.macro_set` sets the value and writes every bound parameter in one step. The window is stated as a
  **fraction** of the parameter's own range, so one macro value moves a `-100..100` panning control and a
  `0..100` gain control by the same fraction of their ranges, and an inverted window (`high < low`) drives the
  other way. A parameter that no longer exists is **skipped and counted**, never guessed at; a target that
  names nothing is refused at bind time. `rack.macro_set` is one undo step covering the macro's own value *and*
  every parameter it wrote.
- **New: key and velocity zones.** `rack.zone_add` / `rack.zone_remove` store a **key range and a velocity
  range mapped to one of the rack's chains**, with an optional sample reference, and `rack.zone_resolve` answers
  which zone a note falls into — the first zone, in the order the zones were added, whose key range *and*
  velocity range both contain the note. A zone's ranges are the engine's own (`0..127` keys, `0..200`
  velocity, the note-velocity range `note.velocity_set` uses) and a zone that names a chain the rack does not
  have is refused rather than stored.
- **Both are saved under the existing `<rack>` element** inside the channel's `<mixerchannel>` — as `<macro>`
  and `<zone>` children, with no second container — and both survive a save/reload round trip. The format is
  additive: the element's version is now `2`, a version-1 reader ignores the new children, and a channel with
  no rack at all still writes no element.
- **Proof:** the registered ctest `RackMacrosTest` (docs/RACK-MACROS.md §5), which drives the window
  arithmetic against real `FloatModel` parameters, resolves the zone ranges, round-trips the whole
  configuration through the mixer's own save/load, exercises the group through the command registry including
  `control.undo`, and measures that configuring macros and zones leaves the rack's audio path allocating
  nothing.
- **UI absence — one line: rack macros and key/velocity zones are drivable through the socket, not from the
  interface.** There is no macro knob, no zone editor and no key map to draw; nothing in `src/gui/` creates,
  shows or moves either one. `docs/KNOWN-LIMITATIONS.md` carries the same sentence.
- **And the zone half does not route yet, stated rather than implied.** The rack renders one stereo block and
  has no per-note input, so **no note path consults a zone in this build**: `rack.zone_resolve` reports which
  zone a note *would* fall into and nothing acts on that answer. What shipped is the persisted, validated,
  queryable zone model and its resolver. Closing the gap needs a per-note data path at the rack, not a control
  on this one — `docs/RACK-MACROS.md` §4.

## Clip fades, crossfades and clip gain (`clip.*`) — added 2026-09-13

- **New: a clip carries a gain and a fade-in/fade-out ramp, and the engine applies them.**
  `include/ClipEdits.h` / `src/core/ClipEdits.cpp` hold the values on the base `Clip`; the play handle snapshots
  them and multiplies the envelope into the frames it renders (`src/core/SamplePlayHandle.cpp`) — **not** in
  `Sample::render`, which the browser preview and the metronome share, so a preview is not faded.
  Three registered commands drive it: `clip.set_gain` (`gain_db`, −60…+24), `clip.set_fade` (tick lengths and a
  shape each: `linear`, `exponential`, `equal_power`) and `clip.crossfade` (`out`, `in`, `shape`), which ramps
  two overlapping clips on one track into each other over exactly their overlap. Every mutating call records its
  SPEC A16 reversibility class (`true_inverse` — the clip's or the track's own ProjectJournal checkpoint), its
  mechanism and its before-state, so one `control.undo` takes the edit back. `arrangement.get_state`,
  `track.get_state` and `roll.get_state` report each clip's `gain_db`, `fade_in`, `fade_out` and both shapes.
- **The two ramps of a crossfade are measured, not asserted.** The suite that lands with this feature
  (`tests/src/core/ClipEditsTest.cpp`, `tests/src/core/ClipFadesRenderTest.cpp`) asserts the identity an
  equal-power pair must satisfy (`gain_in² + gain_out² == 1` at 101 points), then renders a crossfaded pair
  through the real export path and compares the summed audio against the closed form of the pair, with the hash
  of every render printed as `AB_EVIDENCE`. It also pins the property the rest of the release depends on: a
  clip with no fade and unity gain renders **byte-identically** to the same clip with the feature's defaults
  assigned, while a real fade moves the hash.
- **UI absence — one line: clip fades, crossfades and clip gain are drivable through the socket, not from the
  interface.** There is no fade handle to drag, no crossfade gesture when two clips overlap and no clip-gain
  control on the clip; the waveform does not draw the ramp either. `docs/KNOWN-LIMITATIONS.md` carries the same
  sentence.
- **Stated limits of this feature, not to be mistaken for bugs.** Fades and clip gain are applied to **audio
  clips only** in this release: a MIDI clip can carry the fields but nothing renders them, so the commands
  refuse a MIDI clip with a typed error rather than writing state that does nothing. A crossfade is a *pair of
  independent fades*, not a linked object, so moving or resizing one clip afterwards breaks the pairing without
  a warning. There is no fade curve editor, and no fade at all is drawn.

## Browser tag/metadata search and the waveform peak cache (W8) — added 2026-09-13

- **The browser can be searched by what a file IS, not only by where it sits.** `browser.query` finds files
  in the directories the browser reads by name, by tag, and — with `probe` — by what the audio file itself
  says it is: sample rate, channel count, length, and the embedded title/artist/album/comment/genre tags.
  `browser.roots` lists the directories (the same ones the sidebar's tabs read), `browser.tags` lists the
  vocabulary of the library, and `browser.tag.add` / `browser.tag.remove` edit it. Metadata is what
  libsndfile's own header parse and tag read return — there is no index and no database of ours.
- **The waveform peaks are cached, and the cache is observable.** `browser.peaks` answers a file's min/max
  peaks at the resolution you ask for (up to 2048 buckets: about 16 KiB per file, whatever the file's
  length), and says whether the answer came out of the cache or off the disk. It is bounded at 32 files,
  least-recently-used first, and an entry is dropped when the file changes underneath it.
- **Tags live in the user's config directory, not in the project file.** They are written to
  `browser-tags.json` next to the config that already holds the browser's favourites, atomically (a crash
  mid-write leaves the previous file, not a truncated one). A tag describes your library, so it survives a
  project being closed; and the project format is left exactly as it was, so a tagged library still opens in
  any other LMMS.
- **The reversibility of a tag edit is a real inverse, not a description.** `browser.tag.add` and
  `browser.tag.remove` each record a SPEC A16 transaction whose inverse is the *paired command*, which
  `control.undo` dispatches — the tag store is not a journalled object, so the class is `snapshot` and the
  before-state carries the file's tag set. The suite applies the edit, asks `control.undo` to take it back
  and then reads the store file itself.
- **Proof:** `tests/src/core/BrowserCatalogTest.cpp` (the engine: a byte-written RIFF/WAVE probe, the tag
  store's round-trip through its file, the peak cache's hit/miss behaviour) and
  `tests/src/core/ControlBrowserCommandsTest.cpp` (the surface: schemas, typed refusals, the A16 undo
  proof). Both are registered ctests.
- **UI absence — one line: browser tag/metadata search and the waveform peak cache are drivable through the
  socket, not from the interface.** The filter box still matches file names only, there is no tag column,
  no tag editor and no query UI, and the browser draws no waveform. `docs/KNOWN-LIMITATIONS.md` carries the
  same sentence.

## Bounded, coalescing undo (`control.*`) — added 2026-09-13

- **The undo stack is bounded TWO ways, and both bounds are readable.** A count cap (100 steps by
  default, settable up to 10000) and a **byte budget** over the serialised checkpoints (16 MiB by
  default, settable up to 512 MiB), evicting oldest-first, with the newest step never dropped and every
  eviction counted and reported. `control.undo_depth` returns the depth, both caps, the bytes retained,
  how many steps a bound has evicted and whether it has evicted anything at all (`bounded`), so an agent
  can tell "this is the whole history" from "this is what the bound retains";
  `control.set_undo_depth {steps?, bytes?}` sets either cap and reports what lowering it cost
  (`dropped`). Before this, the cap was a hardcoded 100 with no byte bound and no way to read either.
- **A drag is one undo step, not one per call.** Two consecutive calls of the same command on the same
  target, with no other step pushed in between and less than the coalescing window (400 ms) apart, are
  **one** undo step — so a 200-call `clip.move` drag costs one Ctrl+Z instead of 200. The commands it
  applies to are declared per command in the A16 contract table (`clip.move`, `clip.resize`,
  `mixer.set_volume`, `plugin.param_set`, `rack.macro_set`) with the argument(s) that name the target,
  and the gesture ends on any other command, any other target, any other step in between, a pause
  longer than the window, or a window change. `control.set_undo_coalescing {window_ms}` sets the
  window, and **0 turns grouping off entirely — which is the pre-0.3.0 behaviour, one step per call** —
  so the effect of the rule is measurable rather than asserted. On the transaction record, a coalesced
  run is ONE record with a `commands` count, never one record per call.
- **`control.undo` refuses, typed, when a bound has evicted the step it would unwind** (the record
  carries the serial of the step it describes), instead of taking back a later edit than the one asked
  about. Both bounds are session state, not project state, and neither survives a restart.
- **Engine:** `include/ProjectJournal.h` + `src/core/ProjectJournal.cpp` (the accounting) +
  `src/core/ProjectJournalBounds.cpp` (the two caps, the eviction, the coalescing primitive);
  `src/core/ControlUndoCoalescing.cpp` (the rule), `src/core/ControlTransactions.cpp` (the record),
  `src/core/ControlCommandsUndo.cpp` (the three commands), and the contract table's new coalescing
  column (`src/core/ControlReversibilityTable.cpp`, the `RC()` rows).
- **Proof:** `tests/src/core/UndoBoundsTest.cpp` (registered ctest) — the declared depth and the count
  cap measured through the socket; the 200-call drag asserted to be one journal step and one record,
  then undone once and read back; the window-at-0 negative control in the same process (20 calls, 20
  steps) against the same 20 calls coalesced into 1; a pause, a different target and an intervening
  command each shown to break the run; the byte budget set from a step's own measured size with the
  retained bytes held under it; and an evicted step proved to make `control.undo` refuse without
  unwinding anything. `ReversibilityContractTest` gained the anti-drift assertion that every command
  declaring coalescing is a `true_inverse` row and appears in `control.undo_depth`'s report.
- **UI absence — one line: the undo depth, its caps and the coalescing window are drivable through the
  socket, not from the interface.** There is no undo-history panel, no depth or memory setting in any
  dialog, and no way to change the grouping gesture from the interface; Edit ▸ Undo / Redo (Ctrl+Z)
  remains the one interface affordance and it drives the *same* `ProjectJournal::undo()` the socket
  does. `docs/KNOWN-LIMITATIONS.md` carries the same sentence.
- **Both decisions — the cap values and the coalescing rule — are written down in
  `docs/UNDO-BOUNDS.md`**, with the reason for each and the two things deliberately left out of the
  rule (`note.move`/`note.resize`, whose target is re-derived by the move itself, and `warp.move`, whose
  marker key *is* the value being edited).

## Comping: take lanes and a non-destructive composite (`comp.*`) — added 2026-09-13

- **New: a track has take lanes, and a composite assembles them without touching a single byte of take audio.**
  A lane is a child relationship of the track (not a second track type): the takes stay in the track's own clip
  list and carry a lane tag (`lane` on the clip's element, default 0). Seven registered commands drive the
  feature — `comp.lane_add`, `comp.lane_remove`, `comp.lane_list` (the lanes and the takes on each),
  `comp.assign` (an audio clip becomes a take of a lane), `comp.select` (choose which lane supplies the
  composite over a tick range, slipped `srcpos` ticks into that take), `comp.rebuild` (sort, merge and — given
  a span — clamp to it and fill every gap with the base lane) and `comp.get_state` (lanes, composite, and what
  each segment resolves to: the take clip and the source frame its first tick reads, `bound` or `unresolved`).
- **A composite is a VIEW, and that is the whole design.** It is an ordered, gapless list of
  `{begin, end, lane, srcpos}` choices over `include/TakeLane.h`; resolution maps a tick back onto the take clip
  through the clip's own `sourceFrameAt()` mapping, so the comp reads the take where it already lies. Nothing is
  copied, merged, normalised or rewritten, and no playback path reads the composite yet. The proof is a
  byte-identity pair: after every `comp.*` command and after a save/reload, the take **files** and the take
  **buffers** are sha256-identical, while what a tick resolves to changes when the selection changes.
- **Engine:** `include/TakeLane.h` + `src/core/TakeLane.cpp` (the lanes, the composite, resolve/takeAt) and
  `Track::takeLanes()`, serialised by `Track::saveTrack` as ONE `<takelanes>` element written only when the model
  is non-empty — so a project that never comped serialises byte for byte as before. The decisions, the element
  shape and the `metadata="1"` trap (`Track::loadTrack` turns an unrecognised child of `<track>` into a real Clip)
  are recorded in **`docs/COMPING.md`**.
- **Control surface:** the new `comp.*` group, split across `src/core/ControlCommandsComp.cpp` (the take half) and
  `src/core/ControlCommandsCompEdits.cpp` (the composite half), with argument/result schemas and A16
  reversibility rows for all seven ids (`src/core/ControlReversibilityTableTrueInverse.cpp` and
  `...Passive.cpp`). Every mutating call takes the object's own ProjectJournal checkpoint before it writes, and
  every refusal is typed and happens BEFORE the checkpoint, so a refused call leaves no undo step behind.
- **Proof:** the registered ctest `TakeLaneCompTest` (`tests/src/core/TakeLaneCompTest.cpp`) — the ten claims
  listed in `docs/COMPING.md` §6, including the byte-identity proof, the round trip, the reset-on-absence
  behaviour on both levels, the seven typed refusals and `control.undo` unwinding a `comp.select`.
- **UI absence — one line: take lanes and comping are drivable through the socket, not from the interface.**
  There is no lane row, no lane header, no comping gesture, no audition and no waveform drawing of the composite;
  nothing in `src/gui/` creates, shows or edits a lane or a comp. `docs/KNOWN-LIMITATIONS.md` carries the same
  sentence.
- **Stated limits, not to be read as bugs: a comp does not sound different from the track's clips in this
  release.** No playback path consumes the composite, so the per-segment `srcpos` slip is recorded and reported
  but not applied; MIDI comping is out (`comp.assign` refuses a MIDI clip with a typed error); and `comp.audition`
  / `comp.flatten` from the design's sketch are not implemented — flatten is the destructive bounce, and it is
  deliberately absent while nothing renders a composite.

## Tempo map: tempo and time-signature changes on the timeline (`transport.tempo_map_*`) — added 2026-09-13

- **New: the timeline can carry tempo and time-signature events.** A tempo map is an ordered set of events,
  each with a tempo and/or a time signature, saved with the project (a `<tempo-map>` element inside `<song>`),
  and the ticks-to-time conversion reads it. Before the map's first event the global tempo is still in force,
  and past the last event the last event holds — so an event added at bar 16 cannot retime bars 1-15
  (`docs/TEMPO-MAP.md` §2 has both decisions and why).
- **The engine reaches the timing path.** The transport follows the map once per audio block, so a mapped
  project really plays at the mapped tempo; the change takes effect at the start of the block that contains it
  (sample-accurate tempo automation is a separate, still-open item).
- **Control surface:** `transport.tempo_map_get` (every event, the active flag, and the tempo, time signature
  and elapsed seconds the map answers at the play head), `transport.tempo_map_add`, `transport.tempo_map_remove`,
  `transport.tempo_map_clear` and `transport.tempo_map_set_active`. Each mutating call records its SPEC A16
  reversibility class (`true_inverse` — an action checkpoint that writes the captured map back, because a
  tempo map is not a `JournallingObject` and no `Song` checkpoint carries it), its mechanism and its
  before-state, so one `control.undo` takes the edit back.
- **The empty path is unchanged, and that is measured.** A project that has no tempo map gains no element in
  the file: `TempoMapTest` loads a real project, saves it, and requires the bytes to be identical, then
  requires the engine's frame/tick scalar and the play head's advance to be exactly what they were with the
  map inactive — with the same map switched on moving both, so the equality cannot pass vacuously.
- **UI absence — one line: tempo and time-signature changes are drivable through the socket, not from the
  interface.** Nothing in `src/gui/` draws, edits or reads a tempo map. `docs/KNOWN-LIMITATIONS.md` carries the
  same sentence.
- **Proof:** the registered ctests `TempoMapTest` (the map's arithmetic at and around every event, the
  verbatim empty-map path, the play head retiming, the byte-identical round trip) and
  `ControlTempoMapCommandsTest` (the schemas, the typed refusals and the inverse of every mutating command).
- **Stated limits.** Time-signature events change the bar/beat arithmetic, not the tick-to-frame rate —
  matching the pre-existing engine, which divides by `DefaultTicksPerBar` and never by the metre. There are no
  tempo *curves*: events are steps.

## Tempo-map export and import: Standard MIDI File conductor interchange (`interchange.*`) — added 2026-09-15

- **New: the tempo map leaves the program as a file another DAW reads, and comes back.** The map is written
  as a **format-1 Standard MIDI File with one conductor track** at **480 ticks per quarter note**, carrying the
  tempo and time-signature events, and a file's conductor events can be read back and applied to the map.
  LMMS' own grid is 48 ticks per quarter note, so a LMMS tick maps into the file **exactly** (×10, no
  rounding); the tempo meta event is microseconds per quarter note, and `bpm -> µs -> bpm` is the identity for
  every integer tempo the engine accepts (10..999 — measured, not asserted).
- **The tick-0 rule, stated because a DAW's reader depends on it.** An SMF has no "global tempo before the
  first event" — before one, a player assumes 120 bpm — so the writer seeds tick 0 with the tempo and metre the
  timeline obeys there unless the map already carries that half at tick 0. A map whose first event is at bar 5
  therefore exports bars 1-4 at the tempo they actually play, and `seed_events` in the reply says how many
  halves the rule had to add. Importing such a file leaves every sampled tempo and metre unchanged.
- **Control surface:** `interchange.smf_convention` (the PPQ, the LMMS ticks per quarter, the ratio, the tempo
  unit, the time-signature byte layout, the file shape and the tick-0 rule — the convention as data on the
  wire), `interchange.smf_export`, `interchange.smf_read` (read a file's events back **without touching the
  session**, which is what makes a round trip checkable against the file rather than against its hash) and
  `interchange.smf_import`. Export and read are `not_mutating` (the export writes a file outside the session);
  an import replaces the tempo map and records its SPEC A16 class (`true_inverse` — the same action checkpoint
  the `transport.tempo_map_*` commands use, because the map is not inside any `Song` checkpoint), so one
  `control.undo` brings the previous map back.
- **Cross-DAW on the read side too.** A foreign division (96, 960, 1000 ppq) is scaled onto LMMS'
  48-ticks-per-quarter grid, every event that had to be rounded is **counted and reported**
  (`rounded_events`), every track's tempo and metre events are read and merged by tick, and a file needing more
  ticks than the map holds (128) is refused — `capacity_events` says by how much — rather than truncated into
  the map.
- **UI absence — one line: Standard MIDI File tempo-map interchange is drivable through the socket, not from
  the interface.** Nothing in `src/gui/` writes or reads a conductor track; `File > Export MIDI` is the
  pre-existing note export and is neither changed by nor wired to these ids. `docs/KNOWN-LIMITATIONS.md`
  carries the same sentence.
- **Proof:** the registered ctests `SmfInterchangeTest` (the four ids, the schemas, the typed refusals, and
  the written file's bytes checked against the format by the **test's own** Standard MIDI File parser, which
  shares no code with the module under test) and `SmfInterchangeRoundTripTest` (**the round trip the feature
  list names**: a map with tempo AND metre changes exported, the session's map cleared, the FILE imported, and
  the MAP then compared — event for event, as the engine's own `TempoMap` object, and as sampled step
  functions against an oracle built from the authored events — not the file, not its hash).
- **Stated limits.** Events are **steps**: the map holds steps and the format's tempo event is a step, so no
  tempo curve is written (there is none to write). **Only the conductor track** is written — notes, clips,
  automation and markers are not in the file. On read, only the tempo and metre meta events are used, so the
  rest of a foreign file is ignored rather than refused. And import **replaces** the map (no merge, no
  "import into a range"). All of it is in `docs/SMF-INTERCHANGE.md`.

## DAWproject import / export (`dawproject.*`) — added 2026-09-15

- **New: the session leaves the program as a DAWproject container another DAW reads, and comes back.**
  Tracks, clips, notes, the tempo map, the global tempo and metre and the mixer strips are written as a
  **DAWproject 1.0 container** (ZIP with `project.xml` and `metadata.xml`, UTF-8), and a file can be read
  back and imported. The format is version 1.0 and stable (the published spec's own statement); this module
  writes `version="1.0"` and refuses a file declaring another major version.
- **Control surface:** `dawproject.convention` (the format version, the container, the time unit, the tick
  rule and the stated losses as data), `dawproject.export`, `dawproject.read` (read a file's model back
  **without touching the session**, which is what makes a round trip checkable against the model rather than
  against its hash) and `dawproject.import`. Export and read are `not_mutating` (they write a file outside
  the session or only read); import replaces the session's tracks, tempo map, globals and mixer strips and
  records its SPEC A16 class (`true_inverse` — a recorded action checkpoint carrying the captured document,
  because the whole session is not inside any `Song` checkpoint), so one `control.undo` brings the previous
  session back.
- **The mixer model is separate strips.** LMMS' MixerChannel is a summing strip several tracks may feed, not
  a property of any one track, so the format's bare `<Channel>` elements carry the mixer strips and each
  track's `<Channel destination="...">` names the strip it feeds. The one routing fact the format CAN
  express is preserved; the wider MixerRoute graph, pre/post-fader flags and which tracks share a strip are
  not (LOSSY #7).
- **UI absence — one line: DAWproject import / export is drivable through the socket, not from the
  interface.** Nothing in `src/gui/` writes or reads a DAWproject container. `docs/KNOWN-LIMITATIONS.md`
  carries the same sentence.
- **Proof:** the registered ctest `DawProjectInterchangeRoundTripTest` (**the round trip the feature list
  names**: a model with tracks, clips, notes, mixer channels and tempo-map points is written, read back and
  compared — model for model, not file for file or hash for hash — then applied to a session, extracted and
  compared again, and the import's undo is measured). The ids and the track-to-strip IDREF are asserted by
  name too, so a document that gets renumbered fails with the id it changed rather than only in the blanket
  comparison. `docs/DAWPROJECT-INTERCHANGE.md` section 9 carries the measured proof output.
- **The model's own ids survive the trip.** The document's ids are the MODEL's: a mixer strip is written
  with its `mixer<n>` id, a track with its own, and every IDREF (`destination`, a lane's `track`) points at
  the id the model carries rather than at one the writer invented, so export → import → export is stable
  and the track-to-strip join survives a round trip.
- **Stated limits.** Eleven losses are recorded, each counted: audio clips and their media, automation
  clips, device/plugin state, sends, fades and clip gain, loop points, scenes and clip slots, folder nesting
  (LMMS' track list is flat), mixer routing and sharing, and track types with no format counterpart; the
  track type's NAME is not in the document (it is derived from `contentType`, so a differently-spelled name
  comes back canonical); and an id the schema would reject or the model repeats is replaced by a generated
  one (`xs:ID` must be unique and an NCName). The tempo is bounded to the engine's own 10..999 and a file
  outside them is refused. Time values are beats; a foreign time off LMMS' 48-ticks-per-beat grid is rounded
  onto it and counted. All of it is in `docs/DAWPROJECT-INTERCHANGE.md`, with the format version read and
  cited and where each loss is counted.

## The groove pool and quantise (`groove.*`) — added 2026-09-13

- **New: the feel of a note pattern can be captured, named and re-applied, and notes can be
  quantised with a strength and a humanise amount.** A *groove* is a cycle length, a slot width and
  one timing/velocity step per slot (`include/GrooveTemplate.h`); a *pool* is the project's named
  grooves (`include/GroovePool.h`). `groove.extract` reads the feel out of a clip's notes — each
  slot's timing step is the mean signed deviation of the notes that fell in it, and its velocity
  step is that slot's mean velocity; a slot the clip never played carries no velocity opinion, so it
  cannot flatten the notes that land there later. `groove.apply` writes it back: each note is snapped
  to its slot and given that slot's velocity, by `strength` (0..1) of the way — at 1 it lands
  exactly, at 0.5 it is half the feel, and because both targets are absolute a second apply has
  nothing left to do. `groove.quantize` is the grid quantise with the same strength control plus a
  `humanise_ticks` / `humanise_velocity` jitter drawn from a **seed** and each note's own identity
  (the mechanism MIDI depth's rolls use), so the same call on the same notes reproduces the take and
  a second seed is a second take. `groove.set` writes a groove verbatim, `groove.remove` deletes one and `groove.rename`
  renames one in place.
- **Engine:** `include/GrooveTemplate.h` + `src/core/GrooveTemplate.cpp` (the value type, the
  extraction and application arithmetic), `include/GroovePool.h` + `src/core/GroovePool.cpp` (the
  named pool and its XML form), `NoteTransform::quantizeNotes` (the strength-and-humanise grid
  quantise, beside the existing strength-less `quantizePositions`, which is unchanged), and
  `Song::groovePool()` — the pool is project state, saved as ONE `<groove-pool>` element written
  **only when it holds a groove**, so a project that never used one re-saves byte for byte as
  before. The decisions and the measured numbers are in **`docs/GROOVE-POOL.md`**.
- **Control surface:** the `groove.*` group — `groove.list`, `groove.extract`, `groove.set`,
  `groove.apply`, `groove.quantize`, `groove.remove`, `groove.rename` — with argument/result schemas
  and A16 reversibility metadata. Six of the seven are `true_inverse`, and they are two different
  mechanisms: a clip edit reverses through the MidiClip's own journal checkpoint, and a pool edit
  through a recorded action checkpoint, because the pool is not in the track container and a Song
  checkpoint does not carry it (the finding the tempo map and the modulation layer record). Every
  refusal is typed and happens before anything is written.
- **Proof:** the registered ctests `GrooveTemplateTest` (the arithmetic, with no Engine at all: the
  exact per-slot numbers, idempotence, the strength interpolation, the velocity clamp, the pool's
  XML round trip and its reset-on-absence), `ControlGrooveCommandsTest` (the seven ids, the contract
  rows, the measured effect of an apply and a quantise read back through `roll.get_state`, both
  inverses, and a real project save/load round trip whose negative control must leave an empty
  pool) and `ControlGrooveCommands` (`tests/control-groove-commands.py`: a real binary driven over
  `--control-socket` by an external client, asserting the same tick positions and velocities off the
  wire).
- **UI absence — one line: the groove pool and quantise are drivable through the socket, not from the
  interface.** There is no groove list, no template browser, no drag-to-apply and no quantise
  dialog; nothing in `src/gui/` creates, shows, edits or applies a groove.
  `docs/KNOWN-LIMITATIONS.md` carries the same sentence.
- **Stated limits.** A groove's resolution is the slot, so several notes in one slot are described
  by their mean; there is no swing-percentage template generator (`groove.set` writers or
  `groove.extract` only); a groove moves MIDI notes, so a sample clip is refused typed; and a groove
 is applied once and the notes are ordinary notes afterwards — nothing on the audio path reads a
 template.

 ## Plugin chains as reusable presets (WAVE-1 / OWNER-31 item 2) — added 2026-09-13

 - **A track's effect chain can be captured as a named preset and applied to another track, in another
 project.** `chain.save` captures the target's chain — the ordered device list plus **each device's
 own state document** (`plugin.state_save`'s document, so enabled, wet, autoquit and every parameter
 travel with it) — into ONE file in the user preset tree
 (`<userPresets>/chainpresets/<name>.zcp`). `chain.list` and `chain.get_state` read the store back
 (each preset's name, path, device count and, per device, its own identity plus the size and SHA-256
 of its state document); `chain.apply` replaces a target's chain with the preset's devices **in the
 preset's order**, each device restored through `plugin.state_load`'s own path, which refuses a
 document written for a different device; `chain.rename` and `chain.remove` edit the store. A chain
 preset is **not** a rack chain: `rack.add_chain` adds one more parallel signal path inside one
 channel's rack (`docs/RACKS.md`), while a chain preset is a copy of a chain's devices and settings
 that can be applied anywhere, later.
 - **The store is outside the project, deliberately.** A preset exists to be reused, and reuse means
 another track in another project: the store is the product's own user preset root, so it survives
 `project.save` / `project.open` by construction (it is not in the document at all) and it is the
 **same** store under every project. `docs/KNOWN-LIMITATIONS.md` states that it is per-user rather
 than per-project, and what that costs.
 - **Control surface:** the `chain.*` group — `chain.list`, `chain.get_state`, `chain.save`,
 `chain.apply`, `chain.rename`, `chain.remove` — with argument/result schemas and A16 reversibility
 metadata. All four writers are `true_inverse` through a recorded **action checkpoint**, each undoing
 its own operation: `chain.apply`'s writes the chain's own `<fxchain>` XML back
 (`EffectChain::saveSettings` captured before the write, `EffectChain::loadSettings` — the project
 loader's own path — on the way back), `chain.save`'s removes the file it created or writes the
 revision it replaced back, `chain.rename`'s renames the file back and `chain.remove`'s writes the
 removed bytes back. A target chain too large for the bounded snapshot is **refused** by
 `chain.apply` rather than replaced without an inverse. Every refusal is typed and happens before the
 checkpoint, so a refused call writes nothing.
 - **Proof:** the registered ctests `ControlChainPresetTest` (the six contract rows, the document's
 identity and name rules) and `ControlChainPresets` (`tests/control-chain-presets.py`), which starts
 the real binary over `--control-socket` and asserts a REAL effect off the wire: the ordered device
 list and the parameter values read back through `dsp.get_state` after an apply to a second track,
 the same values again after a real `project.save` → `project.open` round trip, and every inverse
 through `control.undo`.
 - **UI absence — one line: plugin-chain presets are drivable through the socket, not from the
 interface.** There is no chain-preset list, no "save chain as preset" action and no apply control;
 nothing in `src/gui/` creates, shows, edits or applies a chain preset.
 `docs/KNOWN-LIMITATIONS.md` carries the same sentence.
 - **Stated limits.** A preset carries effects only — not the track's instrument, not its mixer
 routing and not the rack's parallel chains; a preset naming a device this build cannot load is
 refused typed and the target's chain is left untouched; and the store is a per-user directory, so a
 preset is not carried inside a project file, not shared with one and not versioned with it.

## Phase-locked multitrack edit groups (`vca.*`, OWNER-31 item 11) — added 2026-09-14

- **New: VCA / mix-and-edit groups are drivable.** The group *entity* landed earlier (task #622:
  `include/VcaGroup.h`, `Mixer::createVcaGroup`, one fader published to member channels as a separate
  relative factor, `<vcagroup>` beside the channels). What it never had was a way to *make* one, a way
  to *address* one, and the edit half the feature's own name promises. This adds the registered
  `vca.*` command group - **14 ids** - over that entity:
  `vca.create`, `vca.remove`, `vca.list`, `vca.get_state`, `vca.rename`, `vca.set_gain`,
  `vca.set_mute`, `vca.set_solo`, `vca.assign`, `vca.unassign`, `vca.set_phase_lock`,
  `vca.track_add`, `vca.track_remove`, `vca.edit_move`. A group is addressed by its own stable id,
  `vca-<n>`, the grammar every other addressable object here uses (`ch-<n>`, `trk-<n>`, `clip-<n>`);
  the naming decision and why are recorded in `src/core/ControlCommandsVcaShared.h`. **Before this,
  a group could only be created by editing the project file.**
- **The edit half is built: the phase lock.** A group also carries a set of *tracks*
  (`vca.track_add` / `vca.track_remove`) and a lock flag (`vca.set_phase_lock`, ON by default). With
  the lock on, `vca.edit_move` moves the clip you name **and every other member's clips by the same
  delta**, so a take recorded across eight inputs is slid as one object and stays sample-aligned.
  The correspondence rule is the design decision: the named clip is the ANCHOR and goes to exactly
  the position asked for, and every other member's clips that OVERLAP the anchor's pre-command span
  move by the same delta; a member with nothing in that span is reported in `unlocked_tracks` and a
  member whose track is gone in `skipped_tracks`. The delta is a delta, not "move every member onto
  the anchor's new position", so two members deliberately offset by a few ticks stay offset - which
  is what makes this a lock and not a snap-to-grid. `vca.set_phase_lock false` keeps the membership
  and switches the propagation off.
- **The edit set is persisted.** `<vcagroup>` gained a `locked` attribute and one
  `<edittrack track="n"/>` child per edit-set track (`src/core/Mixer.cpp`), written by STABLE TRACK
  ID and not by position. An older LMMS skips both, exactly as it skips a `<vcagroup>`, so grouping
  still degrades to "no groups" there; a project with no `<vcagroup>` loads with no groups and every
  channel at unity.
- **Reversibility.** All twelve mutating ids are `true_inverse` (see the histogram section below for
  the per-row mechanism); the two reads are `not_mutating`. `vca.edit_move` is the interesting one:
  every clip it moves gets a live Clip checkpoint and the registry merges them into ONE undo step,
  so one `control.undo` (or one Ctrl+Z) returns every member - not one undo per member.
- **Proof.** `tests/src/core/ControlVcaCommandsTest.cpp` (registered QTest: the fourteen ids and
  their schemas, the typed refusals, the contract rows, the measured effect of a lock read back
  through `clip.move`-free state, and `control.undo` after a lock) and `tests/src/core/VcaGroupTest.cpp`
  extended with the entity half (edit-track membership, the lock flag, and both surviving a
  save/reload round trip through `Mixer::saveSettings`/`loadSettings`). The socket proof of the same
  claim is the registered ctest **`ControlVcaCommands`** (`tests/control-vca-commands.py`), which
  starts the real binary with `--control-socket` and drives it end to end.
- **UI absence — one line: VCA / mix-and-edit groups are drivable through `--control-socket` and the
  MCP bridge and have no interface.** There is no VCA strip, no group menu, no member list and no
  phase-lock toggle; a group is created, named, filled and locked from the socket.
  `docs/KNOWN-LIMITATIONS.md` carries the same sentence, and the sentence it carried before this lane
  ("a group can only be created by editing the project file") is updated rather than deleted.
- **Stated limits, in the release rather than discovered later.** (1) The ONE media edit propagated
  by the lock is a clip MOVE; trim, slip, split and fades on a locked group are not propagated - the
  membership and the rule are in place and move is the edit a multitrack take needs first.
  (2) A track deleted while it is in an edit set stays in the set: its id is reported in
  `missing_tracks` by `vca.get_state` and in `skipped_tracks` by `vca.edit_move`, and it is removed
  deliberately with `vca.track_remove`. That is a decision, not an oversight - a group that rewrote
  its own membership on somebody else's delete would hide the fact that the set changed.
  (3) `vca.set_solo`'s undo does not restore `MixerChannel::m_muteBeforeSolo`, which is transient
  and not part of the project file (the same limit `track.set_solo` states).
  (4) The mix half's audibility was measured before this lane (`VcaGroupTest`'s rendered dB delta);
  this lane's own end-to-end proof drives the model through the socket and reads state back, and
  does not re-render.

## Routing: PDC, the routing graph, buses and audio ports (`pdc.*`, `routing.*`, `bus.*`, `port.*`, and the mixer routing verbs) — added 2026-09-14

- **The latency graph the mixer runs is now readable. `pdc.report` answers the whole PDC picture in one call:**
  the total latency from a source entering the mixer to the master output (`Mixer::totalLatencyFrames()`), the
  delay line's own capacity and whether the total is clamped to it, every channel with its alignment point
  (`Mixer::channelInputLatency`) and the latency its own effect chain adds (`EffectChain::latencyFrames`),
  every regular send with the compensation the mixer applies at it, the direct track inputs the PDC graph
  reads (`AudioBusHandle::latencyFrames`), and **whether sidechain routing exists** — it does, with every
  sidechain send's tap point and deferred flag listed. Feature row 27 recorded that latency compensation was
  "neither readable nor settable through the socket"; it is readable now, and it is deliberately **not**
  settable: `Mixer::updateLatencyCompensation()` recomputes each edge's delay from the topology once per
  period, so a command that wrote one would be overwritten by the next period. What changes PDC is the
  topology, and that is settable.
- **The mixer's routing verbs exist at last.** `mixer.route_to` writes a channel's output path at unity,
  `mixer.send_to` writes an auxiliary send with an amount, `mixer.sidechain_to` writes a sidechain send with
  a tap point (`post_fader`, `pre_fx`, `pre_fader`, `post_fader_no_gain`), and `mixer.route_remove` removes
  either kind. `ableton-gap/AGENT-TOOLING.md` §7 names `route_to` and `send_to` as part of this release's
  mixer surface and the 0.3.0 tip registered neither. The engine's own rules are the refusals and the
  defaults: a send **into a bus** comes back pre-fader without being asked (`Mixer::createChannelSend`), a
  route that would close a **feedback path** is refused by `Mixer::isInfiniteLoop` before anything is
  written, a channel cannot route to itself, and a sidechain route that would close a cycle of sidechain
  sends alone is refused outright by `Mixer::createSidechainSend`.
- **The routing graph is an inspector, and this is the honest half of row 28.** `routing.get_state` reports
  the graph a target's signal is actually processed through — the `RoutingGraph`'s nodes with the engine's own
  type names (`chain_input`, `rack_chain`, `rack_sum`, `effect`, `constant`, `onepole_lowpass`, `gain`,
  `sink`), their arity, the connections, the cached topological processing order the audio thread walks, the
  output node, whether the chain renders through the graph at all (`EffectChain::routesThroughGraph`), and,
  for a mixer channel, its rack's graph (`Rack::routingGraph`). **No command edits a graph**, and that is a
  recorded decision rather than a gap left open: `include/RoutingGraph.h`'s threading contract says topology
  edits must not run concurrently with `process()` and names the atomic plan swap it deliberately does not
  implement, and a chain's graph is derived — `EffectChain::rebuildRoutingGraph()` re-wires it from the effect
  list on every change, so a hand-wired edge would be discarded by the next `plugin.load`. **The measured
  scope of the read, stated because it is narrower than "the routing graph" sounds:** a chain whose devices
  HAVE audio-ports models keeps the plain effect loop — `EffectChain::rebuildRoutingGraph()` returns early
  for exactly that (`src/core/EffectChain.cpp:89`) — and every built-in device in this tree is
  `AudioPlugin`-derived (`DefaultEffect`, `include/AudioPlugin.h:462`), so a track's or a channel's **chain**
  graph is normally empty with `routes_through_graph: false`. The graph with **live, prepared nodes** that
  this release can measure is the **rack's**, and `tests/control-routing-commands.py` measures it: two added
  chains are five nodes (input, sum, one per chain), six connections, output node 1, a prepared block of the
  audio engine's own frame count, and a topological order that puts the sum node last; removing one chain
  re-wires it to four nodes and removing the other (one chain left is not a rack) leaves it unwired. The
  patcher GUI is out of scope for this release, exactly as row 28 records.
- **Buses are topology state with the engine's own semantics.** `bus.create` makes a parallel bus
  (`Mixer::createBusChannel`), `bus.list` reports every bus with its fader, sends and PDC numbers, and
  `bus.remove` deletes one — refusing a channel that is not a bus and naming `mixer.remove_channel` for it. A
  bus never receives instrument output and its incoming sends default to pre-fader, which is why the group
  carries no `bus.set_*`: a bus **is** a mixer channel, so its fader and its routing are `mixer.set_volume`
  and the routing verbs.
- **The audio-ports pin matrix is drivable.** `port.get_state` reports a device's `AudioPortsModel` — the
  input and output matrices with their channel counts, channel names and every enabled pin, plus the engine's
  own used-track-channel / used-channel caches and whether the direct-routing optimisation is available —
  and `port.set_pin` writes one pin through the **same call the PinConnector view makes**
  (`AudioPortsModel::Matrix::setPin`), validating the direction and both indexes before anything moves. The
  model is reached through the const accessor and written through a `const_cast`, which is the engine's own
  idiom for this object: `AudioPortsModel::instantiateView()` does exactly that to hand a mutable model to its
  editor.
- **Proof.** `tests/control-pdc-commands.py` (`ControlPdcCommands`), `tests/control-routing-commands.py`
  (`ControlRoutingCommands`), `tests/control-bus-commands.py` (`ControlBusCommands`) and
  `tests/control-ports-commands.py` (`ControlPortsCommands`) each start the real binary under
  `QT_QPA_PLATFORM=offscreen` and drive it over `--control-socket` through the shared
  `control_socket_harness`. The routing transcript measures a real graph — one loaded effect is exactly two
  nodes, one connection `0 -> 1`, output node 1 and processing order `(0, 1)`; a second effect makes it three
  — and the bus transcript measures **both** A16 answers: `bus.create` is one undoable step and
  `bus.remove` makes `control.undo` fail, typed, with the `irreversible` kind. The engine halves stay where
  they were: `tests/src/core/PdcMixerTest.cpp`, `PhaseDSidechainTest.cpp`, `RoutingGraphTest.cpp`,
  `RoutingGraphLiveTest.cpp`, `AudioPortsTest.cpp`, `AudioPortsModelTest.cpp`, `AudioBusTest.cpp`,
  `AudioBusHandleTest.cpp` and `PluginAudioPortsTest.cpp` are all still registered.
- **Stated bounds.** `pdc.report` publishes **0** for every latency unless a device reports one
  (`Effect::latencyFrames()` defaults to 0, and in this tree only the WASM effect overrides it), so the
  arithmetic of a nonzero delay is proven by `PdcMixerTest.cpp`, not by the transcript. `port.set_pin` needs a
  device that **has** an audio-ports model (`AudioPlugin`-derived: the CLAP and VST3 hosts and the analysers);
  every built-in effect answers `port.get_state` with a typed `not_found` naming that fact, and
  `ControlPortsCommands` reports ctest **Skipped** (never Passed) when this build ships no such device and the
  pin write therefore cannot be measured. `bus.remove` records the bus's full state but is **not reversible**.
- **UI absence — one line per group:** PDC is drivable through the socket and nothing in the interface shows a
  latency, a compensation or a per-channel PDC table; the routing graph is drivable through the socket and
  nothing in the interface draws a patch bay, a cable, a node or a port, and no command edits a graph at all;
  buses are drivable through the socket and nothing in the interface can add one or draw one differently;
  audio ports are drivable through the socket and nothing in the interface opens a pin connector.
  `docs/KNOWN-LIMITATIONS.md` carries the same four sentences.


## Clip edges, note probability and stem export: four ids for engines that already shipped

Four 0.3.0 ids whose engines were in the tree and whose command surface was not. Each one is the
registration only — the engine, its arithmetic and (for three of the four) its tests landed in an
earlier wave — and each is added with an argument schema, a result schema and a SPEC A16
reversibility row, so the whole of what a caller needs is declared rather than inferred.

- **`clip.trim`** moves a clip's START edge and holds the audio the clip already carried at the same
  song position: the start, the length and the source offset move together. That three-part rule is
  not invented here — it is the song editor's own left-edge drag (`src/gui/clips/ClipView.cpp`), and
  it is what neither `clip.move` (which slides the audio with the clip) nor `clip.resize` (which
  changes only the tail) can do on its own. An optional `end` trims the tail in the same step.
  `true_inverse` through the clip's own ProjectJournal checkpoint.
- **`clip.slip`** moves the audio INSIDE a fixed clip rectangle: the position and the length do not
  move and the part of the source that plays at the clip's start becomes `offset` ticks into it.
  This is the first implementation of the verb in the product — a case-insensitive grep for "slip"
  over `src/` and `include/` returns seven hits and every one is a comment. `true_inverse` through
  the same checkpoint.
- **`note.probability_set`** sets the chance, in [0, 1], that a note is played at all in a take,
  the MIDI-depth field `docs/MIDI-DEPTH.md` describes and the engine has carried since. Per note, so
  one clip can hold some 100% notes and some 50% notes; rolled against the project's own MIDI seed
  once per note trigger. A value outside [0, 1] is **refused typed**, not clamped — "never silently
  clamped" is the same rule `clip.set_gain` follows. `true_inverse` through the owning `MidiClip`'s
  checkpoint.
- **`render.stems`** exports every unmuted track to its own file in an absolute directory — one stem
  per track, post-fader and post-effects, including that track's own sends and their tails, each
  rendered to the project's length plus `tail_bars` bars (default 1, the whole-project render's own
  convention) — and returns the file names it wrote. It drives the shipped `lmms exportstems` CLI in
  a child process, the same way `render.render` drives `lmms render`, and for the same reason (an
  in-process render drives this instance's audio engine). `not_mutating`: it writes output artefacts
  and touches no project state.

**UI absence, one line each.** `clip.trim` and `clip.slip` are **drivable through the socket, not
from the interface** — the trim gesture exists in the song editor but no action, menu entry or
keybinding reaches the command, and slip has no gesture at all. `note.probability_set` is **drivable
through the socket, not from the interface** — `docs/MIDI-DEPTH.md` already states that probability
is "not exposed in the GUI editor (no drag handle, no right-click entry)", and a grep for
`probability` over `src/gui/` returns zero matches. `render.stems` is **drivable through the socket,
not from the interface** — the File menu's "Export Tracks..." action is the *different, pre-existing*
`renderTracks()` path, which trims each stem to its own track and does not align them, and it is
neither changed by nor wired to this id. `docs/KNOWN-LIMITATIONS.md` carries all four sentences.

**The limits, stated rather than left to be discovered.** Stems are **per track, not per bus**: a
"bus" is a `MixerChannel`, not a `Track`, and the render path isolates tracks by muting, so
`exportStems` selects tracks (`docs/STEM-EXPORT.md`, "No bus-level stems"). Neither edge verb authors
`SampleClip`'s `srcin`/`srcout` window — that window is written only when it is not the whole buffer
and applied on load only when the attribute is present, so **no reset-on-absence exists for it** and
a checkpoint taken before a *first* window edit could not take the edit back; both verbs therefore
write only attributes their clip type serialises unconditionally, and a frame-domain trim is a later
feature. And `render.stems` carries the **declared bound** every render-running command carries: the
export blocks the dispatch thread on `waitForFinished(600000)`, so the control surface does not
answer — `control.ping` included — until it finishes. `docs/RENDER-CHILD-WAIT.md` records that defect
and designs the deferred-reply fix; **this release does not build it**, and `render.stems` states the
bound in its own description and contract row instead of pretending to a timeout knob it lacks.
### The plugin scan cache, the quarantine list and the crash reporter (rows 46 and 54)

- **The scan cache and its quarantine list are drivable, and the quarantine no longer needs a hand-edited
  file.** `plugin.scan_cache_get_state` reports the cache file and whether it is persistent and dirty, how
  many records it holds, the quarantine entries with their reasons (and whether each file is still there), and
  the last scan's own report — files found, quarantined, served from cache, known-bad skipped, scanned,
  descriptors, and the one-line `scan_report` text the log carries. `plugin.scan_cache_list` reports the
  cache's **contents**: every record sorted by path, with its fingerprint (path, size, mtime), its status
  (`has-descriptor` / `not-a-plugin` / `load-failed`) and, for a plugin, the descriptor metadata the scan
  resolved. `plugin.scan_cache_lookup` answers about one file — `cached` (the record would still be served: the
  file's size and mtime both still match), `stale` (a record exists and no longer matches, which is why a scan
  repeats work) and whether the quarantine list hides it. `plugin.scan_cache_quarantine_add` /
  `plugin.scan_cache_quarantine_remove` write the list through `PluginScanCache`'s own API **and the cache
  file**, and `plugin.rescan` runs the scan the factory already has (`PluginFactory::discoverPlugins`, a public
  slot nothing in the shipped GUI re-invokes) — which is what **applies** a quarantine edit.
- **Proof.** `tests/control-plugin-scan-commands.py` (`ControlPluginScanCommands`) starts the real binary under
  `QT_QPA_PLATFORM=offscreen` and drives it over `--control-socket` through the shared `control_socket_harness`:
  it quarantines a path, reads the entry back off the wire **and reads the cache file off disk** to prove the
  entry is really stored, removes it, and undoes the removal to get it back with its reason.
  `tests/src/core/PluginScanCacheTest.cpp` still proves the engine layer and grew two cases for the enumeration
  this group needed (`records()`, `record()`), including the fingerprint distinction `stale` reports.
- **A16, and the trap this group has its own version of.** The two quarantine verbs are `snapshot`: a
  `PluginScanCache` is a JSON file outside the project and is not a `JournallingObject`, so there is no
  checkpoint to take and the inverse is the **paired command** (`applies: command`) — the class and mechanism
  `browser.tag.add` / `browser.tag.remove` already carry. The trap is the **reason**: the cache stores one reason
  per path and nothing else reconstructs it, so `plugin.scan_cache_quarantine_remove` captures it **before** the
  write and carries it in the inverse's args — and the transcript measures the difference, by driving the
  path-only re-add a naive inverse would make (the reason comes back **empty**) and then the recorded inverse
  (the reason comes back exactly). `plugin.rescan` is `irreversible` and says so with its fallback: a scan
  re-measures the files and refreshes the cache, and no command puts a file's previous fingerprint record back.
  All three mutating ids record a transaction, so `control.undo` on the rescan fails, typed, rather than
  unwinding an older step.
- **The crash reporter is drivable, and its absences are answerable.** `crash.list_reports` reports whether the
  reporter is installed, its report directory, every report it holds with its size and last-written time,
  whether a report is still pending an offer (`hasPendingReport`, the module's own predicate), the `offered`
  sentinel, whether a session marker says the previous run exited uncleanly, the module's two hard bounds and
  the upload policy. `crash.acknowledge_report` writes the `offered` sentinel and **keeps** the report, so it can
  still be attached; `crash.discard_report` clears the report and the sentinel through
  `crashreporter::discardPendingReport()`. `crash.upload_report` is **registered and refuses** every call, by
  name: this build has no upload and no network code of any kind in the reporter — a design property
  `include/CrashReporter.h` states in as many words — so the refusal names the file to attach by hand instead of
  faking a send. There is no `crash.enable` / `crash.disable`: `main()` installs the reporter before this socket
  is reachable, and the module has no uninstall.
- **Proof.** `tests/control-crash-reporter.py` (`ControlCrashReporter`) plants a report exactly where the
  reporter looks for one and then measures the state machine over the socket: `pending` before, `pending: false`
  and `offered: true` after an acknowledge (with the sentinel verified on disk), the discard removing both
  files, and `control.undo` failing typed and naming the fallback for both writers.
  `tests/src/core/CrashReporterTest.cpp` stays registered and unchanged — the engine did not change, so the proof
  of the **surface** is the transcript.
- **Stated bounds.** Both crash writers are `irreversible` and each names its fallback: nothing in the module
  removes the `offered` sentinel (delete the file and the report is pending again; the report itself is
  untouched), and nothing writes a report from a caller's bytes (re-run the action that crashed; the discarded
  report's content is not recoverable). The read answers in every configuration — an instance with no reporter
  reports no directory and no report rather than refusing — and the writers refuse, typed, when the reporter is
  not installed (on Windows the module is a documented no-op).
- **UI absence — two lines, one per row:** the plugin scan cache and its quarantine list are drivable through
  the socket and nothing in the interface shows a scan record, a cache hit or a quarantine entry, nor offers to
  add one; the crash reporter is drivable through the socket and nothing in the interface shows a report, its
  state or its directory, and there is no way to send one. `docs/KNOWN-LIMITATIONS.md` carries the same two
  sentences.

## Offline stem separation, made drivable (`stem.*`, feature row 26) — added 2026-09-15

The engine landed long before the ids did and was already proven: HTDemucs-over-ONNX-Runtime
separation with a one-worker job manager (`include/StemSeparation/StemJobManager.h`), a model store
that never bundles a model and never fetches an unpinned one (`include/StemSeparation/StemModelStore.h`),
two interchangeable backends (in-process ORT when the SDK was found; `tools/stem_split_cli.py` in a
child process otherwise) and five registered tests (`OnnxRuntimeStemSeparatorTest`, `StemExportTest`,
`StemJobManagerTest`, `StemModelStoreTest`, `StemSplitPipelineTest`). What did not exist was any way
for a client to drive it: the only route was that CLI, outside the socket, plus a GUI-only clip action
(`src/gui/StemSplitController.cpp`). Seven ids close that gap:

- **`stem.get_state`** answers with the engine's own facts — the backend this build drives, whether it
  can run right now (and the reason when it cannot), the model file the store resolves, the model
  contract's constants (44100 Hz, the 343980-frame segment) and the fixed stem order. It reports
  **`realtime: false` and the 7.8 s lookahead**, because that is the truth: HTDemucs is a hybrid
  transformer that needs the whole segment as context, so no chunk fits an audio block and there is no
  live mode to expose. `stem.job_start` / `stem.job_status` / `stem.job_result` / `stem.job_cancel` are
  the offline job: start queues a separation of an absolute audio file and returns the id immediately,
  status polls state and progress, result writes `<stem>.wav` (drums, bass, other, vocals) as float32
  RIFF/WAVE with a sha256 per file, and cancel stops an outstanding job between inference segments.
  The separation runs on the job manager's own worker thread, so the control surface keeps answering —
  **including `control.ping` — while a job runs**, which the ctest measures rather than asserts from the
  source. `stem.model_get_state` and `stem.model_download` are the model store: the resolved path, the
  spec, whether it is pinned enough to fetch, and a download that **refuses an unpinned spec** — the
  default spec is deliberately unpinned in v1, so the default call is a typed refusal naming the model
  card, which is the "never bundled, always verified" policy working rather than a gap.
- **The feature is OFF in the default release configuration, and the group is honest about it.**
  `WANT_STEM_SPLIT` defaults to OFF (`CMakeLists.txt:120`), so a default build compiles none of the
  engine and registers none of these ids — the same rule the `telemetry.*`, `session.*` and `wasm.*`
  groups follow, and the A16 rows are guarded by the same macro so the registry and the contract table
  cannot disagree. Even a build with the option ON needs **the model present** before a job can start:
  models are never bundled with the product, and the refusal names the path it looked in and the model
  card to fetch from.
- **Proof.** ctest `ControlStemCommands` (`tests/control-stem-commands.py`), registered only when the
  feature is compiled in. It drives the REAL pipeline over `--control-socket` on the committed
  458-byte stub ONNX graph (`tests/data/stub-4stem-linear.onnx`) through the shipped CLI — no 166 MB
  download — and measures: the engine's own facts, every argument and state refusal (a relative path, a
  missing file, a 48 kHz file, a non-audio file, an unknown job id, a non-completed job, a format it
  does not write), the asynchrony (`control.ping` answered while the job runs), the four written stems
  (float32 RIFF/WAVE, the mix's own length, sha256 verified against the file), a re-write of the same
  directory, a deterministic cancel of a slowed job (`LMMS_STEM_CHUNK_DELAY_MS`, the separator's own
  test hook) and the model store's policy, with the reported SHA-256 cross-checked against the hash the
  script computes locally. A host with no python onnxruntime skips (`SKIP_RETURN_CODE 77`) rather than
  passing.
- **Stated limits, all in `docs/KNOWN-LIMITATIONS.md`:** 44100 Hz only (there is no resampler —
  SPEC-stem-split.md OQ-1 — and the refusal names the rate); the offline job only, never realtime; the
  source is a file, not a clip or a bus; `stem.job_result` writes output artefacts and does **not**
  materialise tracks (`StemTrackBuilder`, the GUI's own "split to stems" gesture, is not reachable from
  the socket in 0.3.0); the jobs live in the instance's memory and do not survive a reload; and
  `stem.model_download` **carries a declared bound** — a performing transfer runs on the control
  thread, so the surface does not answer until it finishes or fails (the defect
  `docs/RENDER-CHILD-WAIT.md:120-126` records for the child-process renders). The transfer's performing
  path is **not exercised by any registered proof** — CI has no pinned artefact to fetch — and only the
  refusal path is measured.
- **UI absence — one line: the whole `stem.*` group is drivable through the socket and only through the
  socket.** The one user-visible gesture that reaches this engine is the sample clip's **"Split to
  stems"** context action (`src/gui/clips/SampleClipView.cpp:124` →
  `StemSplitController::splitClipToStems`), which is a **different, pre-existing** code path: it takes
  its mix from a `SampleClip` a human selected, is available only when a display is, and no menu item,
  toolbar button or keybinding reaches a `stem.*` id. There is no job list, no progress surface for an
  agent-owned job, no model-manager UI, and nothing in the interface says the model is missing —
  `stem.get_state` is where that answer lives.

## The A16 contract table, and its histogram

The SPEC A16 classification table holds **284 rows** as this branch measures it:
**152 `true_inverse`, 21 `snapshot`, 7 `irreversible`, 104 `not_mutating`**, in the configuration this
build actually is (the telemetry client compiled in, no wasmtime). With the telemetry client
compiled out (`-DZENE_TELEMETRY=OFF`) the two `telemetry.*` rows leave with their commands, giving
**282 rows / 102 `not_mutating`** - which is the base
`ReversibilityContractTest::documentedHistogram()` carries, with the `#ifdef` guards ADDING the
telemetry group and the six `wasm.*` rows (three `snapshot`, three `not_mutating`, and only when the
wasmtime C API is on the find path) rather than writing one figure per configuration, because that is
what left one of them stale before. **The last figure a MERGED tree measured here was 283 rows**
(151 `true_inverse` / 21 / 6 / 105, the five-lane **wave-2** merge train's tip).
`030/automation-modes` moves it by its own delta, stated so the merge step can check it rather than
trust it: `automation.mode_set`'s stale refusal row leaves
`src/core/ControlReversibilityTablePassive.cpp` (-1 `not_mutating` - the command is a working verb
now, not a typed refusal) and two rows take its place in their own TU,
`src/core/ControlReversibilityTableAutomationModes.cpp`, joined with ONE entry (mode_set
`irreversible` - the mode is runtime state, not persisted and not journalled; record_mode_set
`true_inverse` - the clip is a `JournallingObject`). That is **+1 row / +1 `true_inverse` / +1
`irreversible` / -1 `not_mutating`** over the wave-2 measurement, and the merge tip re-takes the
measurement because the sibling lane `030/sample-accurate-automation` carries rows this branch does
not. The same build's live `control.commands` list answers **284** commands, which is the second and
independent instrument: the registry and the contract table are the same size, and no row names a
command that is not there. (284 = the wave-2 tip's 283 + `automation.record_mode_set`, the one command
this lane registers; `automation.mode_set` was already registered - as a refusal - and is the same id
working now.)

What the five wave-2 lanes added - each figure stated beside its own rows, and all five summing to
the measurement exactly:

* **`030/smf-tempo-export` (feature row 33) - +4 rows, +1 `true_inverse`, +3 `not_mutating`:**
  `interchange.smf_import` is the recorded-action `true_inverse` row (it replaces the tempo map);
  `interchange.smf_convention`, `interchange.smf_export` and `interchange.smf_read` write nothing
  outside the session - the export writes a file.
* **`030/undo-structural` (feature row 75) - +1 row, +2 `true_inverse`, -1 `irreversible`:** the new
  `track.move` is a `true_inverse` row, and `plugin.unload` MOVED out of the `irreversible` block
  because a removed device is now re-instantiated with its settings by one `control.undo` - the
  train's only re-classification, and the reason the `irreversible` column goes DOWN. Its four
  structural rows (`track.add`, `track.move`, `track.remove`, `plugin.unload`) live in their own
  table TU, `src/core/ControlReversibilityTableStructure.cpp`, joined into the action half so the
  block still reads as ONE `true_inverse` block with one row count. It is a disagreement with
  `A16-STATUS-MEASURED.md` that the rows themselves record.
* **`030/chord-track` (feature row 35) - +9 rows, +6 `true_inverse`, +3 `not_mutating`:** the three
  reads (`chord.get_state`, `chord.detect`, `chord.progression_list`) are `not_mutating`; the four
  track edits (`chord.set` / `chord.remove` / `chord.clear` / `chord.detect_to_track`) are
  recorded-action `true_inverse` rows and the two generators (`chord.track_write`,
  `chord.progression_generate`) are live-checkpoint `true_inverse` rows.
* **`030/project-archive` (feature row 38) - +3 rows, +1 `true_inverse`, +2 `not_mutating`:** the two
  inspectors of a project FILE (`project.missing_assets`, `project.hash_assets`) read and write
  nothing, and `project.relink` is the one writer, a recorded-action `true_inverse` row.
* **`030/host-chunking-wasm` (feature rows 82 and 73) - +1 row, +1 `not_mutating`:**
  `plugin.host_chunking` is read-only. The group's other two ids, `wasm.pool` and
  `wasm.render_offline`, are registered only when the wasmtime C API is on the find path - it is not
  in this configuration, so their rows are empty here and the figure above is unchanged by them. The
  one place this release computes a split is `plugin.host_chunking`'s own counters; that is data, not
  a row.


Every other figure of this shape below was measured on the branch that wrote it, or on an earlier
merge tip, and is kept as that lane's own record rather than as this tree's number:
`030/meter-surface` 231 (122 + 18 + 7 + 84), `030/linked-clips` 231 (123 + 18 + 7 + 83),
`030/telemetry-code` 228 (120 + 19 + 7 + 82), `030/record-inputs` 236 (121 + 20 + 7 + 88),
`030/pitch-stretch` 228 (121 + 18 + 7 + 82), and the three-merge tip this wave started from
227 (120 + 18 + 7 + 82) over the base 225 / 120 / 18 / 7 / 80. This page's rule is that the number
here is the merged measurement and never a sum of anybody's report - which is why the merge step
re-ran the test and rewrote this paragraph and that constant together. The measurement agrees with
the lanes' own deltas exactly, which is the check that it is a measurement and not a total:
225 base + 1 (telemetry) + 4 (meter) + 4 (linked clips) + 9 (recording) + 1 (pitch-stretch) +
4 (render presets) + 15 (note/scale) = 263, and the class columns add up the same way.

What the eight lanes added, in each lane's own words:

* **`030/meter-surface` (feature row 24) - +4 rows, +2 `true_inverse`, +2 `not_mutating`:**
  `meter.arm` and `export.set_loudness_report` as recorded-action `true_inverse` rows,
  `meter.get_state` and `meter.measure_file` as `not_mutating` inspectors.
* **`030/linked-clips` (feature row 6) - +4 rows, +3 `true_inverse`, +1 `not_mutating`:**
  `clip.link_create`, `clip.link_remove` and `clip.link_sync` are `true_inverse` on LIVE `Clip` /
  `MidiClip` checkpoints - the relation is the `link` attribute the clip's own element carries,
  written only when the clip is a member and reset to 0 by `Clip::loadClipEdits` when the attribute
  is absent, so a checkpoint taken before a *first* link restores "unlinked" exactly and one taken
  before a mirror restores the members' note lists - and `clip.link_get_state` is `not_mutating` (it
  reads the groups, their members and each member's content verdict, and writes nothing).
  `docs/LINKED-CLIPS.md` §4 is the argument, and the one `control.undo` that takes the whole group
  back is asserted by `ClipLinkTest::undoRestoresEveryMemberOfTheGroup()`.
* **`030/record-inputs` (feature rows 14/16/64) - +9 rows, +1 `true_inverse`, +2 `snapshot`,
  +6 `not_mutating`:** `record.arm_track` and the re-classified `track.set_arm` (`snapshot`, each
  with a paired-command inverse), `record.input_set` (`true_inverse`, the config write's previous
  plan) and seven `not_mutating` rows (`record.get_state`, `record.disarm_track`,
  `record.disarm_all`, `record.input_get_state`, `record.retro_capture_arm`,
  `record.retro_capture_status`, `record.retro_capture_to_take`), while `track.set_arm` LEAVES
  `not_mutating` because it is no longer one of the refusals.
* **`030/telemetry-code` (CODE-6) - +1 row, +1 `snapshot`:** `script.set_memory_budget`.
* **`030/pitch-stretch` (feature row 30) - +1 row, +1 `true_inverse`:** `warp.stretch`, mechanism =
  the clip's own journal checkpoint, because the stretch mode is the `stretch` attribute of the
  clip's `<warp>` element.
* **`030/render-presets` (feature rows 70/71) - +4 rows, +3 `true_inverse`, +1 `not_mutating`:**
  the render/export preset store's three recorded-action rows and its one read.
* **`030/note-scale-verbs` (board task #648) - +15 rows, +11 `true_inverse`, +4 `not_mutating`:**
  the note random/slide/transform verbs, the `scale.*` group and the registry's first `device.*`
  group. The classes are stated on each row in `src/core/ControlReversibilityTableNoteScale.cpp`,
  which holds exactly those fifteen.
* **`030/stem-surface` (feature row 26) - +0 rows in this configuration:** its seven
  `not_mutating` rows are guarded by `WANT_STEM_SPLIT` and the release does not ship them; see
  below.

The seventeen rows this train's three merges added are the verb wave's four
(`clip.trim` / `clip.slip` / `note.probability_set`, `true_inverse`; `render.stems`, `not_mutating`),
the plugin scan-cache and crash-reporter groups' ten (two `snapshot` - the two quarantine writers, whose
recorded inverse is a bounded cache revision - three `irreversible` - `plugin.rescan` and the crash
reporter's two writers, each with a named fallback - and five `not_mutating` rows: the three scan
reads, `crash.list_reports` and its refusal `crash.upload_report`) and the auto-mastering
group's three (`mastering.run`, `true_inverse` through a recorded action checkpoint; the two
inspectors, `not_mutating`) - `+4 true_inverse / +2 snapshot / +3 irreversible / +8 not_mutating`
against the base this page carried before the train, 208 / 116 / 16 / 4 / 72. The figures this page
carried before this train were lane-local and
incomparable - the fold quoted 164, the MIDI clock lane 167, the chain-preset lane 170 and the folder
tracks lane 173, each measured on its own base - and one of them (165 rows against 167 ids) was
internally impossible, which is the reason the number on this page is now the merged measurement and
never a sum of anybody's report.
The four rows the 0.3.0 verb wave added are `clip.trim` and `clip.slip` (`true_inverse` on a LIVE
`Clip` checkpoint: both write only attributes their clip type serialises and reads back
unconditionally - `pos`, `len`, `off`, `autoresize` - which is what makes a checkpoint taken before a
*first* edit reversible; neither verb authors `SampleClip`'s `srcin`/`srcout` window precisely
because that window has no reset-on-absence) and `note.probability_set` (`true_inverse` on the owning
`MidiClip`'s checkpoint: `Note::loadSettings` reads the optional `prob` attribute with a default of
1, so restoring a pre-first-edit state brings the note back to "always plays") -
`+3 true_inverse`. `render.stems` is the fourth and is **`not_mutating`**: it writes one output file
per unmuted track through the shipped `exportstems` CLI in a child process, so no project state is
touched and there is nothing for a checkpoint to capture - `+1 not_mutating`. `docs/STEM-EXPORT.md`
and `docs/KNOWN-LIMITATIONS.md` carry the contract and the declared render bound.
**The seven `stem.*` rows are NOT in the 284 above, and that is the point:** the offline
stem-separation group (feature row 26, board task #653) is compiled only when `WANT_STEM_SPLIT=ON` -
**OFF in the default release configuration** this page describes - so its seven `not_mutating` rows
(`stem.get_state`, `stem.job_start`, `stem.job_status`, `stem.job_result`, `stem.job_cancel`,
`stem.model_get_state`, `stem.model_download`) leave the table exactly when its ids leave the registry,
which is the rule the six `wasm.*` rows already follow in the other direction. A build with the option
on carries **291 rows / 111 `not_mutating`** - measured, not derived: the seven-row guard was added to
`ReversibilityContractTest::documentedHistogram()` in the same commit as the rows, and that test passes
against a `WANT_STEM_SPLIT=ON` build of this tree, which is only possible if the table really has
283 + 7 rows and 105 + 7 `not_mutating` ones. So no figure on this page has to be rewritten for a
configuration the release does not ship. All seven drive one offline engine, write output artefacts
(four stem WAVs and a checksum-verified model file) and record no project state: a job is not a
document, and a written stem is an output.
The nine rows the folder-tracks merge added are:
`track.folder_set_collapsed` and `track.set_pinned` are `true_inverse` on a live Track checkpoint (both
flags are part of the folder's own `<trackfolder>` element and are reset on absence, so the checkpoint
is a real inverse), `track.set_folder` / `track.set_routing` / `track.visibility_set_save` /
`track.visibility_set_apply` / `track.visibility_set_remove` are `true_inverse` **recorded actions**
(the parent relation lives on the child's element and `track.set_routing` writes every child's own
mixer channel, so no single live checkpoint covers either; a named visibility set is not a
`JournallingObject` at all), and `track.folder_get_state` / `track.visibility_set_list` are
`not_mutating` inspectors - `+7 true_inverse / +2 not_mutating`. `ReversibilityContractTest` asserts both
sets, so a row added or moved between classes cannot ship with this page quoting the old split. The
three `midi.retro_capture_*` rows the retrospective MIDI capture merge added are
`midi.retro_capture_to_clip` as `true_inverse` (a live `Track` checkpoint, the `clip.add` shape) and
`midi.retro_capture_arm` / `midi.retro_capture_status` as `not_mutating` (a mode flag and a
read-only inspector) - `+1 true_inverse / +2 not_mutating`. The three rows the MIDI clock merge added
are `clock.master_set` (`true_inverse`, a recorded action: the enabled flag and the port subscription
are a bounded pair), `clock.slave_set` (the train's **`snapshot`** row - tempo-follow makes the slave
write `Song::setTempo` whenever the measurement leaves its dead band, and a trajectory of project-state
writes is not one state a bounded record restores) and `clock.get_state` (`not_mutating`) -
`+1 true_inverse / +1 snapshot / +1 not_mutating`. The six rows the chain-preset merge added are
`chain.save` / `chain.apply` / `chain.rename` / `chain.remove` (`true_inverse` recorded actions: the
preset store is a file tree OUTSIDE the project, the user preset tree's `chainpresets/`, which no Song
checkpoint carries) and `chain.list` / `chain.get_state` (`not_mutating` inspectors) -
`+4 true_inverse / +2 not_mutating`. The 155-row figure this page carried before an earlier merge was
the pre-punch table's, and the 157 one incoming lane's own page quoted was measured on that lane's
base - neither is any merged tree's, and this page states only measurements of the tree it ships
with. At 0.2.1 the same four counts were 30 / 5 / 3 / 36 over 74 rows
(`docs/RELEASE-NOTES-v0.2.1-alpha.md`) - that record is left as written.

The seven rows the 0.3.0 groove lane added are `groove.list` (one `not_mutating`), `groove.apply` and
`groove.quantize` (live-checkpoint `true_inverse` rows: a clip edit reverses through the MidiClip's
own journal checkpoint) and `groove.extract` / `groove.set` / `groove.remove` / `groove.rename`
(recorded-action `true_inverse` rows: the pool is project state the Song's journal checkpoint does not
carry, so the recorded step writes the captured `<groove-pool>` element back). `docs/GROOVE-POOL.md`
section 5 is the argument for each.

The fourteen rows the `vca.*` lane added (OWNER-31 item 11, phase-locked multitrack edit groups) are
`+12 true_inverse / +2 not_mutating`. The two reads - `vca.list` and `vca.get_state` - are
`not_mutating` inspectors and live with the other passive rows; the twelve mutating commands are ALL
`true_inverse`, and none of them through a checkpoint *of the group*: a `VcaGroup` is a `QObject`
owned by the Mixer rather than a `JournallingObject` with an id on the journal's object map (its
`<vcagroup>` element is part of the MIXER's serialized state, and a Mixer checkpoint would destroy
and recreate every channel). Six rows lean on a live checkpoint of a MODEL instead - the group's
fader (`vca.set_gain`), its mute (`vca.set_mute`), the composite solo step
(`vca.set_solo`: the group's solo flag, every other group's flags and every channel's mute as ONE
undo step, the shape `track.set_solo` uses, with the same stated limit that
`MixerChannel::m_muteBeforeSolo` is transient and not restored) - and, for `vca.edit_move`, a live
Clip checkpoint per moved clip, merged by the registry into one step. The other six are recorded
ACTION steps for state that is not a model at all: a name (`vca.rename`), a membership list in either
direction (`vca.assign`, `vca.unassign`, `vca.track_add`, `vca.track_remove`), a lock flag
(`vca.set_phase_lock`), and a group's existence (`vca.create`, `vca.remove` - whose delete is
EXACTLY reconstructible, unlike `mixer.remove_channel`, because a group holds scalars, flags and two
id lists and nothing else in the mix refers to it). The two limits worth repeating are in the rows
themselves: the transient `m_muteBeforeSolo` above, and `vca.edit_move`'s index-derived clip ids,
which are why its inverse is the checkpoint and not a replayed `clip-<n>` id.
`docs/VCA-EDIT-GROUPS.md` is the lane's report and carries the argument for each.

The three rows the 0.3.0 MIDI clock lane added are the group's whole surface, and only one of them is
`true_inverse`: `clock.get_state` is a `not_mutating` inspector; `clock.master_set` is a
recorded-action `true_inverse` row (the enabled flag and the port subscription are a bounded pair a
recorded undo step restores exactly); and `clock.slave_set` is a **`snapshot`** row - the
configuration it sets (mode, tempo-follow flag, source port, drift bound) is restored exactly, but
turning tempo-follow ON makes the slave write `Song::setTempo` every time the measurement leaves the
dead band, and a *trajectory* of project-state writes is not one state any bounded record can restore.
The row says exactly that, the transaction reports the tempo the command found, and the fallback is
`transport.set_tempo`. Claiming `true_inverse` here would be claiming that one Ctrl+Z puts the tempo
back, which it does not - see the row's own text in
`src/core/ControlReversibilityTableSnapshot.cpp`.

The eleven rows the routing-surface lane added are the pdc / routing / bus / port groups (feature rows 27-29)
and the mixer group's four routing verbs: `pdc.report`, `routing.get_state`, `bus.list` and `port.get_state`
are `not_mutating` inspectors; `bus.create`, `mixer.route_to`, `mixer.send_to`, `mixer.sidechain_to` and
`mixer.route_remove` are `true_inverse` **recorded actions** (a created channel has no before-state, and a
`MixerRoute` / `MixerSidechainRoute` is not a `JournallingObject` — the send lists are not project-journalled
state — so the recorded step deletes the route it created, or writes the captured amount and pre-fader flag /
tap point back); `bus.remove` is a `snapshot` with no automatic replay (the class `mixer.remove_channel` has,
for the same reason: nothing creates a channel WITH state) and `port.set_pin` is a `snapshot` whose inverse
**is** a command (one pin is one bool in the processor's `<pins>` element, and there is no
`JournallingObject` behind an `AudioPortsModel`, so the recorded inverse is `port.set_pin` with the previous
value and `control.undo` dispatches it through `applies: command`) - `+5 true_inverse / +2 snapshot /
+4 not_mutating`. `src/core/ControlReversibilityTableRouting.cpp` holds the rows as one group, whatever their
class, and `reversibilityRowTable()` joins them exactly as it joins the folder-tracks group's.

The ten rows the scan-cache + crash-reporter lane added are the `plugin.*` scan group (feature row 46) and the
`crash.*` group (row 54): `plugin.scan_cache_get_state`, `plugin.scan_cache_list`, `plugin.scan_cache_lookup`,
`crash.list_reports` and `crash.upload_report` are `not_mutating` (`crash.upload_report` is the
`crash.upload_report` shape - declared mutating, refused by name, so no write and no transaction);
`plugin.scan_cache_quarantine_add` and `plugin.scan_cache_quarantine_remove` are `snapshot` rows whose inverse
**is** a command (the scan cache is a JSON file outside the project and is not a `JournallingObject`, so the
recorded inverse is the paired verb with `applies: command`, exactly as `browser.tag.add` /
`browser.tag.remove` are - and the removal carries the entry's REASON, captured before the write, because no
other state reconstructs it); and `plugin.rescan`, `crash.acknowledge_report` and `crash.discard_report` are
`irreversible` with a named fallback (a scan replaces a file's fingerprint record and nothing puts the previous
one back; nothing removes the reporter's `offered` sentinel; nothing writes a report from a caller's bytes) -
`+2 snapshot / +5 not_mutating / +3 irreversible`. `src/core/ControlReversibilityTableScanAndCrash.cpp` holds
the rows as one group, whatever their class, and `reversibilityRowTable()` joins them for the same reason it
joins the routing surface's: the passive block and the live block are both at the file-length cap.

## Modulation layer: modulators that drive a set of parameters, and per-note expression (`modulator.*`, `note.expression.*`) — added 2026-09-13

- **New: a modulation layer.** A *modulator* is a timeline-locked LFO (shape, rate in Hz, phase,
  polarity) that drives a **set** of parameters at once, each by its own **depth**. Ten ids:
  `modulator.get_state`, `modulator.create`, `modulator.remove`, `modulator.rate_set`,
  `modulator.target_set`, `modulator.depth_set`, `modulator.target_remove`, and `note.expression_set`,
  `note.expression_get`, `note.expression_clear` for per-note expression.
- **A depth is a fraction of the target's own range, and the write is RELATIVE.** `depth` is in
  `-1..1` and means a fraction of that parameter's own `min..max`, the same unit the rack macros use
  and for the same reason (the engine defines a parameter's range; recording its numbers would pin
  the assignment to one build). What differs is what the fraction is applied to: a macro *replaces* a
  parameter's value, a modulator *adds* to it —
  `written = clamp(base + depth * output * (max - min), min, max)` — so two parameters with different
  ranges and different current values move by the same fraction of their own range. That is what makes
  the amount independent of each parameter's own absolute value. `docs/MODULATION.md` §2 records the
  decision and the comparison with the macro lane.
- **A modulator really reaches the audio path.** `Song::processModulation()` runs once per audio block
  beside the tempo map's follower, reads the layer through a seqlock (`ModulationLayerPublisher`, the
  `TempoMapPublisher` precedent), and writes each resolved route's target once. An empty layer returns
  before copying anything, so a project that uses no modulator runs the block it has always run, and
  `ModulationLayerTest` measures both halves: zero allocations on the block path, and a no-op on a
  layer with no modulator, no resolved route, an inactive modulator or a zero depth.
- **A route's address is the rack macro's address**, resolved through the effect's own parameter list
  (channel, chain, effect, parameter display name), so a modulator route and a macro target cannot
  disagree about what a parameter name means. A route that does not resolve at bind time is refused,
  typed — and a route whose device disappears later is reported as unresolved by `modulator.get_state`
  rather than silently miswritten.
- **Every edit restores the parameters first.** Each `modulator.*` write rebuilds the layer's resolved
  write set after handing every target back to the base it was modulated around, which is what makes
  re-capturing a base idempotent: editing a modulator can never bake a modulated value into it.
  `Song::stop()` does the same, so a stopped song does not leave a parameter parked where the last
  block put it. The layer is saved with the project (a `<modulation-layer>` element inside `<song>`)
  only when it holds a modulator, so a project that never used one re-saves byte-identically.
- **Per-note expression is now reachable.** `note.expression_set` / `get` / `clear` drive the per-note
  MPE fields `#601` already stores on a `Note` and serializes as the optional `mpepitch` /
  `mpepressure` / `mpetimbre` attributes — they are not a second expression store, and the inverse is
  a `MidiClip` checkpoint. **All three axes are applied by playback** (pitch as a frequency ratio,
  pressure and timbre as MIDI events on the note's own member channel, task #649) — a note that
  carries no expression sends neither event, so nothing that did not use MPE changes. **Proof:**
  `MpePlaybackTest` (registered ctest) renders ONE audio block of a note through the real playback
  path with the expression and the same block without it and asserts the level moves by the ratio
  the expression asks for — its subject is the in-tree MIT test instrument
  `tests/src/plugins/MpeTestConsumer.cpp`, because no built-in synthesiser consumes channel
  pressure or CC74; a hosted instrument or a MIDI output port is where a musician would hear them.
- **UI absence — one line: modulators and per-note expression are drivable through the socket, not
  from the interface.** Nothing in `src/gui/` creates, draws or edits a modulator or a note's
  expression. `docs/KNOWN-LIMITATIONS.md` carries the same sentence.
- **Proof:** four registered ctests. `ModulationLayerValueTest` (the four LFO shapes, the source
  validation, the layer's bounds — values only, no engine), `ModulationLayerTest` (the target
  resolver, the relative write against two ranges, the clamp, the no-op paths, **0 allocations over
  64 blocks**, the base restore, the save/load round trip), `ControlModulatorCommandsTest` (the ids
  with their schemas, the A16 classes, the typed refusals, `bindDriveUndoAndRebind` — create, bind,
  apply a real block, `control.undo`, and re-bind) and `ControlNoteExpressionCommandsTest` (the
  `note.expression.*` round trip with its checkpoint). Every mutating call records its SPEC A16
  class (`true_inverse`: an action checkpoint for the layer, a `MidiClip` checkpoint for a note's
  expression).
- **Stated limits.** Modulation is applied **once per audio block** (about 11 ms at the default
  period), not sample-accurately; the source is an **LFO only** (no envelope follower); targets are
  device parameters inside a mixer channel's rack chains, so a route cannot name the Song's own master
  gain or an instrument's parameters in this release; and while a modulator is active the parameter's
  own control is taken over — the value you see is the base, and the modulator's offset is on top of
  it until the modulator is deactivated, removed or `control.undo` takes the edit back.

## Automation modes: off / read / touch / latch / write, made drivable and observable (`automation.mode_set`, `automation.record_mode_set`) — added 2026-09-15

The engine's mode state machine (`AutomatableModel::AutomationMode`) is now selectable through the control
surface: `automation.mode_set` sets a parameter's mode (off / read / touch / latch / write) and
`automation.record_mode_set` toggles its clip's record flag. The default is Read, which is what every
existing project already behaves as. `off` is a mode of its own, not a second spelling of `read`: an off
control ignores its written curve (the engine's apply pass skips it, so the manual value stands) and writes
nothing — `AutomationModesTest::testOffIgnoresTheAutomationAndWritesNothing` pins that difference, with a
Read leg as its sensitivity control. A mode change is **observable, not only issuable**: `automation.get_state`
reports each parameter's `mode` (one spelling function serves both directions,
`control::automationModeName`) and each clip's `recording` flag, and `automation.mode_set` answers with
`mode`, `mode_before` and `changed`.

The no-destruction property is pinned twice, in `tests/src/core/AutomationModesTest.cpp` (registered at
`tests/CMakeLists.txt:56`) and through the command surface in
`tests/src/core/ControlAutomationModesTest.cpp::readRideThroughTheSocketCannotTouchTheRecordedAutomation`
— a curve recorded with `automation.add_point`, the mode set to `read`, the control ridden with
`plugin.param_set` while the harness drives `Song::processNextBuffer()`, and the clip compared node for node
afterwards — each paired with a leg that runs the identical harness where a write IS expected (a Touch pass
in the engine test; `write` mode through the socket), so the comparison cannot pass by the harness never
writing anything. Both command-surface binaries are on `tests/CMakeLists.txt`'s `LMMS_TEST_PLUGIN_DIR`
list, because the ride needs a real instrument parameter and a build without the modules falls back to a
parameterless `DummyInstrument`.

The mode is runtime state: not persisted in the project file and not journalled, so a reload resets every
control to Read with no trim and a mode change has no undo. Only the mixer fader is wired to a touch
gesture; pan, sends and plugin-parameter knobs would each need widget hooks, so touch and latch have nothing
to take hold of through the socket yet (write needs no gesture and is fully drivable). Write mode does not
erase the un-passed remainder of the clip. The Read render's byte-identity (the status quo this feature must
not move) is held separately by `tests/automation-modes-render-check.sh`, which re-renders a demo project and
compares the data chunk against a base-commit binary or a recorded hash.

## Session sync: two instances on one tempo and one beat (`link.*`) — added 2026-09-13

- **New: a session. Two Zene instances on one machine (or on one network segment) agree on a tempo and a
  shared beat phase, and either one can drive the other.** Instance A declares a tempo - deliberately, with
  `link.set_session_tempo`, or simply by changing its tempo with `transport.set_tempo` - and instance B,
  driven by nothing but its own socket, plays that tempo. No client relays between them: the instances
  announce themselves to each other every 100 ms over UDP multicast on `224.76.78.75:20808`, the group
  Ableton Link itself uses.
- **Control surface:** `link.get_state` (whether sync is on, the peers and their ids, the session tempo and
  who declared it, the shared beat and its phase inside the quantum, this engine's own tempo and phase and
  the error between the two, and whether announcements can travel at all), `link.set_enabled` (join or leave
  the session), `link.set_quantum` (1..64 beats, 4 by default), `link.set_start_stop_sync`, and
  `link.set_session_tempo`. Every mutating call records its SPEC A16 class (`true_inverse` — an action
  checkpoint on the engine's own journal stack restores the previous value) and every refusal is typed.
- **The engine's own tempo is what makes an ordinary tempo change reach the session.** The model compares
  the Song's tempo with the value it last applied; anything else is a local edit, and it is announced. So a
  tool that only knows about `transport.set_tempo` drives the session without learning a new command.
- **The shared beat is continuous across a tempo change.** The timeline is re-anchored *before* the new tempo
  takes effect, so no peer's phase jumps; and a packet carries the sender's beat at its send time, which the
  receiver advances by the datagram's actual transit time on the shared monotone clock rather than guessing a
  round trip.
- **What this is NOT, in the code and not only in this file.** The model is `zene-link-style`: Ableton Link's
  *semantics* without the Ableton Link *library*, which this build does not vendor. `link.get_state` carries
  an `interop` block that says so, with the reason. The licence question was settled before the code was
  written and the finding is recorded in `docs/LINK-SYNC.md` §1: Ableton Link's own `LICENSE.md` is
  **GPL-2.0-or-later** (its final paragraph offers a *separate* commercial licence; it does not qualify the
  GPL grant), so **vendoring it is permitted for this GPL-2.0-or-later product and real Link
  interoperability is a follow-up lane rather than a licence-blocked one**. That is a deliberate scope
  decision, and it is the reason a Link-enabled third-party application cannot join this session today.
- **UI absence — one line: session sync is drivable through the socket, not from the interface.** Nothing in
  `src/gui/` draws, edits or reads a session tempo, a peer list or a beat phase. `docs/KNOWN-LIMITATIONS.md`
  carries the same sentence.
- **Stated limits.** `start_stop_sync` is announced and reported but never acted on; the play head is *not*
  repositioned onto the session grid (the phase and its error are measured and reported, and the tempo is
  applied); Windows reports the transport unavailable with the reason; and the shared clock assumption holds
  for instances on one host or on hosts whose clocks agree (`docs/LINK-SYNC.md` §5 lists all six).
- **Proof:** the registered ctests `ControlLinkCommandsTest` (the five commands, the typed refusals, the A16
  inverses through `control.undo`, and the revision/phase arithmetic computed independently) and
  `ControlLinkSync` — **two real binaries, one session, one driving the other's tempo and phase through
  `--control-socket`**, with the phase compared against elapsed wall time. `ControlLinkSync` is registered
  `RUN_SERIAL` (the multicast group is shared state by design) and reports ctest *Skipped*, never *Passed*,
  when the host cannot carry announcements.

## Freeze / bounce-in-place (`bounce.in_place`, `freeze.*`) — added 2026-09-13

- **New: a track's output can be rendered to audio and played in place of its clips.** `bounce.in_place`
  renders ONE track's own contribution — its devices, fader, pan and sends, which is what the engine's stem
  export already means by a stem — to a WAV and returns the file, its frame count and its sha256; with
  `start`/`end` in ticks the file covers that region instead of the whole track. The render runs in a child
  process against a serialised copy of the session with every other track muted (the reason
  `ControlCommandsProject.cpp` gives for `render.render`: rendering in-process would drive this instance's
  audio engine and leave it unable to quit cleanly), so `bounce.in_place` changes nothing in the session and
  records no transaction.
- **`freeze.track` makes the track play the render INSTEAD of its clips, and `freeze.unfreeze` puts the
  source back.** The substitution is in the engine, not in the document: `InstrumentTrack::play` and
  `SampleTrack::play` return the take for every pass inside its window and schedule none of the track's own
  playback, which is what "the source is disabled" means. The take carries the track's devices, fader, pan and
  sends, so it is summed at the mix level and NOT through the track's chain a second time.
- **The frozen state is project state and survives save/load.** A frozen track serialises the render's path and
  the window it covers as four attributes on the track's own element (`frozenAudio`, `frozenStart`,
  `frozenEnd`, `frozenMuted`) — attributes and not a child element, for the reason `SPEC-stable-ids.md` §3.1
  records for the track id: `Track::loadTrack` turns an unrecognised child element of `<track>` into a real
  clip. (A child marked `metadata="1"`, the pattern the take lanes use, cannot carry state at all:
  `DataFile::write` calls `cleanMetaNodes()`, which removes every marked element from a saved project — measured
  on this tree.) A track element with no `frozenAudio` attribute is NOT frozen, and that reset-on-absence is
  what makes one `control.undo` take a freeze off. The audio is opened on the loading (control) thread, never on the
  audio thread, and a take whose file has moved is still frozen state — reported as `frozen: true` with
  `frozen_audio_ready: false` by `track.get_state` rather than silently dropped.
- **`freeze.region` freezes one tick range.** The region is rendered and the take plays inside it, the source
  keeps playing outside it, and the clips that START inside the region are muted — recorded by their ticks in
  the same element, so `freeze.unfreeze` unmutes exactly those and nothing else (a clip the user had muted
  themselves stays muted). A clip that starts before the region and runs into it is NOT muted, because that
  would silence audio outside the region: it is named in the command's `overlapping_clips` and sounds twice
  inside the region.
- **Control surface:** `bounce.in_place`, `freeze.track`, `freeze.region` and `freeze.unfreeze` — with
  argument/result schemas and SPEC A16 reversibility metadata. `bounce.in_place` is `not_mutating` (an output
  artefact, like `render.render`); the three `freeze.*` verbs are `true_inverse` (a live Track checkpoint; the
  take and the mute record are both part of the track's own serialized state).
- **A frozen track's own length accounts for its take.** `Track::length()` floors on the take's end, because a
  region freeze mutes the clips inside it and the export path skips a muted clip when it measures the song —
  without that floor a render of a project whose last clips were frozen would stop before the take's audio.
- **UI absence — one line: freeze and bounce-in-place are drivable through the socket, not from the
  interface.** Nothing in `src/gui/` renders a track, marks it frozen or plays a take.
  `docs/KNOWN-LIMITATIONS.md` carries the same sentence, plus the stated limits (region overlap, the 44.1 kHz
  re-sample, the take bypassing the track's chain, and a moved take reporting itself as not ready).
- **Proof:** the registered ctest `ControlFreezeCommandsTranscript` (`tests/control-freeze-commands-transcript.py`)
  drives a real instance over `--control-socket`: it bounces a track and a region and checks each returned file's
  frames and sha256 against the bytes on disk, freezes the track and requires `track.get_state` to report the
  take with its audio loaded, then **DELETES the source notes and requires the frozen session to still render
  audio within 6 dB of the unfrozen reference** — with the notes gone the source cannot make a sound, so silence
  there would mean the take never played. It then saves and reopens the project (requiring `frozenAudio` in the
  written file, and audio again after the reload), takes the freeze back off with `freeze.unfreeze` and with one
  `control.undo`, freezes a REGION and requires it to mute exactly the clip inside it **and that region's bar to
  still sound in a render** (its source is muted, so only the take can make it), and drives every typed refusal.
  Its socket wrapper, recorder and WAV measurements live in `tests/freeze_bounce_evidence.py` (the split
  `tests/link_sync_evidence.py` made, for the same Gate 7 reason).
## MCP tooling: every registered command is an MCP tool (`zene_*`) — added 2026-09-13

- **New: the bridge's coverage of the command surface is a registered assertion, not a promise.** The
  `zene-control` bridge generates one MCP tool per id it reads from a live instance's
  `control.commands_list` (`tools/mcp-zene-control/zene_control/registry.py`), and the registered ctest
  `ControlCommandsSnapshot` (`tests/control-commands-snapshot.py`) now fails on **any** id this binary
  registers that the bridge offers no tool for — with the instance answering, with no instance and an empty
  state directory, and with a stale cache planted in it. The idle modes agree because the bridge now serves
  the **freshest** readable offline copy (`registry.rank_offline_bundles`) instead of the old cache-first
  order, which had let a 70-id 0.1.0-alpha cache leave **74 ids** of this tree's surface unreachable while
  the committed snapshot was current (`docs/COVERAGE-MATRIX-2026-09-13.md` §4.4). Measured by the test itself on
  the merged tip of THIS train (platform defects + folder tracks + retrospective capture + MIDI clock +
  chain presets together): **185 ids registered, 185 exposed live, 185 exposed offline** (187 tools with the
  two bridge-owned ones), **0 missing and 0 extra** in both directions in all three modes — live, empty state
  directory, and with the stale cache planted and passed over. The 164 the figure read before this train was
  the freeze/groove/MCP/punch tip's own measurement.
  **The five lanes of this train add 21 ids to that figure:** folder tracks (10:
  `track.set_folder`, `track.set_routing`, `track.folder_set_collapsed`, `track.set_pinned`,
  `track.folder_get_state` and the four `track.visibility_set_*` verbs), retrospective MIDI capture (3:
  `midi.retro_capture_arm`, `midi.retro_capture_status`, `midi.retro_capture_to_clip`), MIDI clock (3:
  `clock.get_state`, `clock.master_set`, `clock.slave_set`) and chain presets (6: `chain.save`,
  `chain.list`, `chain.apply`, `chain.rename`, `chain.remove`, `chain.get_state`); the platform-defects lane
  adds none. The snapshot is regenerated ONCE, at the end of the train, from a live instance of the merged
  build — at **185 ids** — and `ControlCommandsSnapshot` passes against it with 0 missing and 0 extra in all
  three modes. The generator is invoked with an explicit `--lane` (this worktree, so `lane_head` names a
  commit on `release/0.3.0`) and `ZENE_CONTROL_BINARY` (so `binary_sha256` names the exact executable that
  answered); a default invocation records `lane` = `tools/zene-pa-agentctl`, a path in no worktree, with
  `lane_head` null — which is what one lane's committed snapshot recorded and what this train repairs. Like
  every figure in this section it is a measurement of ONE tip; this one is the tip it ships in.
- **UI absence — one line:** none of this is in the interface; the tool list exists only through the MCP
  bridge over a control socket. **And the limit, stated plainly:** a Hermes session reads the bridge from the
  registration in `~/.hermes/config.yaml`, which points at a scratch copy outside this repository; until that
  entry is re-pointed at this tree's `tools/mcp-zene-control`, the session's offline list is the stale 70.
  See `docs/KNOWN-LIMITATIONS.md`.

## Punch in/out (`transport.punch_*`) — added 2026-09-13

- **New: a punch region on the transport.** `transport.punch_set` sets the tick range `[start, end)` that
  capture is gated to and arms it (or sets the range with `"enabled": false`), `transport.punch_clear` disarms
  and forgets it, and `transport.punch_get_state` reads the range, the arm flag and `punch_active` — the gate's
  own answer at the current play position. The region lives on `Timeline`, the design's own host for a punch
  range (`docs/CLIP-CAPTURE-DESIGN.md` slice B), so it is written with the project as `punch0pos` / `punch1pos`
  / `punchstate` on the `<timeline>` element — **only when it is set or armed**, so a project that never punched
  re-saves exactly the bytes it always had — and `Timeline::loadSettings` clears it when the attributes are
  absent, which is what lets a checkpoint taken before the first punch take the region back off.
- **Control surface:** `transport.punch_set` and `transport.punch_clear` are `true_inverse` (a live `Timeline`
  checkpoint; the timeline is a `JournallingObject` with its own id), and `transport.punch_get_state` writes
  nothing. The ids keep the `transport.` prefix the tempo map's half of the group uses — the region belongs to
  the transport, so an agent finds it where it finds the transport.
- **UI absence — one line: punch in/out is drivable through the socket, not from the interface.**
  Nothing in `src/gui/` draws a punch ruler, a region handle or a punch toggle.
- **Stated limit, in the same place as the claim:** 0.3.0 ships the region and the gate
  (`Timeline::punchCapturesAt()`), and **does not wire the audio-side capture gate** — this build has no
  capture path to gate (ALSA records nothing; the two-track prototype is fed by tests), so a gate here would be
  a change no test could exercise. `docs/KNOWN-LIMITATIONS.md` carries the sentence and the reason.
- **The defect the round trip found, and the fix.** The first version of these verbs used
  `song->getTimeline()`, the no-argument accessor. `Song::m_playMode` is `PlayMode::None` until
  playback starts, so that accessor addresses `m_timelines[None]` — a **different** timeline from the
  one the project carries (`Song::saveProjectFile` writes `getTimeline(PlayMode::Song)` and
  `Song::loadProject` restores that one). The region was therefore real and drivable and tested
  correctly at every boundary, and **vanished on the first save**: the punch-region transcript caught
  it, because it reads the `punch0pos` attribute out of the saved file rather than trusting the
  reply. The verbs now address the SONG transport explicitly (`songTransport()`), which is also the
  transport a recording runs on (`Song::playAndRecord()` sets the mode to Song). This is the class of
  defect the four-part scope contract exists to catch: an engine feature that works everywhere except
  where the user will need it.
- **Proof:** the registered ctest `ControlPunchTranscript` (`tests/control-punch-transcript.py`) drives a real
  instance over `--control-socket`: it sets the region, then **asserts the gate on both sides of both
  boundaries** (true at `start`, true inside, true at `end - 1`, false at `end`, false past it, false before
  it), requires the range to survive `project.save` / `project.open` after the session has been moved
  elsewhere, reads the three attributes out of the saved file rather than trusting the reply, requires a
  session that never punched to carry NO punch attribute at all, and takes a first-ever region back off with one
  `control.undo`.

## Recording crash recovery (`record.*`) — added 2026-09-13

- **New: a recording in progress is journalled, and the next start can recover it.** A capture writes a small
  side file beside its take (`<take>.rec-journal`: the take, the sample rate, the frames the disk-writer had
  flushed and when) through `record.journal_begin` / `record.journal_update` / `record.journal_finish`, and
  `TrackRecorder::arm()` / `disarm()` are wired to the same journal — so a **clean stop leaves no journal**,
  which is what makes "there is a journal" and "the capture died" the same fact. After an abnormal exit,
  `record.recovery_get_state` finds the interrupted captures and reports each one's `frames_journalled`,
  `frames_in_file` (measured from the take's own RIFF header, **falling back to the file's real byte length**
  when a crashed header was never updated) and `frames_recoverable`; `record.recovery_restore` takes an offer,
  and `record.recovery_discard` refuses one.
- **Control surface:** the three journal verbs and `record.recovery_restore` are `snapshot` (the inverse is the
  paired command, dispatched by `control.undo` — a side file is not project state, so no checkpoint can hold
  it); `record.recovery_discard` is `irreversible` and `control.undo` fails typed, naming the fallback;
  `record.recovery_get_state` writes nothing.
- **THE BOUND, stated rather than implied.** **Guaranteed recoverable is `min(frames the journal recorded,
  frames the take's file holds)`** — the reported number can never promise audio that is not on disk. **NOT
  recoverable:** the audio written after the journal's last update (the journal lags by at most one second of
  audio, `RecordingJournal::UpdateIntervalFrames`) and up to **65536 frames** still in the recorder's ring
  buffer when the process died — audio that never reached a file. `include/RecordingJournal.h` is the
  statement, `record.recovery_get_state` reports it per take, the registered ctest measures it.
- **UI absence — one line: recording crash recovery is drivable through the socket, not from the interface.**
  Nothing in `src/gui/` offers a recovery prompt, and `record.recovery_restore` does not yet put the recovered
  take into the session — no command in 0.3.0 imports an audio file onto a track as a clip, so restore resolves
  the offer and hands the material back untouched (`audio_untouched: true`, with `next_step` naming the
  deferred half). `docs/KNOWN-LIMITATIONS.md` carries the sentence and the bound.
- **Proof:** the registered ctest `ControlRecordingRecovery` (`tests/control-recording-recovery.py`) journals a
  capture against a real WAV, **SIGKILLs the instance (a real abnormal exit: the test asserts exit code -9 and
  that no shutdown ran)**, starts a SECOND instance against the same working directory, and requires the next
  start to find the capture, to report `frames_in_file` equal to the WAV's own frame count, to report
  `frames_recoverable` as the smaller of the journal's count and the file's, to place the measured
  beyond-the-journal material **inside the stated one-second lag bound**, and to report the ring frames it
  cannot recover. It then restores the take and requires the WAV to be byte-identical afterwards (sha256), the
  offer to be gone, a discard to remove the journal while keeping the audio, one `control.undo` to take a
  `record.journal_begin` back off by dispatching the paired command, and `control.undo` to refuse, typed,
  after an irreversible discard.

## Folder tracks: a real container with two modes, plus pinning and named visibility sets (`track.*`) — added 2026-09-13

- **New: a track that HOLDS other tracks.** `track.add` takes `type=folder`, and the folder is a
  **real engine container**, not a UI grouping: it is one row of the same flat track list (so
  iteration, document order, the save/load walk and the whole `trk-<n>` addressing model keep
  working unchanged), it **references** its children and never owns them (`TrackContainer` is the one
  owner, so a folder that deleted a child would be a double free), and the relation is **one
  attribute on the child's own `<track>` element** (`folder="<id>"`), written only when the child is
  in a folder. Five folder verbs: `track.set_folder` (into a folder, or out with an empty `folder`),
  `track.folder_set_collapsed`, `track.set_routing`, `track.set_pinned` and `track.folder_get_state`,
  plus **four named-visibility-set verbs** — `track.visibility_set_save` / `_apply` / `_remove` /
  `_list`. `track.list`, `track.get_state` and `arrangement.get_state` gained a `folder` field and a
  `visible` flag per track, and the three read commands stay **flat**: the parent is a FIELD naming a
  `trk-<n>`, never a nested array, so no existing client of the flat list breaks.
- **TWO MODES, and the organisational one is the default** (the owner's recorded decision,
  2026-09-12). `group` is organisation only: every child keeps its own mixer channel and no audio
  path changes. `routing` gives the folder **one regular mixer channel of its own** and points every
  child's mixer-channel binding at it, so the folder's channel receives the children's output through
  `Mixer::mixToChannel`, runs its own effect chain on the sum and sends to master like any other
  channel. It is `Mixer::createChannel()` and **not** `createBusChannel()`: a parallel bus refuses
  instrument output outright (Mixer.cpp: "a parallel bus never receives instrument output directly"),
  so a bus could not be the summing point this mode is. Routing mode refuses an empty folder, typed,
  because a routing group with nothing to sum is not a routing group. No new
  delay-compensation code: a child's own `AudioBusHandle` aligns itself to
  `Mixer::channelInputLatency(nextMixerChannel)`, so the sum lands in the folder's buffer **before**
  that channel's FX chain and before every compensation point downstream of it — the structural
  answer to the VCA lane's "which side of the compensation point" question
  (`docs/TRACK-FOLDER-DESIGN.md` §5.3). Turning the mode off restores each child to the channel it
  was on — the bindings are recorded in the folder's `prevch` attribute, so a folder **saved** in
  routing mode can be switched back in a later session — and releases the folder's channel **last**,
  after the children have stopped pointing at it.
- **Pinning and named visibility sets.** `track.set_pinned` and `track.folder_set_collapsed` write two
  persisted booleans on the folder's own `<trackfolder>` element. A **named visibility set** is a
  named, id-based list of tracks saved **in the project**: `track.visibility_set_apply` makes exactly
  its members visible and hides every other track. The flag is a **view flag** — it mutes nothing and
  changes no render, and the registered transcript proves that with a negative control.
- **UI absence — one line: folder tracks, their two modes, pinning and the named visibility sets are
  drivable through the socket, not from the interface.** Nothing in `src/gui/` creates a folder,
  draws the relation, indents a child, collapses a row, shows a pin or offers a set switcher: the
  folder's row is an ordinary `TrackView`, so the flags this release persists have no affordance
  reading them yet. `docs/KNOWN-LIMITATIONS.md` carries the same sentence.
- **Proof:** two registered ctests. `TrackFolderTest` (`tests/src/core/TrackFolderTest.cpp`, the
  engine half: membership derived from one source of truth, the cycle and self-parent refusals, the
  reset-on-absence of every field, routing mode really re-binding every child's channel to the
  folder's — non-bus — channel and putting them back, the visibility set round trip, a legacy project
  that grows **no** `folder=` / `visible=` attribute and **no** `<trackfolder>` or `<visibilitysets>`
  element, and a dangling `folder` attribute repaired at the container root) and
  `ControlTrackFolderTranscript` (`tests/control-track-folder.py`, the socket half: it drives a real
  instance, **saves the session, moves the in-memory model elsewhere, reopens the FILE** and requires
  the membership, the mode, both flags and the set to come back, then **measures the sum** — with
  routing on, the folder's own fader at 0 silences the render and at unity it sounds again within
  3 dB, while in group mode the folder owns no channel at all, so the silence can only have come from
  the routing).
- **Stated limits, in the same place as the claims.** The collapsed and pinned flags drive **no**
  interface in 0.3.0 (§ above). A child's mixer-channel **index** can change across a
  routing-off/routing-on cycle (`Mixer::deleteChannel` renumbers) while the routing relation is
  preserved — the same class of limit `track.add`'s re-add taking a fresh `trk-<n>` records. An
  **older build** reading `type="7"` hits `Track::create`'s `default: break` and **drops the folder
  row** — a dropped track, not a degrading one — which is stated in
  `docs/TRACK-FOLDER-DESIGN.md` §4.4 and is the strongest argument for a folder being organisational
  state that a legacy build degrades on. Nested containers are still out of the addressing model: a
  folder's children are addressed by their own `trk-<n>` and the song's flat list, exactly as before.
## Retrospective MIDI capture (`midi.retro_capture_*`) — added 2026-09-13

- **New: the engine keeps a rolling window of what you just played, so MIDI can be recovered AFTER the
  fact** — the "I should have hit record" case. `midi.retro_capture_arm` arms or disarms the mode (the same
  mode `Edit > Arm MIDI Capture` drives; the menu action declares this command id and its slot invokes it,
  SPEC A11), `midi.retro_capture_status` reports whether it is armed, which MIDI client is running and what
  the window holds, and `midi.retro_capture_to_clip` writes the retained window into a NEW MIDI clip and
  returns its `clip-<n>` id. `midi.retro_capture_to_clip` is `true_inverse` (a live Track checkpoint, the
  `clip.add` shape), so ONE `control.undo` — and one Ctrl+Z, which unwinds the same journal — removes the
  clip and every note in it. `midi.retro_capture_arm` is `not_mutating` (mode state, the `midi.learn_toggle`
  precedent; the config file's `midi/retrocapture` key is written only when the mode actually moves) and
  `midi.retro_capture_status` writes nothing. **Off by default**: a fresh capture records nothing, and while
  disarmed the whole cost on the MIDI input thread is one relaxed atomic load per event.
- **THE BOUND, stated rather than implied.** **8192 events**, the most recent ones, per open MIDI client —
  128 KiB, allocated once when the client is constructed. It is a **memory bound, not a time bound**: the
  ring is written from the MIDI input thread and that path may not allocate, lock or call out, so the
  storage is allocated once and never resized. In the case the feature exists for — a human playing, 10–20
  events a second — that is **roughly 7–13 minutes**, and the release notes say *minutes, not hours*; a
  dense controller stream fills the same window in under a minute. The policy is **drop-OLDEST** (the note
  you just played is the one that stays) with the loss COUNTED: `overwritten` and `paused_dropped` are
  reported by `midi.retro_capture_status` and the ctest requires
  `retained + overwritten + paused_dropped == events played`. `docs/MIDI-RETRO-CAPTURE-BOUNDS.md` is the
  decision record; the ctest asserts the capacity the build reports equals the figure that page states.
- **The window is per CLIENT, and it is not project state.** One window per open MIDI client, every channel
  and every source port in the same one; arming and disarming do not clear it; choosing a different MIDI
  backend discards it; `project.save` writes none of it. It is not a recording (no audio), a SysEx is stored
  as a flagged placeholder rather than its bytes, system clock/start/stop bytes are not stored at all, and a
  window that starts mid-phrase is REPORTED as truncated (`unmatched_ons` / `unmatched_offs`) rather than
  quietly tidied.
- **UI absence — one line: retrospective MIDI capture is drivable through the socket, not from the
  interface.** Two Edit-menu items (Arm MIDI Capture, Capture MIDI) invoke the same two commands and show
  the armed state, but nothing draws the rolling window, its length, or what it holds — there is no
  waveform view of it, no "you played something" prompt, and no keyboard shortcut (the one candidate,
  `Ctrl+Shift+M`, is unverified across the whole shortcut table, and taking an unverified key is worse than
  taking none). `docs/KNOWN-LIMITATIONS.md` carries the same sentence and the bound.
- **Owner's-31 item 15 (retrospective AUDIO capture) is NOT in this release.** It needs the same rolling
  window applied to audio frames, and this build has no capture path to apply it to (ALSA records nothing;
  the two-track recorder prototype is fed by tests), so a window built now could not be filled and no bound
  stated for it could be measured by a registered test. Item 14's own recorded dependency is *none*; item
  15's is the capture path itself, which is separate engine work. `docs/KNOWN-LIMITATIONS.md` says the same
  in one line.
- **Proof:** the registered ctest `ControlRetroCapture` (`tests/control-retro-capture.py`) starts the real
  binary headless with `--control-socket`, opens a project whose `<midiport>` is readable, and **plays real
  MIDI into it with `aplaymidi`** — an external ALSA-sequencer client, exactly like a keyboard, whose events
  carry the file's own tick timestamp. It requires the same file played BEFORE arming to leave the window
  empty and played again ARMED to come back as the notes' own positions, lengths, keys and velocities (read
  back out of the engine through `roll.get_state`: position 0 and 240, length 240, key 60 at velocity 100,
  key 64 at velocity 64); exactly one `control.undo` to remove the clip; and, with 20000 further events
  played into an 8192-event window, `events_buffered` to be exactly the documented capacity with
  `retained + overwritten + paused_dropped` equal to everything played, the instance still answering
  `control.ping` afterwards. It reports *Skipped* (exit 77), never *Passed*, on a host with no
  ALSA-sequencer tooling.
## MIDI clock / MTC: the DAW as a clock master and as a clock slave (`clock.*`) — added 2026-09-13

- **New: the engine is a MIDI clock master and a MIDI clock slave.** As a **master** it emits 24 clock pulses to
  the quarter note (one every `TicksPerMidiClockPulse` = 2 engine ticks, derived from
  `DefaultTicksPerBar`/`DefaultStepsPerBar` — the grid, not a tunable), START from the top of the song,
  CONTINUE and a Song Position Pointer from any other position, a Song Position Pointer for a seek while
  running, and STOP on the falling edge. It runs on the audio thread from `Song::processNextBuffer`, **before
  that function's transport gate**, because STOP is an *edge* and a stopped transport is exactly when it has to
  be sent (the Session View scheduler's precedent). The messages go through the engine's **existing** MIDI output
  path — `MidiClient::processOutEvent` through a `MidiPort` the clock owns — which is the path a track's MIDI
  output already uses; no second output path was invented. As a **slave** it follows an incoming clock, measures
  the tempo over a window of one quarter note (24 pulses) rather than the last interval, reports `locked` and the
  drift of the last interval from the window's mean, drops the lock when the pulses stop or a STOP arrives, and
  writes the measured tempo to the song when told to follow. Decoding had to be built too: `0xF8/0xFA/0xFB/0xFC`
  and the `0xF1`/`0xF2` payloads were previously **discarded** by the raw MIDI parser, and the clock family was an
  "unhandled output event" warning in both output switches.
- **Control surface:** three ids. `clock.get_state` writes nothing; `clock.master_set` is a recorded-action
  `true_inverse` (the enabled flag **and** the port subscription come back off one `control.undo`);
  `clock.slave_set` is a **`snapshot`** row, and the reason is the honest one: the configuration it sets is
  restored exactly, but turning tempo-follow ON makes the slave write `Song::setTempo` every time the measurement
  leaves the dead band — a **trajectory** of project-state writes, which is not one state any bounded recorded
  state restores. The row says so, the mechanism records it, the fallback names `transport.set_tempo`, and the
  transaction's `before.tempo` reports the value a caller restores it with. The three rows move the A16 histogram
  to the figure in the section above.
- **THE BOUND, stated rather than implied.** The counters and the bounded monitor `clock.get_state` returns are
  what the engine **produced and handed to its MIDI client**, asserted by the registered ctest below; they are
  **not** evidence that an external instrument received the bytes, which nothing on a box with no instrument can
  measure. **MIDI time code is not generated**: a full-frame MTC master needs a frame rate, a drop-frame flag and
  a SMPTE start offset, and this engine's time model is ticks-per-bar with neither, so `clock.get_state` reports
  `mtc: "absent"` instead of a timecode it cannot produce. Real-time and song-position messages **are** decoded
  and counted on the input side, but **an incoming START/STOP/CONTINUE/SONG POSITION does not move the transport**
  in this release. The follower's tempo is accurate to **at most `tempo × 2 × 5 ms / window`** — reported as
  `slave.tempo_error_bound_bpm` — because a pulse is timestamped when the MIDI client's reader thread observes it,
  and the tree's ALSA Raw reader polls after a 5 ms sleep. The master's own granularity is one audio period,
  reported as `master.period_ms`.
- **UI absence — one line: MIDI clock is drivable through the socket, not from the interface.** `grep -rniI
  'MidiClock' src/gui/` returns **0** hits, so there is no port selector, no external-sync toggle and no lock
  indicator; `docs/KNOWN-LIMITATIONS.md` carries the sentence and the bounds above.
- **Proof:** the registered QTest `MidiClockTest` (`tests/src/core/MidiClockTest.cpp`) drives the tracker with
  synthetic timestamps and asserts a pulse stream at 120 BPM measures 120 and one that moves to 140 BPM measures
  140 (following, not averaging), that two pulses or none never lock, that a lock drops past the timeout and
  immediately on a STOP, that a Song Position Pointer is in MIDI beats, that the master's message set for a known
  transport script is exactly START + the pulses the advanced ticks imply + a pointer for a seek + STOP **in that
  order**, and that the group carries the A16 classes the table states. The registered ctest
  `ControlClockCommands` (`tests/control-clock-commands.py`) drives the **real binary** over `--control-socket`:
  the master over a live transport (counters, monitor order, STOP last, silence once stopped, silence while
  disabled), the slave enabled with **no clock arriving** — unlocked, no tempo written, transport not moved, and
  the same again after several audio periods and three follower polls — every typed refusal, the A16 records, and
  both `control.undo` inverses including the slave's honest one.

## Auto-mastering wave 1 — candidate generation and objective scoring, drivable (2026-09-14)

- **The session can be mastered, and the candidates are measured rather than guessed.** `mastering.run`
  renders the mix **once** (the count is the engine's own, `ProjectRenderer::renderCount`, and comes back as
  `render_count`), branches every candidate of `MasteringJob::defaultCandidates()` off that one render, writes
  one wav per candidate into a directory the caller names, and measures each with the merged BS.1770-4 meter:
  integrated loudness, the loudest 3 s window, measured true peak and crest factor, plus the residual against
  that candidate's target and its loudness and true-peak verdicts. `mastering.list_candidates` publishes the
  set the candidates are generated from (each target with the document its numbers come from — EBU R 128's
  published −23 LUFS-I ± 0.5 LU and −1 dBTP, and a −14 LUFS-I streaming *convention* with a ±1.0 LU tolerance
  this project chose and states), and `mastering.get_state` reads the last run back, hashing the files it
  wrote. **Nothing ranks the candidates and nothing calls one best.** The design of record is
  `docs/AUTO-MASTERING.md` (task #610); the engine (`include/MasteringJob.h`, `include/MasteringChain.h`) and
  its own end-to-end test `MasteringTest` were already in the tree — what this adds is the surface.
- **The run is a child process, on a serialised copy of the session.** `mastering.run` runs the shipped CLI
  action (`zene master <project> -o <dir> --report <path>`) exactly as `render.render` and `bounce.in_place`
  run their renders, for the reason their comments give: an in-process render drives **this** instance's audio
  engine (`ProjectRenderer::startProcessing` → `audioEngine()->startProcessing()/stopProcessing()`, which owns
  the device thread) and `Song::startExport` stops playback and re-measures the song. So the **session is not
  modified** and the running instance's audio path is untouched; the numbers come back through `--report` (the
  run's own JSON document) rather than from a parse of the printed table.
- **Reversibility.** `mastering.run` is `true_inverse` through a recorded **action checkpoint** (SPEC A16): the
  run's outputs are files in a directory **outside** the project, which no Song checkpoint carries and no live
  object restores, so the recorded step removes every file the run created and writes back every revision the
  directory already held, byte for byte. Both are captured **before** the first write and bounded at 64 MiB of
  pre-existing wav files — beyond that the run is **refused**, typed, rather than performed without an inverse.
  There is **no redo half**, and the record says so: a faithful redo would have to hold the run's own outputs,
  and `control.redo`'s own contract already documents a one-way action step. The two reads are `not_mutating`.
- **Proof:** the registered ctest `ControlMasteringCommands` (`tests/control-mastering-commands.py`) drives the
  **real binary** over `--control-socket`, opening the product's own fixture
  (`tools/auto-mastering-demo.py make`, the doc's reproduction section): it reads the candidate set off the
  wire, refuses every junk request typed, runs the master (5 candidates from 1 render — 6 files that match the
  reported paths), checks every candidate against **its own target's** tolerance and ceiling, reads the run
  back through `mastering.get_state`, and takes it back **twice over**: after `control.undo` the directory
  holds no candidate at all (a file that never existed can only be **removed**, not restored), and after a
  second run replaces the first run's files, `control.undo` restores the replaced revision **byte for byte**
  (compared by sha256). It also measures the capture bound being enforced. `tests/src/core/MasteringTest.cpp`
  is extended with the document round trip the surface depends on: the JSON and the job's own reports agree
  field for field, the document survives the file, and a reading the meter could not make is `null` rather
  than a fabricated number.
- **UI absence — one line: auto-mastering is drivable through the socket, not from the interface.** There is no
  Export-dialog mastering mode, no candidate list panel and no A/B player; `grep -rniI 'Mastering' src/gui/`
  returns **0** hits. `docs/KNOWN-LIMITATIONS.md` carries the sentence and the bounds above.
- **What wave 1 still does NOT have, stated rather than implied:** the wave-1 ENGINE is complete (candidate
  generation + objective scoring, both drivable now); what is **not built** is the *decision* half the
  feasibility study's step 5 names — **no pick-log**, so no record of which candidate a user chose, and
  therefore **no learned ranker** (wave 3 is gated on real pick-logs, which do not exist yet); **no
  reference-matching arm** (matching a candidate to a reference track); **no level-matched A/B**, so nothing
  presents the candidates as comparable by ear (they differ in loudness by design — that is the target axis);
  and the CLI's own bounds hold through the socket too — **wav only**, no per-candidate parallelism, and
  renders in this tree are **not bit-reproducible** run to run, so two runs of the same master are equal only
  to the meter's tolerance (≤ 0.05 LU / 0.01 dB), never byte for byte.

## Note randomisation, note transforms, slide notes, the `scale.*` group and `device.mpe_set` — added 2026-09-15

Fifteen ids over four groups, all of them a **command surface over an engine that was already in the tree**
(board task #648; feature-list rows 11, 66 and 81). Nothing here is new DSP: `NoteRandom`, `NoteTransform`,
the slide-note flag, `ChordTable` and `MpeExpression` all shipped before this wave, with their own tests;
what none of them had was an id, an argument and result schema, an A16 row and a proof. Each has all four
now, and each is exercised through the registry (the same door an agent uses) by the registered ctest
`ControlNoteScaleVerbsTest` in `tests/CMakeLists.txt`.

- **`note.random_seed_get` / `note.random_seed_set`** — the project's MIDI seed (`Song::midiSeed`,
  serialized as the header's `midiseed` attribute and written only when it is not 0). The pair is what makes
  a seeded take survive a save and a re-open. `true_inverse` through a **recorded action checkpoint** — the
  seed lives on the Song, which is not a `JournallingObject`, so the recorded undo step calls
  `Song::setMidiSeed` with the before-value (`clock.master_set`'s shape, for the same reason).
- **`note.randomize`** — the seeded roll: `velocity_jitter` (0..1) multiplies each note's velocity by a
  factor in `[1-j, 1+j]`, and an optional `position_jitter` moves it by up to that many ticks, never before
  tick 0. Both draws are **pure functions of the seed and the note's identity** (`NoteRandom::velocityFactor`
  and `rollUnit`, on their own salts), so the same seed on the same starting notes reproduces the take
  **exactly** and a different seed produces a different one — the pair the proof asserts, so a comparator
  that only checks "the call succeeded" cannot pass it. The seed defaults to the project's persisted one; a
  `seed` argument rolls from that number **without** writing it to the project (`note.random_seed_set` is the
  verb that persists one). `true_inverse` through the clip's own `MidiClip` checkpoint — and **not** by
  re-running, because the roll is multiplicative on the current velocity and drawn from each note's position
  at entry.
- **`note.transpose` / `note.velocity_offset` / `note.velocity_scale`** — `NoteTransform`'s three transforms
  as commands, over the whole clip or over `note.select`'s selection. The grid quantise is deliberately
  **not** re-wrapped: `groove.quantize` already drives `NoteTransform::quantizeNotes` with a strength, a
  humanise amount and a seed, and a second id for one behaviour is what SPEC A11's "one action, one
  implementation" exists to prevent. The engine's clamps are stated (a transpose stops at 0/127, a velocity
  at 0/200) and the arguments are **refused** outside the range this surface accepts, never silently clamped.
  `true_inverse` through the clip checkpoint.
- **`note.slide_set` / `note.slide_clear`** — the FL-style slide (portamento) note, and one clip-level verb
  that clears every flag in scope as ONE undo step. `true_inverse` through the clip checkpoint **including a
  first edit**: `slide` is written only when it is set, but `Note::loadSettings` assigns
  `attribute("slide").toInt()` unconditionally, so an absent attribute means "regular note" and the restore
  is exact — the same reset-on-absence rule `note.probability_set` relies on for `prob`.
- **`scale.list` / `scale.get_state` / `scale.root_set` / `scale.set` / `scale.snap_notes`** — the scale and
  key vocabulary as commands, over `ChordTable`'s own 95 named entries (the boarded-gaps list names
  `scale.root_set` and `scale.set` by name). `scale.list` publishes each scale's degrees, pitch classes and a
  twelve-character membership mask; `scale.get_state` answers for the context OR for a root/scale the call
  names, and with a `clip` argument counts the notes in and out of scale with the engine's own predicate
  (`NoteTransform::matches`); `scale.snap_notes` moves the out-of-scale notes to the nearest in-scale pitch
  (ties downward, `NoteTransform::snapToScale`) and reports how many moved and how many are still out.
  `scale.root_set` / `scale.set` are `true_inverse` through recorded action steps; `scale.snap_notes` is
  `true_inverse` through the clip checkpoint, and **not** by re-running the snap, because a note it moved is
  now in scale, so a second call moves nothing and cannot bring the old key back.
- **`device.mpe_get_state` / `device.mpe_set`** — the registry's first `device.*` group: the MPE **input
  switch** (`MpeExpression::setEnabled`, one relaxed atomic store, nothing on a realtime path) and the honest
  read-back of what that flag does and does not make audible. `device.mpe_set` is `true_inverse` through a
  recorded action step; the flag is a process-wide `std::atomic_bool` and, by the engine's own design, is
  **deliberately not serialized**. The master channel and the bend range are per-MIDI-stream instance
  settings with no object the control surface can reach, so the group reports the engine's defaults instead
  of writing a copy nothing reads.

**A16, counted:** `+11 true_inverse / +4 not_mutating` over the fifteen rows
(`src/core/ControlReversibilityTableNoteScale.cpp`, a GROUP file joined into
`reversibilityRowTable()`). Seven of the eleven reverse through a live `MidiClip` checkpoint; four (the
seed, the scale group's two context writers and the MPE flag) through a recorded action step, because none
of them is a `JournallingObject`.

**UI absence — one line each.** All three groups are **drivable through the socket, not from the
interface**: no action, menu entry, shortcut or view randomises a note, shows or edits the project seed,
transposes a clip or a selection as a command, marks a slide note, snaps notes to a scale, shows the scale
group's context or reaches the MPE switch — and the piano roll's own key/scale combo boxes are **not wired**
to `scale.*` in either direction. `docs/KNOWN-LIMITATIONS.md` carries the sentences, and the bounds above
with them.
## Loudness metering — the live master and any rendered file, drivable (2026-09-15)

- **The BS.1770-4 meter is reachable, live and offline.** Feature row 24 of `docs/FEATURE-LIST-0.3.0.md` was
  the audit's "in the tree but not drivable" case: the measurement core (`LufsMeter`, ITU-R BS.1770-4 /
  EBU R 128) and the render path's offline consumer (`LoudnessReport`, which writes the `.loudness.txt`
  sidecar) were merged and proven by `LufsMeterTest`, `LoudnessReportTest` and `MasteringTest`, and **no
  `lufs.` or `meter.` id existed at all** — nothing an agent could send measured anything, and
  `export.get_settings` did not expose the render-path report either. The new `meter.*` group closes both
  halves and **forks no DSP**: every number comes out of the merged `LufsMeter`.
- **Three ids.** `meter.get_state` — the LIVE master readout: gated integrated loudness (LUFS-I), momentary
  (LUFS-M), short-term (LUFS-S), the loudest short-term window and true peak (dBTP), plus whether the tap is
  armed and how many audio periods it has measured. `meter.arm` — arms or disarms the tap; **arming starts a
  measurement** and **disarming keeps the last reading readable**, so "arm, play the section, read" reports
  that section and a stopped tape can still be read. `meter.measure_file` — the same five numbers for a
  **rendered file**, measured now from the file's own bytes with the EBU R 128 verdict and the file's own
  facts (rate, channels, frames, duration, size, sha256). A reading is JSON **`null`**, never a plausible
  number, while the meter has no measurement (silence, or a window that has not filled).
- **The live tap is PASSIVE, and that is measured rather than asserted.** `include/MasterLoudnessTap.h` owns
  one `LufsMeter`, constructed once with the engine's processing rate, and the engine hands it the period
  `renderStageMix()` has just mixed. It takes the frames `const`, allocates nothing, locks nothing and grows
  nothing, so a render is bit-for-bit what it was with the tap disarmed — which is also why the tap is
  **disarmed by default**: an unarmed engine pays one relaxed atomic load per period and measures nothing.
  Arming, disarming and resetting go through a **bounded** quiesce on the control thread and never replace
  the meter object, so the tap can be toggled while the transport runs.
- **The render-path report is drivable too.** `export.get_settings` now exposes `loudness_report`, and
  `export.set_loudness_report` turns the `.loudness.txt` report on for the next render (the same value the
  export dialog's checkbox and the CLI's `--loudness-report` set); `render.render` passes the flag to its
  child process, so a socket-driven render produces the sidecar as well. Measure-only: the rendered audio is
  byte-identical whether the report is on or off.
- **Reversibility (SPEC A16).** `meter.get_state` and `meter.measure_file` are `not_mutating` (one reads a
  snapshot of atomics, one reads and hashes a file). `meter.arm` and `export.set_loudness_report` are
  `true_inverse` through recorded **action checkpoints**. What the inverse does **not** restore is a
  measurement: readings are surface memory, not project state, and arming starts a fresh one — the row, the
  command description and the transaction record all say so.
- **Proof:** the registered ctest `ControlMeterCommands` (`tests/control-meter-commands.py`) starts the real
  binary over `--control-socket` and carries the negative controls that make the tap honest — **silence reads
  `null`** (EBU fixtures: `tone-23` −23.00 and `tone-33` −33.00 LUFS-I measure their own known levels, and the
  silent fixture measures nothing and claims no verdict), **a signal 10 dB louder reads 10 LU higher** (both
  from a file and LIVE, with the two fixtures playing through the engine, where a constant-reading tap cannot
  produce the difference), and **the audio is byte-identical with the meter attached** (measuring a file
  leaves its sha256 unchanged; two renders of one project have identical PCM frames with the tap armed and
  disarmed) — plus typed refusals and `control.undo` restoring the armed flag. The live tap itself is proven
  by the registered ctest `MeterTapTest` (`tests/src/core/MeterTapTest.cpp`): the sentinel for silence,
  ±0.1 LU against EBU Tech 3341 case 1/2, 10 LU separation, a fed buffer hash-identical before and after, and
  **0 allocations** over 64 fed blocks.
- **UI absence — one line: loudness metering is drivable through the socket, not from the interface.** There
  is no loudness meter widget, no LUFS/true-peak readout, no meter bridge and no loudness column;
  `grep -rniI 'lufs\|loudness' src/gui/` finds only the export dialog's existing report checkbox and its
  result label. `docs/KNOWN-LIMITATIONS.md` carries the sentence and the bounds;
  `docs/METER-SURFACE.md` is the feature's own record.
## Render/export presets and a render that takes a time range (`export.preset_*`, `render.render`) — added 2026-09-15

Two feature rows, one store and one span. Nothing like either existed: a case-insensitive grep for
"export preset", "RenderPreset" and "batch export" over the tree returned **0** hits before this.

- **`export.preset_add` / `export.preset_list` / `export.preset_apply` / `export.preset_remove`** — a
  named render/export preset is a name plus the three `OutputSettings` fields a render can actually be
  told: the sample rate, the bit depth and the stereo mode. It is stored as ONE JSON document in the
  user preset tree (`<userPresets>/renderpresets/<name>.zrp`), **outside the project**, so "the 24/96
  master" is the same preset whichever project is open — and so `project.open` cannot lose it.
  `export.preset_apply` puts the render path on a preset (or, with no name, back on its own defaults:
  44100 Hz, 16-bit, joint stereo); the NEXT `render.render` is started with those settings **as its own
  child-process command line**, through the one render path the product ships. `true_inverse` for the
  three writers — each records the action checkpoint that removes its document, writes back the
  revision it replaced, or restores the selection it replaced — and `not_mutating` for the read.
  A preset whose values nothing could honour is **refused typed at the point it is written** rather
  than discovered by a failed render: the sample rate must be inside the window the shipped render CLI
  accepts (44100–192000 Hz).
- **`render.render` takes a time range** — `start_ticks` and `end_ticks`, both required together,
  render only that span of the song instead of the whole project. This is the `--range-start` /
  `--range-end` the render CLI gained with it, and the range rides the engine's **own** bounded render
  (`Song::setRenderBetweenMarkers` with `Timeline::setLoopPoints` — the path the GUI's "render between
  loop markers" checkbox has always driven), so a selection render is the same renderer over a span:
  **no second renderer, and no tail bar or loop repetition** — the selection is exactly the selection.
  Half a range, an empty range and a negative range are refused typed, before the session is
  serialised or a destination opened.
- **UI absence — one line: render/export presets and a ranged render are drivable through the socket,
  not from the interface.** `grep -rniI 'renderpreset\|render preset' src/gui/` returns **0** hits:
  the export dialog still has its own per-render controls, its own bit-depth/stereo-mode combo boxes
  and its own "export between loop markers" checkbox, and **no preset list, no "save as preset"
  action and no apply control** reaches the store or the applied selection.
  `docs/KNOWN-LIMITATIONS.md` carries the sentence and the bounds below.
- **The limits, stated rather than left to be discovered.** A preset carries the **three settings
  only** — `OutputSettings`' bitrate, compression level, loudness-report, dither and SRC-quality
  choices are **not** in a preset (the CLI has no flag for them), and an applied preset governs
  **`render.render` only**: `render.stems` keeps its own fixed 44100 Hz and its own one-bar tail.
  The applied selection is **process-wide, not project state** — it is not saved with the project and
  a new instance starts on the defaults — and the **stereo mode is only read by the MP3 encoder**
  (`src/core/audio/AudioFileMP3.cpp:112`), so a WAV render stores it, passes it and is unaffected by it.
  The store is **per-user, not per-project**, exactly as the chain-preset store is. And `render.render`
  carries the **declared bound** every render-running command carries: the render runs in a child
  process and the control surface does not answer — `control.ping` included — until it finishes
  (`docs/RENDER-CHILD-WAIT.md`); the range does not raise that bound, it is applied inside the child.
## Linked / smart clips: two clips, one source (`clip.link_*`) — added 2026-09-15

Feature-list row 6 (section 1), board task #645: **a linked clip is a clip that shares its source with
another, so an edit to one is seen by all.** The engine half is `include/ClipLinks.h` plus
`Clip::linkId()`; the surface is four ids in the `clip.` group (`clip.link_create`, `clip.link_remove`,
`clip.link_get_state`, `clip.link_sync` — not `link.*`, which is session tempo/beat sync, `docs/LINK-SYNC.md`).

- **The decision this row asked for: a persisted group id plus a WRITE-THROUGH MIRROR — not a shared content
  object, and not copy-on-write.** The relation is an int on the clip (`0` = unlinked) written to the clip's
  OWN element as `link="<n>"` only when the clip is a member, and read back with the same reset-on-absence rule
  the take lane already follows. An edit to a member copies that member's note list onto every other member of
  the group in the same step. Aliasing one `NoteVector` across N clips was rejected because it needs a
  load-order re-linking pass (the very thing a save/reload round trip has to prove), because the note list is
  read as a value by the play handle, the piano roll, the comp lanes and the serialiser, and because a
  disagreement would then be impossible by construction — and therefore unreportable. Copy-on-write in its
  strict sense (share until someone edits, then DETACH) is the opposite of the feature: a write fans OUT, and
  the detach is its own command. `docs/LINKED-CLIPS.md` §1–2 is the full argument.
- **The relation survives a save/reload**, and that is the registered proof's centrepiece: the `link`
  attribute rides each member's own element, so a reload rebuilds the group from the clips with no second
  registry in the file and no repair step. `ClipLinkTest::theLinkSurvivesSaveAndReload()` writes a project,
  asserts the attribute is in the file, reloads it, asserts the group is still there with both members in
  sync, and then **edits one reloaded member and asserts the other one changes** — the round trip and the
  propagation together, which is what makes it a smart clip and not a session hack.
- **`clip.link_remove` is the unlink, and it is the only detach.** A group needs two ends: an unlink that
  leaves one member dissolves the relation entirely, so no group of one outlives its last pair. Merging two
  groups by naming a member of another is refused typed (`refused`) instead of silently overwriting one
  group's content with the other's.
- **Reversibility (SPEC A16): `true_inverse` for the three writers, `not_mutating` for the read**, rows in
  `src/core/ControlReversibilityTableVerbs.cpp` beside `clip.trim`'s. The checkpoint is taken over EVERY
  member a command writes (`ProjectJournal`'s multi-object overload) and the registry's
  `mergeCheckpointsFrom()` folds those into the command's one undo step, so **one `control.undo` takes the
  whole group back** — asserted, not asserted-about: `ClipLinkTest::undoRestoresEveryMemberOfTheGroup()`.
- **UI absence — one line:** the four ids are drivable through the socket and not from the interface — no
  link badge, no "edit shared source" gesture, no group colour; and of the edit kinds, `note.add` /
  `note.remove` / `note.move` / `note.resize` / `note.velocity_set` propagate while `clip.move` /
  `resize` / `trim` / `slip` / `set_fade` / `set_gain` / `crossfade`, mute/solo, name, colour and take lane
  stay per-member, and an audio clip cannot be a member at all (the group's content channel is a note list, so
  `clip.link_create` refuses a `SampleClip` typed). `docs/KNOWN-LIMITATIONS.md` carries the same sentence; the
  ctest `ClipLinkTest::theOneLineUiAbsenceIsWrittenDown()` reads both files from the built tree and fails if
  either loses it.
- **What is NOT here, stated rather than implied:** no UI (§ above); no audio-clip linking; the mirror writes
  the WHOLE note list rather than a delta (so a one-note edit to a member of a large group rewrites every
  member's list — unconditional by design, because a delta-based mirror is where divergence would come from);
  a member with auto-resize on re-sizes to the propagated content, because that is the engine's own rule for a
  clip whose notes changed, while a manually resized member (`autoresize=0`) keeps its length; and outside the
  command path a GUI gesture that calls the note entry points directly takes one checkpoint per object the
  journal sees, so a human's Ctrl+Z there may need more than one press — the pre-existing behaviour of a
  gesture that is not one command.
## The change-plan code rows: a Lua memory budget, an https-only non-blocking telemetry transport, and a shutdown hook that survives its owner (CODE-6, CODE-7, CODE-8)

Three rows of the change-plan register (`BACKLOG.md` § Change-plan register), built together because
they are all bounds on things the tree already had: a script that could allocate without limit, a
transport that could post in clear and wait for it, and a shutdown hook whose lifetime nobody owned.

**CODE-6 — the Lua memory budget beside the instruction budget.** `src/core/ScriptEngine.cpp` opened
its Lua state with `luaL_newstate()`, whose allocator is a bare `realloc()` with no accounting: the
instruction budget bounds how long a script may run (a count hook), and nothing bounded how much it
may hold. The state is now opened with `lua_newstate()` and the engine's own allocator, which counts
live bytes against an engine-held budget (default **64 MiB**, `ScriptEngine::DefaultMemoryBudgetBytes`)
and **refuses** an allocation that would cross it — so a runaway script fails with
`memory budget exceeded (… budget N, M refused allocation(s))` and its run is aborted, instead of
growing until the OOM killer takes the process. It is drivable and observable through the surface:
the new verb **`script.set_memory_budget`** sets the cap (refusing anything outside
`[512 KiB, 1 GiB]` rather than clamping it, and recording the previous value for `control.undo`), and
**`script.run`** reports the budget in force and what the run measured against it
(`memory_live_bytes`, `memory_peak_bytes`, `memory_refusals`). A16 class: `snapshot`, the
`control.set_undo_depth` shape. Proof: the registered ctest **`ScriptMemoryBudgetTest`** — the same
script completes under the shipping budget and is refused under one it cannot fit in, which is the
pair that makes a refusal mean something. Limits: the budget bounds **Lua memory** (what the script
asks the allocator for), not the C++ heap the bindings use; a run already executing keeps the budget
it opened its state with; and the cap does not make a script cheaper, it makes a runaway one
survivable.

**CODE-7 — the telemetry transport is https-only and never blocks the caller.**
`TelemetryNetworkTransport::send()` posted to `QUrl(m_endpoint)` whatever scheme the config file held,
and then waited in a nested `QEventLoop` behind a 10 s timer for the reply — so an endpoint configured
as `http://` would have put the payload on the wire in clear, and a slow or blackholed one parked the
**calling thread** (the GUI thread) for ten seconds. Both are now structural: the scheme is checked on
**every** send (`TelemetryNetworkTransport::isAllowedEndpoint()`, one definition, https only, with the
refusal in words) and a send **hands the POST to Qt and returns** — `true` means "queued", not
"delivered", and the reply deletes itself when it finishes. `telemetry.status` reports the policy and
the verdict on the configured endpoint (`transport_policy`, `transport_endpoint_allowed`,
`transport_endpoint_reason`, `transport_blocking`), so a client can see whether what is configured
would actually be posted to. Proof: the registered ctest **`TelemetryTransportTest`** — a plain-http
endpoint is refused **before the delivery seam is reached** (the recorder standing in for the network
is never called, which is the measurable form of "before a socket exists"), and a send to TEST-NET-1
(192.0.2.0/24, RFC 5737 — never answers) returns at once, which the ten-second version could not do on
any machine. The consent model is untouched, as the change plan requires. Limits: the transport is
**still not wired to any code path that sends** (the endpoint ships empty and no ingest service
exists), so this fixes the transport's properties rather than delivering telemetry; and "non-blocking"
is a property of the transport, not a promise about a reply that never arrives — an attempt that never
finishes is never counted as a send.

**CODE-8 — the control-server shutdown hook survives its owner.** The hook that unlinks the control
socket was `m_registry->addShutdownHook([this]() { close(); })`: it captured a raw pointer, and it was
held in a member of a **singleton that can be destroyed and re-created** — `ControlSession.cpp`'s
last-resort guard calls `ControlRegistry::instance()->runShutdownHooks()`, and `instance()` builds a
new, empty registry if the old one is gone. So a hook registered before a `destroy()` died with the
first instance, and the guard ran an empty list on exactly the route the socket must be unlinked by;
and in the other direction a `ControlServer` destroyed before shutdown left a call on freed memory
behind. Hooks now live in a **process-lifetime store** (survive the instance), ids identify them, and
`ControlServer::~ControlServer()` **un-registers its hook** so the hook can never outlive the object it
closes over — the socket file is removed either way, by the hook or by the destructor's own `close()`.
Proof: the registered ctest **`ControlShutdownHookTest`**, whose two lifetime cases fail on the
pre-CODE-8 behaviour. Limits: `runShutdownHooks()` is not thread-safe and is not meant to be (it is
the UI thread's exit path); and the hooks are process state, so nothing here survives a `SIGKILL`.

**The boarded `telemetry.consent_set` (row 85) is closed by decision, not by a second id.**
`telemetry.consent` **is** the consent verb: it opens the one consent screen, the screen's Save is the
only writer of the consent record, and the command declares `requires: display, human` so no automated
caller can consent at all. A `telemetry.consent_set` an agent could call would be a command that turns
telemetry **on** on the user's behalf — the one thing the feature's recorded design forbids
(`docs/TELEMETRY-V1.md` §2.5, `docs/AGENT-SURFACE-TELEMETRY-FIX.md`) — and adding it with the same
`requires` would be a second id for an act the surface already names. The decision, the argument and
the condition that would reopen it are recorded in **`docs/TELEMETRY-V1.md` §2.6**, and the invariant
is pinned by a test rather than by prose: `ControlRegistryTest`'s telemetry slots refuse the id's
existence, assert that no reachable `telemetry.*` command is mutating, and assert that the only
consent verb is `requires: display, human`.

**UI absence, one line each** (the same sentences are in `docs/KNOWN-LIMITATIONS.md`): the Lua memory
budget has **no interface** — no dialog shows live Lua bytes or sets a cap, and the budget is
drivable only through `script.set_memory_budget` and visible only in `script.run`'s report; the
telemetry transport's policy has **no interface** — the consent screen describes what would be sent,
not where or over what, and whether the configured endpoint is acceptable is visible only through
`telemetry.status`; the shutdown hook has **no interface** — it is process machinery on the exit path,
and nothing in the window shows it; and the `telemetry.consent_set` decision has **no interface** to
be absent from, because nothing was built.
## Pitch-preserving time-stretch: a warped clip can keep its pitch (`warp.stretch`) — added 2026-09-15

Until now every rate change in the engine was **plain resampling**: the warp mapping hands
`Sample::play` a ratio, `Sample::play` hands it to `AudioResampler`, and the pitch moves with the rate —
which is why `docs/WARP.md` §3 says a 2x warp is an octave up. That is still the **default** and it is
unchanged for every project; what is new is the second mode.

* **The engine half.** `AudioStretcher` (new: `include/AudioStretcher.h`, `src/core/AudioStretcher.cpp`)
  is a **WSOLA** time-domain stretch: grains of 1024 frames at 50 % overlap with a periodic Hann
  window, a hop that advances the output by a fixed synthesis hop and the source by
  `synthesisHop × speed`, and a normalised cross-correlation search that re-aligns every grain to what
  has already been rendered. Fixed-size state, prepared once: **`process()` allocates nothing, locks
  nothing and grows nothing** (0 allocations across 2000 render calls, measured with `AllocationProbe`),
  and period-sized calls produce **byte-identical output to one call**.
* **The clip half.** `SampleClip` gains a persisted stretch mode (`WarpStretchMode`, **default
  `Resample`** = the historical render), written as the `stretch="wsola"` attribute of the same `<warp>`
  element the warp engine already owns — so a clip that never asks serialises byte for byte as before —
  and `SamplePlayHandle` routes a non-linear clip through the stretcher when the clip asks. A clip that
  renders linearly (no markers, project tempo) is **never** routed through it, measured as **0 differing
  frames of 88200** between the two modes.
* **The proof, with the pitch measured rather than asserted.** The same two-tone input (440 Hz + 660 Hz)
  through both paths, amplitudes measured with a Goertzel bin per tone and the fundamental measured a
  second time by interpolated zero crossings: the **resample** control renders 0.0000 / 0.0000 / 0.5000 /
  0.3000 at 440 / 660 / 880 / 1320 Hz (880 Hz measured — the octave up), the **stretch** renders
  0.4992 / 0.2990 / 0.0001 / 0.0001 at the same frequencies (**439.95 Hz** measured), at the same length
  and the same RMS. Measured again through the clip path (`SampleClip` → `SamplePlayHandle::play`) on
  one clip's two render modes. The registered ctests are `AudioStretcherTest` (9 slots) and
  `SampleClipStretchTest` (7 slots), both green: `ctest -R "AudioStretcherTest|SampleClipStretchTest"` →
  2/2 Passed.
* **The quality/complexity trade, stated and measured.** The price is the alignment search:
  **58.7 ms of CPU per second of stretched audio at the default** (`searchRadius` 128, 44.1 kHz stereo,
  ~17x faster than realtime, ≈ 6 % of one core per stretched clip), 29.7 ms at 64 and 15.1 ms at 32 —
  linear in the radius, which is also the alignment reach (±128 frames = 2.9 ms, so periods down to
  ~345 Hz). With the search off (a plain overlap-add) the two tones **cancel** to 0.0285 of 0.5: that row
  is why the cost buys something. Nothing is paid by a project that does not use the mode.
* **The surface.** One new command, `warp.stretch` (`clip`, `mode` = `resample` | `preserve_pitch`),
  headless-safe, **reversible**: the mode is the clip's own serialized state, so the Clip's journal
  checkpoint is the inverse and the recorded inverse is `warp.stretch` with the before-state's mode
  (A16 row `true_inverse` in `src/core/ControlReversibilityTable.cpp`). `preserve_pitch` is **refused**
  for a clip with no rate change rather than silently accepted. `warp.list` reports `stretch`,
  `stretch_algorithm` and `renders_linearly`, so both modes are readable and the answer is never a
  guess. Full detail, including the measurements and every limitation: `docs/PITCH-STRETCH.md`.
* **UI absence — one line: the stretch mode is settable through the socket, not from the interface.**
  `grep -rniI 'WarpStretchMode\|preserve_pitch\|warpStretch\|AudioStretcher' src/gui/` returns **0**
  hits — there is no clip-context entry, no checkbox and no marker-drag gesture for it.

## Structural undo: add / remove / move a track, add / remove a device (feature row 75, task #664) — added 2026-09-15

- **What was wrong.** The undo stack is one history (`ProjectJournal`, the same stack Ctrl+Z unwinds), but the
  STRUCTURAL edits - the ones that create or destroy an object rather than change it - were not on it.
  `track.move` did not exist as a command at all, `plugin.unload` recorded itself `reversible: false` ("snapshot
  only, rebuild it by hand"), and the reorder a user made by dragging a track, and the delete a user made with the
  track ✕ button, left **no** undo step: several paths that reach the engine bypassed `addJournalCheckPoint`
  entirely. The deletion was the worst of them and it is structural in the strict sense: `~Track` deletes the
  track's clips and **only then** calls `TrackContainer::removeTrack`, so the container never sees the music die -
  a checkpoint taken there restores a track with an **empty clip list**. The track would come back and the music
  would not.
- **What is in now.** `track.add`, `track.remove`, `track.move`, `plugin.load` (the effect branch) and
  `plugin.unload` are journalled, and ONE `control.undo` restores each of them through the engine's own code paths:
  - `track.remove` captures the track's own XML (`Track::saveState` - **its clips and their notes included**)
    *before* the delete, and the recorded step recreates the track with `Track::create(element, song)` - the call
    the project loader makes - **at the index it was removed from**. The redo removes it again, so the inverse is a
    pair and not a one-way door.
  - `track.move` is a NEW command id: the arrangement's order, drivable at last. It refuses an index outside the
    song rather than clamping it, reports the song's whole order back in the same reply, and is one recorded step
    (`track.move` + one `control.undo` puts the order back).
  - `plugin.unload` captures the device's own state document (`controlEffectStateXml`, which carries the
    sub-plugin key that identifies a **hosted** plugin, not merely its descriptor name) and the recorded step
    re-instantiates the same plugin **at the same index in the chain** with its settings restored.
  - The GUI's own two gestures record the **same** steps: the ✕ button (recorded before the deferred delete
    runs) and a track drag / arrow-key move (recorded inside `TrackContainer::moveTrack`, the one place every
    reorder passes through). A delete a user can undo and a delete an agent can undo are one implementation.
- **The new engine piece.** `ProjectJournal::addJournalStructure()` - a structural step is an action checkpoint
  **plus the byte accounting an action checkpoint gets wrong for a structural payload**: an action step declares
  `bytes = 0`, so a 64 KiB captured track was free, the declared byte budget never applied to the largest steps on
  the stack, and `control.undo_depth`'s `retained_bytes` reported nothing. The structural step measures the
  document it carries and charges it, so the same FIFO bound evicts either kind of step. A replay guard keeps a
  recorded step's own reorder from being recorded again (otherwise the stack would grow while it unwinds).
- **The A16 rows.** `track.move` and `plugin.unload` are new `true_inverse` rows and `track.remove`'s mechanism
  now names the clips and the index; all four structural rows live in their own table file
  (`src/core/ControlReversibilityTableStructure.cpp`), joined into the action half so `control.transactions` still
  reads ONE `true_inverse` block with one row count. `plugin.unload` moved out of the `irreversible` block, which
  is a **disagreement with `A16-STATUS-MEASURED.md`** that the row itself records.
- **The proof.** `tests/control-undo-structural.py`, registered as ctest **`ControlUndoStructuralTranscript`**,
  drives the real binary headless and asserts on the CLIPS' CONTENT read back after the undo - position, length,
  note count, and every note's key / position / length / velocity - because "a track with that name exists again"
  is satisfied by exactly the lossy undo this work removes. It measures the row-51 interaction (deleting a track
  shifts every later `clip-<n>`; the undo shifts them back) rather than asserting it away, drives the
  `track.move` reorder + undo + redo and its out-of-range refusal, drives the device half (load, change a
  parameter, remove, one undo restores the instance at its index with the parameter), and asserts the accounting:
  deleting a track must GROW `control.undo_depth`'s `retained_bytes` - a step that counted 0 bytes would print no
  growth at all.
- **UI absence — one line: structural undo is drivable through the socket and the two gestures that reach it from
  the interface are the ones that already existed (the track ✕ and a track drag), which now record the same step;
  there is still no undo-history panel, nothing lists the structural steps, and no control names or limits the
  capture bound.** `docs/KNOWN-LIMITATIONS.md` carries the sentence and the three limits above (`clip-<n>` is
  index-derived; a document over 64 KiB records no inverse and says so; the captured size is charged to the byte
  budget).

## Plugin hosts process exactly the frames they are asked for, in chunks (CODE-4, row 82) — added 2026-09-15

Both native host paths used to get a request larger than the block they were prepared for wrong, in two
different ways. `Vst3Host.cpp` handed the plug-in one call with `numSamples = frames` while its own silence
and scratch blocks stayed the prepared block long, so the plug-in read and wrote past both (measured:
`free(): invalid size` from glibc's allocator). `ClapHost.cpp` clamped the request to the prepared block and
returned success, which processed the first block and left the rest of the caller's buffers holding whatever
was there before. Both now run an explicit chunk loop: chunks of at most the prepared block, the last one
carrying the remainder, so the request is processed whole; a channel the caller does not supply maps to the
start of the zeroed block or the scratch, never past its end; parameter changes are delivered once, with the
request's first chunk, and MIDI is drained once and sliced per chunk with the offset rebased to that chunk - including the boundary case the pre-chunking host handled with `std::clamp(offset, 0, frames)`: an event at or beyond the end of the request is delivered with the last chunk at its end offset, so a note-off written at a block boundary still releases the note.
The rule is written on `HostedPlugin::process()` in both host headers.

Observable: **`plugin.host_chunking`** (read-only) reports the contract and the counters both hosts increment —
process() calls, frames asked for, plug-in calls they became, requests that needed more than one chunk and the
frames beyond the prepared block they carried, the largest request and the block size the last one was
prepared with. `chunks` > `requests` with a non-zero `frames_beyond_prepared_block` is a request that was
chunked rather than truncated or over-run.

Proven by `Vst3ChunkProbeTest` and `ClapHostTest::testChunkedProcessing` against a purpose-built in-tree MIT
fixture that reports what it was asked for (`tests/data/vst3-chunk-probe`, and the same witnesses added to the
CLAP fixture `tests/data/clap-test-plugin/clap-test-gain.c`). Measured, 1061 frames into a 512-frame block:
chunk sequence 512, 512, 37; no call larger than the declared block; the full request written; and a second
instance prepared for 1061 frames producing identical audio. With the chunk loop removed the same test fails
on both counts and aborts in the allocator.

## A shared WASM worker pool and a deterministic offline render (`wasm.pool`, `wasm.render_offline`, CODE-5, row 73) — added 2026-09-15

The WASM worker owned a `std::thread` and a `sleep_for(200us)` polling loop, so N hosted modules cost N
threads and a queued block waited for the next tick. `WasmWorkerPool` is now ONE process-wide pool of bounded
lanes (`min(cores - 1, 8)`) that every worker shares: a lane claims one worker at a time, drains its queue in
FIFO order and releases it, so a stateful module's blocks are processed in submission order while different
workers run on different lanes. A lane parks on a work generation and `submit()` wakes it — a real wake-up, and
while a lane is already awake the audio thread takes no syscall at all.

`wasm.render_offline` renders a module offline — one block in flight, on a fresh worker, so nothing can be
reordered or dropped — and reports whether the render is reproducible **within a measured tolerance**. This is
deliberate: `docs/RENDER-DETERMINISM.md` records that this tree's renders are not bit-reproducible run to run
for every project (period-boundary differences, up to one frame of start jitter, two bundled projects still
unstable). So the command renders the same input twice on an inline control path to MEASURE this build's own
run-to-run floor in the same call, renders it two to eight times through the pool as the subject, and answers
`deterministic` when the subject is within that floor in both differing frames and largest absolute difference.
The SHA-256 digests it returns are informational; the comparator is self-tested on a one-sample perturbation,
so "the same" is a measurement and not a constant.

Observable: **`wasm.pool`** (lanes, workers, lane passes, blocks, wake-ups, wake-ups suppressed, parks) and
**`wasm.render_offline`**, both read-only with their A16 rows in
`src/core/ControlReversibilityTableWasmRender.cpp`. Proven by `WasmWorkerPoolTest`, which measured on this box:
3 hosted workers on 8 shared lanes; with every lane parked, a `submit()` woke one and the block came back
through `collect()`; floor 0 of 8192 frames differ and subject 0 of 8192 (so the verdict is the measured one),
while a 0.25-scale vs 0.75-scale stimulus differs on 8160 frames — the comparator and the render are both
exercised.

## Not in this draft yet

The Session View, racks, comping, MPE modulation, Link sync, browser search and the engine-gap items of the
0.3.0 scope, plus the release-bar statements, are the responsibility of their own lanes and wave W12. This
file grows as those land; it is not a summary of 0.3.0 and must not be read as one.

## The recording engine surface: an arbitrary input count, a drivable multi-track recorder, and a retro window

Three feature rows of `docs/FEATURE-LIST-0.3.0.md` that were one chain, landed together: **row 64** (arbitrary
input count / multiple simultaneous inputs), **row 14** (multi-track recorder) and **row 16** (retrospective
audio capture). `docs/RECORD-INPUTS.md` is the design record; `docs/RETRO-AUDIO-CAPTURE.md` is the audio
window's, next to the MIDI half's `docs/MIDI-RETRO-CAPTURE.md`.

- **The ALSA backend has a capture path for the first time.** `src/core/audio/AudioAlsa.cpp` contained **no**
  `snd_pcm_readi`: under ALSA `AudioEngine::inputBufferFrames()` was always 0, so every record route took zero
  inputs. A second PCM is now opened for `SND_PCM_STREAM_CAPTURE` **on its own thread** (the read blocks, and
  a blocking read on the playback thread would stall the render), with FLOAT preferred to S16_LE and a bounded
  200 ms wait so the stop flag is honoured on a device that has gone quiet. Every buffer it touches is
  allocated before the thread starts.
- **The input count is arbitrary, and it is the *channel* count.** `record.input_set` writes
  `audioinput/{device,channels,left,right}`; `channels` is 1..32 and is the number of device channels the
  backend captures. The engine keeps all of them (an N-channel staging ring, `AudioWideInputStage`) and puts
  the configured **pair** on the stereo bus the rest of the engine reads, so an interface's third and fourth
  input can be what the engine hears.
- **`track.set_arm` is no longer a refusal stub — it arms a real capture.** The audit's row 14 named the
  contradiction exactly: a real recorder in the tree with no command that could start it, and the id that
  should, registered to refuse. It now starts a capture on the record route a song track's position maps to,
  writes a 24-bit WAV, **journals the take** beside it (so a crash mid-take is offered to the next start by
  `record.recovery_get_state`), and is taken back by `control.undo` through its recorded inverse. **No field
  is added to the track's serialization format** — the arm state lives on the recorder, where it always did,
  which is the objection the old refusal was right to raise.
- **The recorder is drivable.** `record.get_state`, `record.arm_track`, `record.disarm_track` and
  `record.disarm_all` address up to `MultiTrackRecorder::MaxRoutes` (16) routes, each able to select **any**
  input channel in `[0, input_channel_capacity)`. A route is a file plus a channel; several routes may record
  one channel.
- **Retrospective AUDIO capture.** `record.retro_capture_arm` / `_status` / `_to_take`: off by default, one
  bounded window of the most recent 2^20 frames (~21.8 s at 48 kHz) of the engine's input bus, drop-oldest
  with the overwritten count reported, a consistent copy that never blocks the audio thread, and a one-pass
  stereo 24-bit WAV of exactly the retained frames. The model is `RetroMidiCapture`'s, deliberately.
- **A16.** Ten rows, in the new group file `src/core/ControlReversibilityTableRecording.cpp`: `snapshot` for
  `record.arm_track` and the re-classified `track.set_arm` (each with a paired-command inverse), `true_inverse`
  for `record.input_set` (the config write's previous plan), and seven `not_mutating` rows for the two
  inspectors, the three writers whose only output is a file, and the retro mode.
- **Proof.** `RecordingInputPathTest` and `RetroAudioCaptureTest` (registered ctests, no hardware: N routes ×
  N channels with each take read back sample-exactly, and the retro window's bounds, non-allocating producer
  path and empty-window refusal) plus `ControlRecordInputs` (`tests/control-record-inputs.py`), which drives
  the **real binary** over `--control-socket` and proves the arbitrary input count **across a restart** — a
  route armed for input channel 7 is refused by the instance that started with two channels and accepted by
  the next one, which is what `restart_required` means.
- **UI absence — one line: the recording engine surface is drivable through the socket and nothing in the
  interface reaches it.** There is no input-device picker for capture, no input-channel selector on a track,
  no arm button bound to `track.set_arm`, and no retrospective-audio control at all; `docs/KNOWN-LIMITATIONS.md`
  carries the sentence and the bounds.
- **And the honest limit, in the same voice as everything else here: the real-interface half is hardware-bound
  and unverified on this box.** Whether a sound card opens, how many channels it grants and whether it delivers
  frames are this machine's answers and not properties of the feature — which is why they are *reported* by
  `record.input_get_state` (`capture_capable` / `capture_open` / `capture_reason`, the granted channels and
  rate, and the `bus_frames` / `wide_frames` / `input_frames_staged` counters) rather than assumed, and why
  the ctest asserts those fields for internal consistency rather than for a value. The
  `TwoTrackAlsaCaptureProbe` (task #556) remains the real-hardware probe: it drives libasound directly and
  needs a card, a cable and a human.

## Chord track, chord detection, progression tools, generators (feature row 35)

- **A chord TRACK that persists in the project.** An ordered list of chords — position, length,
  root pitch class, the octave the root sounds in, the chord's name and the key it was written
  in — saved as **one `<chord-track>` element inside `<song>`**, written **only when it holds a
  chord**, so a project that never used one re-saves the bytes it always had, and cleared by
  `Song::clearProject()`. The position is the key (a set at an occupied tick replaces), `length`
  0 means "hold until the next chord", and a name outside the vocabulary is refused rather than
  stored. Bounds: 64 events.
- **No second scale vocabulary.** Every name — chord and scale — comes from
  `InstrumentFunctionNoteStacking::ChordTable`, the 95 entries behind the piano roll's own chord
  and scale selectors, through a read-only view (`include/ChordVocabulary.h`). This feature adds
  no table; adding an entry to the piano roll's table adds it here.
- **Chord DETECTION.** `chord.detect` reads a clip's notes, groups them into slices (notes that
  start together; `window_ticks` for a strummed take), names each slice from the vocabulary with
  its root, its root key, its bass, the tones it MISSES and the tones it ADDS (`exact` is false
  when either is non-empty — a name is never rounded), and reports the clip's key. Proven on a
  known clip: `ControlChordCommandsTest::aKnownClipDetectsTheChordItSpells` builds C-E-G through
  `note.add` and reads back `Major`, root C, key 60, exact. `chord.detect_to_track` writes a
  detection onto the track, and a detection that names nothing is refused rather than
  half-written.
- **Progression tools and a SEEDED, REPEATABLE generator.** `chord.progression_generate` walks a
  named progression's SCALE DEGREES, builds each chord by stacking the scale's own tones in
  thirds and names it from the table (`I-V-vi-IV` in C major is Major / Major / minor / Major),
  then lays it out as block, arpeggio up, arpeggio down or broken, into a clip. The draws —
  voicing, timing, velocity — are `NoteRandom::rollUnit` over the seed and each chord's own
  identity, so **the same request with the same seed reproduces the take note for note and a
  different seed gives a different one** (the repeatability pair, asserted in
  `ChordProgressionTest` and again through the surface in `ControlChordCommandsTest`), while
  `variation` 0 draws nothing and the seed decides nothing. `chord.track_write` turns the chord
  track's own chords into notes under the same layout.
- **Nine ids, all drivable, each with an A16 row.** `chord.get_state`, `chord.detect`,
  `chord.progression_list` (reads); `chord.set`, `chord.remove`, `chord.clear`,
  `chord.detect_to_track` (track edits, reversed by a recorded action checkpoint);
  `chord.track_write`, `chord.progression_generate` (note generators, reversed by the clip's own
  journal checkpoint). Rows in `src/core/ControlReversibilityTableChord.cpp`.
- **Proof.** `ChordTrackTest`, `ChordDetectTest`, `ChordProgressionTest` (the engine arithmetic
  with no Engine at all) and `ControlChordCommandsTest` (the surface: schemas, typed refusals,
  both inverses through `control.undo`, the known-clip detection, the repeatability pair read off
  the wire, and the project file with the element present when the track holds a chord and ABSENT
  when it does not). Design and reproduction: `docs/CHORD-TRACK.md`.
- **UI absence — one line: the chord track is drivable through the socket, not from the
  interface.** There is no chord lane, no chord ruler and no generator panel; `grep -rniI
  'ChordTrack\|chord-track' src/gui/` returns **0** hits, and `docs/KNOWN-LIMITATIONS.md` carries
  the sentence and the bounds above.
- **What this does NOT have, stated rather than implied:** no chord detection from AUDIO (the
  detector reads notes), no time-varying key analysis (the key is one estimate for the whole note
  list), no roman-numeral analysis of arbitrary chord sequences, and no chord track that sounds on
  its own — it is harmony written down, and `chord.track_write` is what turns it into notes.

## Stable ids — slice 2: clip-, note-, ch-, fx- persist through save/open (feature row 51)

- **`trk-<n>` was slice 1 (row 50).** This slice makes the remaining four document-object families persistent the same way: the id is assigned once at construction, written as an `id` attribute on the object's own element, and read back on load.
- **`clip-<n>`** persists on `<midiclip>`, `<sampleclip>`, `<patternclip>` and `<automationclip>` elements. A cached clip id survives sibling insert, delete, split, reorder and undo.
- **`note-<n>`** persists on `<note>` elements. A note's id survives `rearrangeAllNotes` re-sorting the list.
- **`ch-<n>`** persists on `<mixerchannel>` elements, beside `num` (which remains the positional index). A channel's id survives add, remove and move.
- **`fx-<n>`** persists on `<effect>` elements. An effect's id survives append, remove and reorder in its chain.
- **`dev-<n>`** is reclassified as a catalogue selector: it names a build's `plugin.list` entry, not a project object, and is intentionally NOT written into the project file.
- **Proof.** `tests/control-stable-ids-slice2.py` asserts identical ids before and after `project.save` / `project.open` for each persistent family, and asserts that a clip delete does not renumber siblings. `SKIP_RETURN_CODE 77` when the build ships no loadable effect (the fx-<n> family cannot be exercised).
- **UI absence — one line: stable id inspection is drivable through the socket, not from the interface.** There is no id column in the track list, the clip list, the piano roll, the mixer or the rack; `control.id_contract` is the only way to read the contract and the counts.
- **A copy is a new object (contract rules R4/R5).** Duplicating a track, Ctrl-dragging a clip, pasting notes or applying a device preset creates NEW objects with NEW ids: the payload carries the source's attributes verbatim, so the reader deliberately ignores the id in it. Only two things preserve an id: the object never moving (it is the same object) and a restore from the document (a save/open, an undo of a delete).

### mmpz-git depth (#612)

- **Three-way merge of concurrent track edits** with a musical (not textual) conflict presentation. The merge driver uses deep whole-subtree fingerprints so a delete or rename on one side can never silently discard an edit nested below it. Conflicts are reported as `track "Bass" > pattern "I" > note F#1 at bar 1 beat 1`, not as XML noise, and marked in the file as machine-readable comments.
- **Large-asset handling.** Embedded samples and plugin state chunks are summarised by hash in conflict comments; the full value is preserved in a `.mmpz-git-conflicts.json` sidecar so the project file does not bloat. The sidecar is named from the work-tree path git passes (`%P`) when git runs the driver, so a conflicted `git merge` leaves the full values beside the project file and never on git's transient `.merge_file_*` path.
- **Audible-diff CLI** (`project.audible_diff`): renders two projects and reports which bars of which track differ, with per-bar RMS and peak-difference metrics.
- **CI render recipe** (`tools/mmpz-git/render-recipe.sh`): headless render with unpiped exit codes, SHA-256 of output, and refusal on empty renders.
- **Four control-surface ids:** `project.merge`, `project.diff`, `project.conflicts`, `project.audible_diff`, each with argument/result schemas and A16 reversibility metadata.
- **Proof:** `MmpzGitDepthTest` (registered ctest: the Python test suite over real project files, including a git-driven end-to-end merge that asserts the large-asset sidecar lands beside the project) and `bash tools/mmpz-git/depth-demo.sh` (a rerunnable transcript with unpiped exit codes that builds branches from one real project, merges them, and asserts the merged documents: the different-track merge, the same-note conflict, the delete-vs-nested-edit silent-loss class, and the embedded-sample conflict with its eight document checks).
- **UI absence — one line:** drivable through the socket, not from the interface.

### MIDI controller surfaces — soft-takeover, LED/feedback output and mapping templates

- **Ids:** `controller.surface_state`, `controller.soft_takeover`, `controller.feedback`,
  `controller.template_save`, `controller.template_list`, `controller.template_apply`,
  `controller.template_delete` (group `controller`, registered in
  `src/core/ControlRegistryRegistrations.cpp`, declared in `include/ControlRegistryGroups.h`).
- **Engine half:** `include/MidiController.h` + `src/core/midi/MidiController.cpp` (the soft-takeover
  crossing gate in `processInEvent`, and the LED/feedback write), `include/ControllerSurface.h` +
  `src/core/ControllerSurface.cpp` (the template store), and two output counters on `MidiPort`
  (`include/MidiPort.h`) plus the `MidiControlChange` case in `MidiClientRaw::processOutEvent`
  (`src/core/midi/MidiClient.cpp`) — without that case a feedback write reached the client and produced
  a `qWarning` per write instead of three bytes.
- **A16:** five `snapshot` rows with `reversible = false` and two `not_mutating` rows, in their own
  translation unit `src/core/ControlReversibilityTableController.cpp`, joined by one entry in
  `src/core/ControlReversibilityTable.cpp`. The surface flags live in the model's own `<connection>`
  element (so no `ProjectJournal` checkpoint holds them) and a template is a file outside the project,
  so an undo attempt is refused, typed, and names the inverse command.
- **Proof.** `ControllerSurfaceTest`, registered in `tests/CMakeLists.txt`, driven entirely through the
  synthetic-CC entry point (`MidiLearn::handleMidiEvent` / `MidiPort::processInEvent`): the soft-takeover
  gate and its take-over-off negative control, the feedback write measured at the MIDI-client boundary
  plus its feedback-off negative control, the template save/list/read/apply round trip with the flags
  intact, what a template reports it cannot resolve, and the surface flags through the project's own
  serialisation.
- **UI absence — one line: the controller surface is drivable through the socket, not from the
  interface.** There is no soft-takeover toggle, no feedback switch and no template menu;
  `grep -rniI 'ControllerSurface\|softtakeover\|controller\.template_' src/gui/` returns **0** hits, and
  `docs/KNOWN-LIMITATIONS.md` carries the sentence and the bounds above.
- **What this does NOT have, stated rather than implied:** **OSC is out**; **no hardware was attached
  when this was written**, so the LED half's strongest claim is a measured write reaching the output
  client (the dummy client's `sendByte()` is a no-op) and nothing here proves a lamp lit or a motor
  moved; and no motorised-fader return path is proved at all.

## The control socket on Windows: the same contract over a named pipe (CODE-9, feature row 83) — added 2026-09-15

- **Same contract, different kernel object.** On Windows `--control-socket <path>` now listens on a
  **named pipe** (`\\.\pipe\<name>`) instead of refusing, and it serves the **identical**
  line-delimited JSON-RPC surface: one request line in / one response line out, the same command ids,
  the same typed refusals (`invalid_args`, `not_found`, …), the same `control socket listening on
  <path>` start line, the same 1 MiB request-line cap and the same over-cap refusal sentence. Nothing
  on the wire tells a client which kernel object it is talking to. **No new command ids and no new
  A16 rows**: this is the same surface over a second transport, so the surface's ids and the
  reversibility table are exactly what they were — the smoke test reads the id list off the pipe and
  checks it against the ids the surface has always carried.
- **Where the code is.** `src/core/ControlServerWin32.cpp` (new, every line inside
  `#if defined(Q_OS_WIN)`) implements `listenWin32`/`closeWin32`/the accept loop/per-connection
  thread/the dispatch bridge; `ControlServerSocket.cpp`'s platform branches hand the Windows case to
  it and report its refusal through the same `fail` lambda the POSIX path uses; the POSIX
  transport itself is **unchanged** (see the proof below).
- **The design, in one sentence each.** A thread per accepted connection, so no blocking pipe call
  ever runs on the server's thread; **every** wait is on an event this code owns and every pipe
  operation is OVERLAPPED, so `close()` can stop any thread without closing a handle a thread is
  inside; `dispatchLine()` **always** runs on the server's thread (a queued call), because the
  control surface, the engine and the journal are single-threaded by construction on POSIX too; and
  the client thread's wait for that answer is **bounded**, so a closing instance can always finish
  joining its threads.
- **Local-only by construction.** The pipe is created with `PIPE_REJECT_REMOTE_CLIENTS`: a client
  cannot reach it over `\\<host>\pipe\...`. Windows has no `chmod`/inode equivalent, so the POSIX
  rules "mode 0600" and "unlink only the socket this instance bound" are **stated as absent** in
  `docs/KNOWN-LIMITATIONS.md` rather than pretended.
- **Proof — and its shape.** `ControlNamedPipeSmoke` (`tests/control-named-pipe-smoke.py`, registered
  under `if(WIN32 AND PYTHON3_EXECUTABLE)` in `tests/CMakeLists.txt`) starts the real binary on a
  pipe, connects with `CreateFileW`, and checks the framing (two requests in one write → two whole
  reply lines, in order), the ids the surface carries over the pipe, the malformed-line and
  unknown-command refusals, the 1 MiB cap (and that the listener survives that connection), the
  launcher-visible refusal for a path that is not a pipe name, and the shutdown (`control.quit`
  answers, the process exits, the name is gone). **This is CI-only evidence**: the transport was
  written on a box with no Windows toolchain, and the `msvc-x64` job is where it is compiled and run.
  What is proven locally instead is that the **POSIX path is byte-for-byte unchanged**: the three
  control translation units preprocess token-for-token to `release/0.3.0`, and the new Windows TU
  preprocesses to nothing on POSIX (`docs/CONTROL-NAMED-PIPE.md` carries the exact commands).
- **UI absence — one line: the Windows transport is drivable through the socket, not from the
  interface.** `grep -rniI 'control-socket\|ControlServer\|controlSocket' src/gui/` returns 2 hits,
  both comments about unattended runs; there is no pipe-name field, no control-surface page and no
  Windows-specific menu entry. `docs/KNOWN-LIMITATIONS.md` carries the sentence and the bounds.
- **What this does NOT have, stated rather than implied:** no Windows-side path-safety rules beyond
  the name check (there is no filesystem object to protect), no per-connection thread pool (one
  thread per connection, retained until the listener closes), no equivalent of the POSIX write
  notifier (a peer that stops reading a reply blocks that connection's thread on the pipe buffer
  instead of queueing), and — for this lane — **no local execution of the Windows half at all**.

## The Lua API's DAW-control half: a script can drive the mixer, and the version policy is a ratchet (feature row 50, task #674) — added 2026-09-15

The Lua binding shipped as a **pattern-editing** API: notes, patterns, a track's name and
volume, transport, files, MIDI. It reached **no** mixer channel, effect chain, plugin, send,
PDC, automation clip, controller or settings object, which is what the audit measured as row
50 at 66%. This is the DAW-control half of it.

- **`zene.mixer()` and the four new classes.** `Mixer` (`channelCount`, `channel(id)`,
  `channelById("ch-3")`, `master()`, `ids()`, `addChannel()`), `MixerChannel` (`gain`/`setGain`,
  `muted`/`setMuted`, `soloed`/`setSoloed`, `name`/`setName`, `isMaster`, `isBus`, `chain()`,
  `sendCount`/`sendTarget`/`sendAmount`/`sendPreFader`), `EffectChain` (`effectCount`,
  `effect(i)`, `effectById("fx-2")`, `loadEffect("dev-7")`, `removeEffect(i)`) and `Effect`
  (`id`, `pluginName`, `enabled`/`setEnabled`, `parameterCount`/`parameterName`/`parameter(i)`
  — the parameter is the engine's own `AutomatableModel`, so it is the same object
  `plugin.param_get` reads).
- **One implementation, two surfaces.** A script addresses the channel the control surface
  addresses: the ids are `ch-<n>` / `fx-<n>` from `ControlVocabulary.h`, the chain is the same
  `EffectChain` `resolveControlTarget()` returns, and a device is loaded through the same
  `controlInstantiateDevice()` `plugin.load` uses. The proof asserts that identity rather than
  assuming it (`ScriptDawBindingTest`).
- **Writes are queued, never applied on the script's thread.** Engine state is mutated by the
  apply side only: the fixed command vocabulary grew by ONE type (`ScriptCommand::Type::DawEdit`,
  an opcode in `i0`) because `ScriptEngine::applyCommand` is a grandfathered complexity-ratchet
  entry — the op dispatch lives in `src/core/ScriptDawEdit.cpp`, and two note-edit case bodies
  moved out of the switch so the entry does not move.
- **Undo.** A gain/mute/solo write queues the channel model's own `addJournalCheckPoint()`
  immediately before the model write (the same call `mixer.set_volume` makes), a created channel
  records the action step that deletes it, and a loaded effect records the action step that
  unloads it — so one `control.undo` (or Ctrl+Z) reverses what the script did. The committed
  test proves the fader case end to end against `ProjectJournal`.
- **The version + compatibility policy is enforced, not described** (it was prose plus a
  script-side header gate). `zene.apiSurface()` reports the version and the LIVE `zene` surface,
  and `tests/lua-api-surface.py` (registered ctest **`LuaApiSurface`**) derives the whole surface
  — every namespace function and every class member — from the registration sources and fails on
  drift in EITHER direction against the committed `docs/lua-api-surface.txt`: a removed or renamed
  name is a breaking change (bump `ZENE_LUA_API_VERSION_MAJOR`), an added name is additive (bump
  `ZENE_LUA_API_VERSION_MINOR`). The test names each entry and prints the policy. It needs no
  build of the DAW, so it runs everywhere.
- **The version moved to 0.2.0** (was 0.1.0) because this change is additive, which is what the
  policy says to do; every `--! zene-api 0.1` script keeps running on this build, and a 0.2
  script is refused by a 0.1 build rather than misbehaving.
- **The console was already there and stays an output path.** `ScriptConsole` streams every
  captured line onto the DAW's Qt log path (`lua:` prefix) and `--run-script` turns it off so
  stdout is not printed twice; `script.run` returns the captured lines as `log`. No dock widget
  is added here — that is still future work.
- **UI absence — one line: there is no console pane, no script editor and no GUI control that
  drives the binding.** The only interface path to a script is the pre-existing File > Run Lua
  Script action (`src/gui/MainWindow.cpp:942`, a file dialog that hands the file to the engine),
  and nothing in the interface shows what a script changed: `grep -rniI 'luaConsole\|LuaConsole\|
  scriptEditor' src/ include/` returns **0** hits, and `src/gui/` mentions Lua in three places
  (two of them that dialog). `docs/KNOWN-LIMITATIONS.md` carries the sentence and the withheld
  list.
- **Deliberately withheld, one line each** (`docs/LUA-API-STABILISATION.md` §5): **channel pan**
  (a `MixerChannel` carries no pan in this tree — `mixer.set_pan` refuses for the same reason),
  **channel and effect removal** (a deleted channel has no inverse; the socket's
  `mixer.remove_channel` row is `irreversible` and the binding does not add a one-way door to
  Lua), **sends** (read-only: creating or moving a send changes routing and is `mixer.route_*`'s
  job, not a script's), **PDC** (not bound at all: latency is a property the engine derives per
  chain and `dsp.get_state` reports, not something a script may set), **plugins beyond the chain**
  cannot scan, instantiate a device outside a chain, or publish a preset), **automation clips
  and controllers** (`automation.*` and the modulator groups own those objects; the binding
  reaches neither), **settings** (`script.set_memory_budget` remains the only knob a script
  owns), and **a channel rename is journalless** (`MixerChannel::m_name` is a plain `QString`;
  the binding performs the rename because a script that creates a channel has to be able to
  name it, and claims no inverse for it).

## MCP tooling: the ten invisible groups are driven, and a stale offline copy now says so — added 2026-09-15

- **Ten command groups had no MCP tool at all** (`browser`, `comp`, `export`, `link`, `modulator`, `rack`,
  `session`, `telemetry`, `warp`, `wasm` — **74 ids** invisible, feature-list row 49), because the registered
  bridge served a stale 70-id 0.1.0-alpha list while the tree's snapshot already carried 144. Nothing in the
  bridge needed a per-feature fix — it generates a tool per id it reads from a live instance
  (`zene_control/registry.py`) — so what was missing was the measurement. The registered ctest
  **`ControlMcpGroupCoverage`** (`tests/control-mcp-group-coverage.py`) now starts the real binary, opens a
  real MCP stdio session over its socket, and drives **one command from each of the ten groups**, with the
  arguments each command's own schema requires filled from the instance's own state through further MCP calls
  (`track.list`/`track.add`, `mixer.get_state`/`mixer.add_channel`, `arrangement.get_state`/`clip.add`).
  Measured against a build of the integration tip: **265 ids across 43 groups reachable live** (267 tools with
  the two bridge-owned ones) against the snapshot's **144 ids across 27 groups**, with nine of the ten groups
  driven end to end (`browser.query`, `comp.lane_list`, `export.get_settings`, `link.get_state`,
  `modulator.get_state`, `rack.get_state`, `session.get_state`, `telemetry.status`, `warp.list`). `wasm.` is
  excused by a `--compiled-out` flag checked in **both** directions (a flag describing a group the binary
  *does* register is a failure), because no configuration on this machine compiles the wasmtime sandbox in;
  a wasm-enabled build receives no flag and must drive the group against the binary, and the bridge's own
  half — a declared group becomes tools and forwards verbatim — is
  `tools/mcp-zene-control/tests/test_declared_surface.py` (3 tests, stand-in socket, ids read from
  `src/core/ControlCommandsWasm*.cpp`).
- **An offline copy's staleness is now detectable instead of silent.** Every command-list bundle records the
  surface it describes — `id_count`, `group_count`, `ids_sha256` (`registry.surface_fingerprint`) — and
  `registry.surface_drift` measures a copy against a live list, naming the ids that differ.
  `zene_status` and `zene_commands` carry that as `offline_drift` whenever an instance is answering, so the
  question "is the list I would be served with nothing running still current?" has an answer at the point of
  use; `snapshot_commands.py` prints the surface it just captured. The flag is checked in **both** directions:
  `tests/control-mcp-group-coverage.py` asserts it equals the truth the script computes for itself, and
  `tools/mcp-zene-control/tests/test_offline_staleness.py` (12 tests, no socket needed) pins the four cases —
  a missing live id, an id the instance does not register, an identical surface (must NOT be stale, or the
  check becomes a permanent red light), and a gap longer than the report's sample limit, which is counted in
  full and listed to the limit.
- **UI absence — one line:** none of this is in the interface either; the drift report exists only through the
  MCP bridge (and `snapshot_commands.py`), and the deployment limit above is unchanged.
