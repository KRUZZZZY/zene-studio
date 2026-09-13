# The groove pool and quantise (0.3.0 engine item, `groove.*`)

This is the implementing lane's report: what a groove is in this engine, how one is captured and
applied, what a quantise with a strength and a humanise amount does, where the pool lives and why
that decides its reversibility, and what is deliberately not here. The release notes carry the
one-line summary; this page carries the numbers a reviewer can check against the tree.

The release contract (charter §3.1) makes a feature IN only when **all four** hold: the engine work
is in the tree, it has a control-surface command group, it has a proof, and its UI absence is
written down. This page is the record of the first, and the pointer to the other three.

## 1. The model

A **groove** is the timing and velocity *feel* of a note pattern, captured from one clip and
re-applied to another. In this engine it is a `GrooveTemplate` (`include/GrooveTemplate.h`):

| field | meaning |
| --- | --- |
| `name` | the pool key: 1..64 characters, trimmed. A groove is found, replaced and addressed **by name**, so extracting under an existing name replaces that groove. |
| `lengthTicks` | the cycle length: a positive whole number of slots, at most `MaxSteps` (64) of them. |
| `stepTicks` | the slot width, in ticks: 1..`DefaultTicksPerBar` (192). |
| `steps[]` | one `GrooveStep` per slot: a signed `timing` offset in ticks and a signed `velocity` offset on the engine's own note volume (0..200, `volume.h`). |

The template **cycles**: a note anywhere on the timeline is governed by the step of the slot it
falls in, modulo the cycle. A four-slot template over 12-tick slots is therefore a one-beat
sixteenth-note feel that repeats across a whole clip, which is what a groove pool is for.

**The timing offset is bounded by half the slot width.** That single rule buys three properties at
once: a groove is a *feel* rather than a rearrangement (a note can never be pushed into a
different slot), applying a groove twice is a fixed point (a note already at its target reads back
into the same slot and its target is itself), and the extraction is idempotent on an already
grooved clip. `GrooveTemplate::setStep` refuses a larger offset, and the control surface reports
that refusal with the permitted interval.

`steps` is a fixed-capacity `std::array` (`MaxSteps`), so no step operation allocates and a
template can never be an unbounded value. All of this is a plain value type with no Engine, no
track and no GUI in sight.

## 2. Extraction: how the feel is read out

`extractGroove(notes, name, lengthTicks, stepTicks, out, notesRead)`:

1. every note is assigned to the grid slot it is **nearest** to;
2. the slot's `timing` offset is the **mean signed deviation** of its notes from that slot's grid
   position, rounded to whole ticks, clamped to half a slot;
3. the slot's `velocity` offset is the slot's mean velocity **minus the clip's own mean velocity**,
   rounded to whole velocity units;
4. a slot nothing landed in is **neutral** (0, 0) — the honest reading: the groove says nothing
   about a position the clip did not play.

Step 3 is the load-bearing decision. **Relative** velocity is what makes a template portable:
"this slot is 20 louder than the rest" carried onto a quiet clip keeps it quiet, where an absolute
velocity would overwrite the second clip's own dynamics.

The measured fixture (the same numbers the tests assert): four notes at ticks **9 / 26 / 34 / 51**
with velocities **120 / 80 / 100 / 100** over a 12-tick grid and a 48-tick cycle read back as
`(+3, 0) (-3, +20) (+2, -20) (-2, 0)` — the clip's mean velocity is 100, so the velocity offsets are
exactly `120-100`, `80-100`, `100-100`, `100-100` once the notes are grouped by slot.

An empty clip is refused (there is no feel in one, and writing a neutral template for it would be an
edit that records nothing), and so is a triple the engine cannot hold (`GrooveTemplate::isWritable`).

## 3. Application and quantise: how the feel is written back

`applyGroove(notes, groove, strength)` — **quantise to a groove**:

```
slot      = the grid slot the note is NEAREST to
target    = slotStart + step(slot).timing
newPos    = pos + round(strength * (target - pos))
newVel    = clamp(vel + round(strength * step(slot).velocity), 0, 200)
```

At `strength` 1 a note lands **exactly** on the groove's target, so a second application is a no-op
(§1's half-slot bound). At 0 nothing moves. In between it is a partial feel, which is what a
strength control is for. `strength` outside `0..1` is **refused rather than clamped**: a caller that
asked for 5 asked for something this engine cannot mean.

`NoteTransform::quantizeNotes(notes, options)` — **quantise to a grid**:

```
target    = the grid step the mode selects (nearest | floor | ceil)
newPos    = pos + round(strength * (target - pos)) + jitter(timing)
newVel    = clamp(vel + jitter(velocity), 0, 200)
```

The two knobs are two different facts:

* **strength** is how far a note travels — `5 → 0` at half strength is `5 + round(-2.5) = 2`, and at
  full strength `0`;
* **humanise** is a jitter added *afterwards*, bounded by what was asked for and never so large that
  a note could change slot (`humanise_ticks` is capped at `(grid-1)/2`).

The jitter is a **pure function** of `seed` and the note's own identity — its pitch, its length, and
the grid slot it is being taken to — through `NoteRandom::rollUnit`, the same seeded mechanism MIDI
depth uses (`docs/MIDI-DEPTH.md`). It is deliberately **not** drawn from the note's current position:
a position-derived jitter would re-roll on every repeat (the position has changed), so the same call
on the same clip would wander inside the bound instead of reproducing the take it just produced.
Drawn from the slot, the same call is a **fixed point**: same seed, same amounts, same result, and a
different seed is a different take. There is no hidden random state anywhere on this path, and
nothing here is called from a render path.

Note that a *neutral* groove applied at strength 1 is exactly a grid quantise — applying a groove
lands every note on its slot, so "quantise to the grid" and "quantise to this groove" are one
operation. That is asserted, not assumed (`GrooveTemplateTest::applicationIsExactAndIdempotent`).

## 4. Where the pool lives, and why that decides its reversibility

The pool is **project state**: one `<groove-pool>` element inside `<song>`, written by
`Song::saveProjectFile` **only when the pool is non-empty** (`GroovePool::shouldPersist`), so a
project that never captured a groove re-saves **byte for byte** as it did before this feature
existed — the reproducibility property the release's own notes rest on. `Song::clearProject()`
clears the pool, so a new project never inherits the previous one's grooves.

The element is restored by its own branch of the load walk, and `GroovePool::loadSettings`
**clears the pool first, unconditionally**. That is the load-bearing half: because the element is
only written when the pool is non-empty, the state a restore must be able to reach is "no pool at
all", and an absent element is exactly that state. A reader that only *added* would leave a
restored project holding the previous project's grooves — the trap `docs/UNDO-BOUNDS.md` records for
the warp map and the take lanes. The test proves it with a negative control: the saved file is
re-parsed with the element removed, the project is loaded, and the pool must come back **empty**.

## 5. Reversibility (SPEC A16)

Two kinds of edit, two different inverses — and the second is the one worth reading:

* **clip edits** (`groove.apply`, `groove.quantize`) move the **notes** of one `MidiClip`. A MidiClip
  is a `JournallingObject` whose serialized state *is* its note list, so the clip's own journal
  checkpoint is the inverse: **one** `control.undo` restores every position and every velocity the
  command moved, whatever the strength was. Class `true_inverse`, mechanism named on the wire.
* **pool edits** (`groove.extract`, `groove.set`, `groove.remove`, `groove.rename`) change the pool.
  The pool is **not in the track container**, so a `Song` checkpoint — which carries the container —
  does **not** hold it. This is the same finding `docs/TEMPO-MAP.md` and `docs/MODULATION.md` record
  for the tempo map and the modulation layer (a `Song` checkpoint captures
  `TrackContainer::saveSettings`, and neither the map nor the layer is in it), and it has the same
  answer: the inverse is a
  recorded **action checkpoint** (`control::addUndoStep`) that writes the captured `<groove-pool>`
  element back. Class `true_inverse` (recorded action), and the descriptor names a **real command**
  a reader can re-issue — `groove.set` with the replaced groove's own steps, or `groove.remove` when
  the capture created the groove. "Remove the new one" would be a descriptor that loses a replaced
  groove, so it is not what is recorded.

`groove.list` writes nothing: `not_mutating`.

The rows are in `src/core/ControlReversibilityTable.cpp` (the two live-checkpoint rows),
`...TableAction.cpp` (the four recorded-action rows) and `...TablePassive.cpp` (the read-only row).
The table's histogram moves from 142 rows / 75 / 9 / 3 / 55 to **149 rows / 81 / 9 / 3 / 56** in the
telemetry-off, wasm-off configuration, and `ReversibilityContractTest` asserts the new split.

**Bounds, stated rather than implied:** the pool holds at most `MaxTemplates` (32) grooves, each at
most 64 slots of two bounded integers, so the before-state a pool edit records — the pool's own XML —
stays a few KiB at worst, far inside `control::MaxTransactionBytes` (256 KiB). The before-state of a
*clip* edit is deliberately only the clip's id and its note count: a clip may hold an unbounded
number of notes, and a record that grew with the clip would be the one thing that cap cannot bound.
The notes are restored by the checkpoint, not by the record.

## 6. The command group (`groove.*`)

| id | args | what it does | class |
| --- | --- | --- | --- |
| `groove.list` | `name?` | the pool: every groove's geometry, and with `name` one groove's steps | `not_mutating` |
| `groove.extract` | `clip`, `name`, `grid`, `length?` | capture a clip's feel into a named groove (replaces that name) | `true_inverse` (action) |
| `groove.set` | `name`, `length_ticks`, `step_ticks`, `steps?` | write a groove verbatim, so a groove can be authored and not only captured | `true_inverse` (action) |
| `groove.apply` | `clip`, `name`, `strength?` | apply a named groove to a clip's notes | `true_inverse` (clip checkpoint) |
| `groove.quantize` | `clip`, `grid`, `strength?`, `humanise_ticks?`, `humanise_velocity?`, `seed?`, `mode?` | grid quantise with strength and humanise | `true_inverse` (clip checkpoint) |
| `groove.remove` | `name` | delete a named groove | `true_inverse` (action) |
| `groove.rename` | `name`, `to` | rename a groove in place | `true_inverse` (action) |

`length` defaults to four slots (one beat of sixteenths at this engine's 192-tick bar). `name` and
`grid`/`length_ticks` are required for `groove.extract` and `groove.set` because a groove with no
name or no geometry is not a groove; every other argument has a documented default. The `name` is
the key everywhere, so `groove.rename` onto a name that is already taken is **refused, typed**, and
changes nothing — it would destroy the groove that holds it.

## 7. What is NOT here (stated limits, not bugs)

* **No interface.** There is no groove browser, no template list, no drag-to-apply and no quantise
  dialog: every one of the seven ids is reachable through `--control-socket` and the MCP bridge
  only. `docs/KNOWN-LIMITATIONS.md` carries the same sentence.
* **One step per slot.** A groove cannot say "the first of two notes here is late and the second is
  early": the template's resolution is the slot, so a slot with several notes is described by their
  mean. Finer feel needs finer slots.
* **No swing/percentage template generator.** A groove is either captured from a clip or written by
  hand through `groove.set`; there is no "make me a 60% swing" command. (The *applied* strength is
  the percentage knob, but the template itself has no generator.)
* **No audio-clip grooves.** A groove moves MIDI notes; a `SampleClip` has no note list and every
  verb of the group refuses it, typed.
* **Not a playback feature.** A groove is applied once and the notes are ordinary notes afterwards:
  un-applying it is `control.undo`, not a switch. Nothing on the audio path reads a template.
* **The pool is per project, not per track.** There is one pool on the `Song`; a groove is
  applicable to any MIDI clip in the project.

## 8. The proof

* `GrooveTemplateTest` (`tests/src/core/GrooveTemplateTest.cpp`) — the engine, with **no Engine at
  all**: the geometry bounds, the extraction rule's exact numbers, the application's exact ticks and
  velocities, idempotence, the strength interpolation, the velocity clamp, and the pool's keying,
  its XML round trip and its reset-on-absence.
* `ControlGrooveCommandsTest` (`tests/src/core/ControlGrooveCommandsTest.cpp`) — the surface: the
  seven registered ids with schemas and no `requires` excuse, the contract rows, the measured effect
  of an apply and of a quantise through `roll.get_state`, the humanise's bound / reproducibility /
  seed sensitivity, both inverses through `control.undo`, a typed refusal for every way a call can
  be wrong, and a real project save/load round trip whose negative control (element removed) must
  leave the pool empty.
* `ControlGrooveCommands` (`tests/control-groove-commands.py`, a **registered ctest**) — the release
  contract's section 3.1 claim: a real `zene` binary, started headless with `--control-socket`,
  driven by an external client. It asserts the same numbers **off the wire** (before/after tick
  positions and velocities read from `roll.get_state`), drives every verb, checks both undos, and
  finishes with `control.quit`. A command that answered `ok` without moving a note fails here.
