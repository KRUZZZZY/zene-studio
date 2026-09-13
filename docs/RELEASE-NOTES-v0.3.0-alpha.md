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
  reversibility rows for all seven ids (`src/core/ControlReversibilityTable.cpp` and
  `...Passive.cpp`). Every mutating call takes the object's own ProjectJournal checkpoint before it writes, and
  every refusal is typed and happens BEFORE the checkpoint, so a refused call leaves no undo step behind.
- **Proof:** the registered ctests `TakeLaneTest` (`tests/src/core/TakeLaneTest.cpp`, the take-lane half) and
  `TakeLaneCompTest` (`tests/src/core/TakeLaneCompTest.cpp`, the composite half) — the ten claims
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

## The A16 contract table, and its histogram

The SPEC A16 classification table holds **139 rows**, measured from the table itself:
**71 `true_inverse`, 9 `snapshot`, 3 `irreversible`, 56 `not_mutating`**. With the telemetry
client compiled out (`-DZENE_TELEMETRY=OFF`) the two `telemetry.*` rows leave with their
commands, giving **137 rows / 54 `not_mutating`**. `ReversibilityContractTest` asserts both
sets, so a row added or moved between classes cannot ship with this page quoting the old
split. (The 129-row figure this page carried before the modulation layer landed was its ten
rows short - six `modulator.*` action-checkpoint rows, two `not_mutating` rows
(`modulator.get_state`, `note.expression_get`) and the two `note.expression_*` `true_inverse` rows. The 117-row figure
before the comping and tempo-map lanes landed was
their twelve rows short - five `comp.*` `true_inverse`, two `comp.*` `not_mutating`,
`transport.tempo_map_get` and the four `transport.tempo_map_*` edit rows that the merge which
took the w11 table split dropped (they were written into the retired
`ControlReversibilityTableTrueInverse.cpp`); the detector and
`ControlTempoMapCommandsTest::contractRowsClassifyTheGroup` both caught it, which is what they
are for.) At 0.2.1 the same
four counts were 30 / 5 / 3 / 36 over 74 rows
(`docs/RELEASE-NOTES-v0.2.1-alpha.md`) - that record is left as written.

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
  a `MidiClip` checkpoint. **Only the pitch axis is applied by playback**, which is `#601`'s own
  stated limit (`docs/MPE.md` §4), not a new one.
- **UI absence — one line: modulators and per-note expression are drivable through the socket, not
  from the interface.** Nothing in `src/gui/` creates, draws or edits a modulator or a note's
  expression. `docs/KNOWN-LIMITATIONS.md` carries the same sentence.
- **Proof:** the registered ctests `ModulationLayerTest` (the four LFO shapes, the source validation,
  the layer's bounds, the target resolver, the relative write against two ranges, the clamp, the
  no-op paths, **0 allocations over 64 blocks**, the base restore) and `ControlModulatorCommandsTest`
  (the ten ids with their schemas, the A16 classes, the typed refusals, `bindDriveUndoAndRebind` —
  create, bind, apply a real block, `control.undo`, and re-bind — and the `note.expression.*` round
  trip with its undo). Every mutating call records its SPEC A16 class (`true_inverse`: an action
  checkpoint for the layer, a `MidiClip` checkpoint for a note's expression).
- **Stated limits.** Modulation is applied **once per audio block** (about 11 ms at the default
  period), not sample-accurately; the source is an **LFO only** (no envelope follower); targets are
  device parameters inside a mixer channel's rack chains, so a route cannot name the Song's own master
  gain or an instrument's parameters in this release; and while a modulator is active the parameter's
  own control is taken over — the value you see is the base, and the modulator's offset is on top of
  it until the modulator is deactivated, removed or `control.undo` takes the edit back.

## Not in this draft yet

Written per lane as it lands, so this list is state as of **2026-09-13**; W12 owns turning this
file into the user-first notes. **In and described above:** warp marker editing, export dither and
SRC quality, rack macros and key/velocity zones, clip fades/crossfades/gain, browser tag/metadata
search with its peak cache, bounded coalescing undo, comping, the tempo map, and the modulation
layer with per-note expression - plus the Session View and the
process items, whose sections W12 adds.

**Still absent from 0.3.0's scope:** Ableton Link sync,
sample-accurate automation, freeze/bounce-in-place, groove pool and quantise, punch in/out, tempo
automation and time signatures, recording crash recovery, the two verification programmes
(real-time-safety and golden-audio), `#614` (doc-only; wasmtime is absent here) and `ARCH-2`. Each
gets a section here and a line in `docs/KNOWN-LIMITATIONS.md` when it lands. This file grows as
those land; it is not a summary of 0.3.0 and must not be read as one.
