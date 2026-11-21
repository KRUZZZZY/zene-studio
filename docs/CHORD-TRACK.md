# The chord track (0.3.0-alpha, feature row 35)

**What it is.** A chord TRACK — an ordered list of chords that persists in the project — plus
detection (what chords a clip's notes spell) and two generators that write notes from harmony
into a clip. The feature-list row is `35 | Chord track, chord detection, progression tools,
generators`, and this document is the reproduction: where the entity lives, what the vocabulary
is (and is not), what the seeded generator guarantees, and the bounds.

## 1. The vocabulary is the one the product already has

There is **no new scale or chord table** in this feature, and that is a requirement rather than a
nicety. The names, intervals and the chord/scale split are
`InstrumentFunctionNoteStacking::ChordTable` — the 95 entries behind the piano roll's own chord
selector and scale selector (`src/gui/editors/PianoRoll.cpp` reads `ChordTable::getInstance()`
for both). `Chord::isScale()` is `size() > 6`, so a "scale" is an entry with more than six tones.

`include/ChordVocabulary.h` (+ `src/core/ChordVocabulary.cpp`) is a **read-only view** over that
table: `chordNames()`, `scaleNames()`, `chordByName()`, `scaleByName()`, `pitchClassesOf()`,
`relativePitchClasses()`, `toneOffsets()`, `exactChordName()`, `bestFit()`, `coveringScale()`,
and the two key helpers (`keyOf()` — Note.h's own convention, so `keyOf(0, 4)` is middle C —
and `pitchClassOf()`). Nothing in it is a table; every function is a loop over the table the
piano roll reads.

Two consequences worth stating:

* a chord this feature names is a chord the piano roll names the same way, and an entry added to
  the table appears here with no second edit;
* `ChordVocabulary.h` is the ONE place the chord arithmetic is derived, so the track, the
  detector and the generator cannot disagree about what a name means.

## 2. The entity: `ChordTrack` (`include/ChordTrack.h`)

One chord event per position:

| field | meaning |
|---|---|
| `pos` | where the chord starts, in ticks. **The position is the key**: a set at a tick that already holds a chord REPLACES it. |
| `length` | how long it sounds; **0 means "hold"** — until the next chord, or the caller's own fallback for the last one (`ChordTrack::effectiveLength`). |
| `root` | pitch class 0..11 (C = 0). |
| `octave` | the engine's octave (Note.h: octave 4 holds middle C), so the sounding key is `keyOf(root, octave)`. |
| `chord` | a CHORD entry's name (`isScale() == false`). |
| `scale` | the key the chord was written in, a SCALE entry's name, or empty. |

Bounds, all reported on the wire: **at most 64 events** (`MaxEvents`; past it a NEW position is
refused and replacing an existing one is still allowed), positions non-negative, events kept in
ascending position order, and a name that is not in the vocabulary is **refused rather than
stored** (`ChordTrack::isWritable`) — a track cannot hold a chord nothing else in the product can
read.

### 2.1 Where it lives, and why that decides its reversibility

Project state on the `Song`, serialised as **ONE `<chord-track>` element inside `<song>`** and
written **only when it holds a chord** (`shouldPersist()`), so a project that never used a chord
re-saves the bytes it always had — the rule `TempoMap`, `ModulationLayer` and `GroovePool`
follow. `Song::clearProject()` clears it, so a new project never inherits the previous one's
chords, and `Song::loadProject()` reads the element when it is there.

It is **not** inside the track container and is **not** a `JournallingObject`, so a Song journal
checkpoint does not carry it: the inverse of an edit to the track is a **recorded action
checkpoint** (`control::addUndoStep`) that writes the captured element back through
`ChordTrack::loadSettings`. The two GENERATORS are different: what they change is a clip's NOTE
LIST, and a `MidiClip` IS a journalled object, so their inverse is the clip's own checkpoint.

## 3. Detection (`include/ChordDetect.h`)

A pure function over a `NoteVector`: the notes are grouped into **slices** (notes that start
together; `windowTicks` widens that for a strummed or humanised take, measured from each slice's
FIRST note so a run of sixteenths cannot chain into one enormous slice), each slice's sounding
pitch classes are looked up in the vocabulary, and the nearest entry is reported with

* `chord`, `root`, `key` (the root's absolute MIDI key) and `bass`;
* `missing` (chord tones the slice does not sound) and `extra` (slice tones the chord does not
  contain), so **`exact: false` is visible rather than rounded to a name**;
* `scale`, the covering entry rooted at the reported root.

`detectKey()` reports the whole clip's key: the root is the FIRST note's pitch class (a stated
rule, so the answer is reproducible) and the scale is the most SPECIFIC entry rooted there that
covers every class in the list — a C major run reports `Major`, not `Chromatic`.

Stated limits: a single tone is not a chord (entries with fewer than two tones are skipped, so
the table's one-tone `octave` entry never names anything); detection is from notes that sound
together, not from audio; and inversions are reported by the bass key, not by a `/G` suffix in
the name.

## 4. The catalogue and the seeded generator (`include/ChordProgression.h`)

A progression is a named walk over SCALE DEGREES. The chord on a degree is built the way music
theory builds it — stack the scale's own tones in thirds on the degree (the degree, +2, +4,
wrapping the octave) — and NAMED by asking `ChordVocabulary` which entry that stack is. So
`I-V-vi-IV` in C major is `Major, Major, minor, Major` at the keys C4, G4, A4, F4, and a scale
whose stack has no name in the table comes back with an **empty name** (and, for the track
writer, is refused) rather than inventing one.

The catalogue is data (`ChordProgression::catalogue()`): `I-V-vi-IV`, `I-vi-IV-V`, `vi-IV-I-V`,
`ii-V-I`, `I-vi-ii-V`, `I-IV-V-I`, `i-VI-III-VII`, `i-iv-v-i`. `chord.progression_list` reports
it with the vocabularies and the bounds, so a caller asks instead of guessing.

Patterns: `block` (all tones at once, held for the step), `arpeggio_up`, `arpeggio_down`, and
`broken` (the root held for the whole step, the tones above it in the slots that follow). The
layout lives in **`ChordProgression::layOutChord`** — ONE definition, used by both generators, so
a track's chords and a generated progression cannot be laid out differently. No note ever runs
past the room its chord has, so a humanised chord cannot overlap the next one.

### 4.1 The seed, and exactly what it decides

Generation has CHOICES in it: each chord's **voicing** (an inversion, drawn from the chord's own
size), its **position** (a nudge of at most an eighth of the step) and its **velocity** (at most
20 engine units). Every one of them is drawn with `NoteRandom::rollUnit` — the product's own
seeded, stateless hash (the mechanism MIDI depth's probability/jitter and the quantise's
humanise amount already use), keyed by the seed and **that chord's own identity** (its degree,
its root, the step length). Therefore:

* **the same request with the same seed is the same take, note for note** (positions, lengths,
  keys, velocities) — asserted by `ChordProgressionTest::theSameSeedIsTheSameTake` and by
  `ControlChordCommandsTest::theGeneratorIsSeededAndRepeatable`, which generates into two clips
  and compares the two note lists off the wire;
* **the same request with a different seed is a different take** — asserted by the same pair of
  tests;
* **`variation` 0 draws nothing at all**: the take is the progression itself, exactly on the
  grid, at one velocity, and the seed decides nothing. That is stated in the command's own
  description rather than left to be discovered.

The run reports the seed it used, so a caller that liked a take can reproduce it.

## 5. The command surface

Ten ids (nine in 0.3.0; `chord.follow_set` since 0.5.0, §6a), all of them `chord.*` (the ids keep the group prefix; the file split is a read/edit
split, not a group split - and inside the writes it is also the A16 seam: the four track verbs
record an action checkpoint, the two generators reverse through the clip's own, so they are
`ControlCommandsChordEdit.cpp` and `ControlCommandsChordWrite.cpp`):

| id | mutating | inverse |
|---|---|---|
| `chord.get_state` | no | — |
| `chord.detect` | no | — |
| `chord.progression_list` | no | — |
| `chord.set` | yes | recorded action checkpoint; the recorded op names `chord.set` at that position with the previous event's own arguments, or `chord.remove` when the position held nothing |
| `chord.remove` | yes | recorded action checkpoint; the recorded op names `chord.set` with the removed event's arguments |
| `chord.clear` | yes | recorded action checkpoint (the whole element) |
| `chord.detect_to_track` | yes | recorded action checkpoint; when NOTHING could be written the track is put back and the call is refused BEFORE a step is recorded |
| `chord.track_write` | yes | the clip's own journal checkpoint |
| `chord.progression_generate` | yes | the clip's own journal checkpoint |
| `chord.follow_set` | yes | recorded action checkpoint (followers included); the recorded op names `chord.follow_set` with the track's previous mode |

| verb | args (required in bold) | result |
|---|---|---|
| `chord.get_state` | — | `chords`, `max_chords`, `events[]` (`pos`, `length`, `root`, `octave`, `key`, `chord`, `scale`), `followers[]` (`track`, `mode`) |
| `chord.detect` | **clip**, `window_ticks`, `min_pitch_classes` | `clip`, `track`, `count`, `window_ticks`, `min_pitch_classes`, `chords[]`, `key` |
| `chord.progression_list` | `name`, `scale`, `root`, `octave` | `progressions[]`, `count`, `scales[]`, `chords[]`, `patterns[]`, `max_steps`, `max_degree_count`, `max_chords`, `progression`, and - with a `scale` - `suggest_scale`, `suggest_root`, `suggest_octave`, `suggestions[]` (each entry's own `chords[]`, `degree`/`root`/`key`/`chord`, plus `reason` when the scale cannot be walked) |
| `chord.set` | **pos**, **chord**, `root`, `octave`, `length`, `scale` | the track state, `event`, `replaced` |
| `chord.remove` | **pos** | the track state, `event`, `removed` |
| `chord.clear` | — | the track state, `removed` (count) |
| `chord.detect_to_track` | **clip**, `window_ticks`, `min_pitch_classes`, `append` | the track state, `clip`, `clip_track`, `detected`, `written`, `skipped`, `appended` |
| `chord.track_write` | **clip**, `pattern`, `velocity`, `length`, `replace` | `clip`, `track`, `pattern`, `chords_written`, `notes_written`, `notes_replaced`, `note_count` |
| `chord.follow_set` | **track**, `mode` (off / chord / scale) | the track state, `track`, `mode`, `previous`, `changed` |
| `chord.progression_generate` | **clip**, **progression**, `scale`, `root`, `octave`, `tick`, `step_ticks`, `steps`, `pattern`, `velocity`, `seed`, `variation`, `replace` | `clip`, `track`, `progression`, `scale`, `root`, `octave`, `tick`, `step_ticks`, `steps`, `pattern`, `velocity`, `seed`, `variation`, `chords[]`, `notes_written`, `notes_replaced`, `note_count` |

`replace` (default true) clears the clip's notes first and reports how many went; false adds to
them. `chord.clear` on an already-empty track is **refused, typed**: a clear that would change
nothing must not leave an undo step behind. Every mutating verb is one undoable step
(`control.undo`), and the contract rows live in
`src/core/ControlReversibilityTableChord.cpp`.

## 6. Reproducing the proofs

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DWANT_QT6=ON \
    -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --build build --target ChordTrackTest ChordDetectTest ChordProgressionTest \
    ControlChordCommandsTest ControlChordWriteTest -j2
cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R 'Chord' --output-on-failure
```

Measured on the lane's tree (2026-09-15): `100% tests passed, 0 tests failed out of 5`, and the
existing tests this group touches are green with it — `ReversibilityContractTest`,
`ReversibilityUndoTest`, `ControlRegistryTest`, `agent_surface` and `ControlCommandsSnapshot`
(the last two drive the REAL binary over `--control-socket`; the snapshot was regenerated with the
documented tool, 227 -> 236 ids).

* `ChordTrackTest` — ordering, replace-by-position, the 64-event bound, "0 means hold", the XML
  round trip, reset-on-absence, and `Song::clearProject()`.
* `ChordDetectTest` — a C major triad named exactly, inversions/octaves naming the same chord,
  eight vocabulary qualities, an inexact fit reported as inexact, slices and a strum window, and
  the key.
* `ChordProgressionTest` — the catalogue as data, the walk's names and keys, the patterns,
  `layOutChord`, the track events, and **the repeatability pair** (same seed identical, different
  seed different, `variation` 0 seed-independent).
* `ControlChordCommandsTest` — the nine ids and their schemas, the six contract rows, the typed
  refusals, the track verbs, their recorded action checkpoint through `control.undo`, and the
  detection of a clip built by commands.
* `ControlChordWriteTest` — the write half: the generator's repeatability pair read off the wire
  (two clips, one seed, identical takes; another seed, a different take; `variation` 0
  seed-independent), the clip's own checkpoint as the generators' inverse, `chord.track_write`,
  and the project file (the element present when the track holds a chord, absent when it does
  not).
* `ChordProgressionPanelTest` — the panel over the commands (§6b): the catalogue spelt in the
  selected scale, the Write button's `controlCommand`, and a write that is ONE `control.undo` away
  from the clip. `ControlChordProgression` (the socket transcript `tests/control-chord-progression.py`)
  is the same workflow a real client drives through `--control-socket`.

## 6a. 0.5.0 E23: the chord lane, and chord follow

**The chord lane.** The arranger's ruler lanes (`src/gui/ArrangerRulerLanes.cpp`, theme 9) have a
fourth lane under Tempo: each chord a band from where it starts for as long as it sounds
(`ChordTrack::effectiveLength`), labelled with its root and chord ("C Major"), its scale in the
tooltip; a click moves the play position to the chord (`transport.seek`). The lane takes room only
while the chord track holds a chord (so a project without one lays out as before), and
`arrangement.ruler_lanes` gained a `chords` switch (the preference, all four lanes by default). The
lane follows the chord track by `ChordTrack::revision()`, polled every 250 ms, so a chord set,
removed, undone or loaded shows without the model having to signal the interface.

**Chord follow.** `chord.follow_set {track, mode}` makes an instrument track FOLLOW the chord track
while it plays: each note it starts in the arrangement sounds on the nearest tone (down on a tie) of
the chord sounding where the note starts (`mode` chord, the default) or of that chord's scale
(`mode` scale; the chord where the event names no scale); `mode` off stops it. It is the
non-destructive twin of `note.follow_chords`: the play handle's copy of the note moves, the clip
keeps its keys. Where the chord track is silent the notes play as written. The followers are part
of the chord track - `<follow track="<id>" mode="chord|scale"/>` children of `<chord-track>`, so the
element is written when the track has followers even with no chord - and `chord.get_state` reports
them as `followers[]`. `chord.clear` empties the chords and keeps the followers; a new project
drops both. One recorded action checkpoint, like every chord-track edit; setting the mode a track
already has records nothing (`changed` false).

The read is on the audio thread (`ChordTrack::followedKey`, called from
`InstrumentTrack::decorateNoteHandle` through `include/ChordFollow.h`), so every chord-track edit
takes the model-change guard and the tones each event sounds are derived on the editing thread as
two 12-bit masks; the audio read is a scan over integers. Not followed: notes played live from a
MIDI input, and a clip played on its own in the piano roll (its notes have no song position).
Proofs: `ChordTrackTest` (`followedKeySnapsToTheSoundingChord`, `followersPersistWithTheTrack`)
and `ControlChordCommandsTest` (`aTrackFollowsTheChordTrackReversibly`).

## 6b. Q050-019: the progression panel

**The panel.** Edit > Progression (and the footer Tools menu; the registered command
`window.progression`) opens `gui::ChordProgressionPanel`:
`include/ChordProgressionPanel.h`, `src/gui/ChordProgressionPanel.cpp`. It is a view over two
registered commands and holds no music theory of its own.

* **Suggestions.** The scale, key and octave selectors (seeded from `scale.get_state`, the group's
  own context; changing them is local to the panel and is NOT written back) drive
  `chord.progression_list {scale, root, octave}`. Every named progression of the catalogue is
  listed with the chords it would write in that key - each chord named by the SAME vocabulary the
  generator writes with - so a person can read a progression's chords before writing it. That is
  the `suggestions[]` the command reports (§5).
* **The write.** The Write button dispatches `chord.progression_generate` on the clip the shell's
  selection names (`gui::ShellSelection`), with the panel's own scale, key and octave. It is the
  SAME registered command a headless client calls; one undo step is the clip's own checkpoint, as
  §5's table says.

**The bounds, stated.** This is a text preview, not an audition: nothing is played through the
engine before the write (there is no on-the-fly preview render). The target is the selected MIDI
clip - there is no clip picker in the panel, so a clip has to be selected in the arranger (or the
note editor) first, and a write with nothing selected is refused with a message. The panel leaves
the chord TRACK untouched: it writes notes into a MIDI clip, exactly as `chord.progression_generate`
does; `chord.track_write` and `chord.detect_to_track` remain the chord track's own writers. Every
step of a progression is one chord of the named walk at the panel's step (one chord per default
4/4 bar, `block`); the pattern, velocity, seed and variation arguments the command accepts are not
exposed as controls here (they default as `chord.progression_generate` documents).

Proofs: `tests/src/gui/ChordProgressionPanelTest.cpp` (`thePanelShowsSuggestionsForTheSelectedScale`,
`theWriteButtonRunsTheRegisteredCommand`, `writingThroughThePanelIsOneUndoStep`) and the socket
transcript `tests/control-chord-progression.py` (registered `ControlChordProgression`:
`chord.progression_list` spelt in a scale, a write, and ONE `control.undo` restoring the clip).

## 7. What is NOT in this release

*(0.3.0 record; since 0.5.0 E23 the arranger draws a chord lane and playback reads the chord track
for the tracks that follow it - §6a; since Q050-019 the progression panel writes a named
progression into a clip from the interface - §6b.)* Without a chord lane there is no chord ruler and
no chord-edit popover, and the chord track itself still has no editor: `chord.*` is the only way to
write it. The panel writes NOTES into a clip (`chord.progression_generate`), not the chord track.
Also not built: chord detection from AUDIO (this
detection reads notes), a chord track that plays through an instrument on its own (the track is
harmony written down; `chord.track_write` is what turns it into notes), key/scale auto-detection
over TIME (the key reported is one estimate for the whole note list), and roman-numeral analysis
of arbitrary chord sequences. Those absences are stated in `docs/KNOWN-LIMITATIONS.md`.
