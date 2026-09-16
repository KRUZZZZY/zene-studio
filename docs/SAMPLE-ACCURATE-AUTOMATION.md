# Sample-accurate automation

**Feature row 9 of `docs/FEATURE-LIST-0.3.0.md`; board task #646.** Automation that reaches the audio
path at sample precision inside an audio block, real-time-safe, and drivable from the control surface.

## The behaviour this replaces

`Song::processAutomations()` has always been a per-TICK pass: at every tick boundary inside a block it
evaluates each automation clip and writes the parameter's value. The buffer the audio path actually
reads per sample, `AutomatableModel::valueBuffer()` — the one `MixerChannel::updatePostFaderBuffer()`
and the fx chains multiply their samples with — was filled ONCE PER BLOCK by interpolating from the
value the model had when the previous block ended to the value the first tick of this block applied.

Two things follow, and both are audible on a fast move:

1. **the parameter is a whole block late.** The samples at the start of a block still carry the
   previous block's value, whatever the curve did in between;
2. **the move is smeared over the whole block.** Its shape is a straight line across the block, not
   the curve's own shape.

`include/AudioEngine.h` says the second half in its own words: the engine's rendering "is chunked into
smaller periods to timely handle per-buffer updates like **non-sample-accurate automation**".

## The mechanism

* **`include/AutomationRamp.h`** — one block's worth of per-sample automation as a **fixed-capacity**
  array of `(frame, value)` knots (`MaxKnots` = 32 → 528 bytes, a member of the model, allocated once
  with it). `valueAt(frame)` is linear between two knots and holds outside them; two knots on one
  frame make a step (the later one wins). No allocation, no lock and no growth by construction, and a
  knot that does not fit is **refused and counted** (`refusals()`).
* **`AutomationClip::writeBlockRamp()`** — builds the ramp for one block, under the clip's own lock
  (once per block rather than once per knot). The knots are the curve's value at the block's first
  sample, at every tick boundary inside the block, and at the block's end.
* **`AutomatableModel::publishAutomationRamp()` / `valueBuffer()`** — the ramp is stamped with the
  period it was built for and copied into the model; `valueBuffer()` then fills the per-sample buffer
  from it (`scaledValue()` → the automation-mode trim → `fittedValue()`, the same transform the
  per-tick path applies). A model with no ramp published for the current period behaves exactly as it
  always has.
* **`Song::buildAutomationRamps()`** — called once per audio block from `Song::process()`, **before**
  the tick loop, because the whole block's curve has to be in the parameters before the first sample
  of it renders. It walks only the clips that opted in and returns at its first type test otherwise,
  which is what keeps every project that never opts in byte-identical.

### Why one knot per tick boundary is the whole curve

A clip's nodes sit on **integer ticks**, and its stored value is **linear in ticks** between two of
them (the `Linear` progression type; `Discrete` holds a node's value until the next one). So inside one
tick the curve is a straight line, and an audio block — a handful of ticks, and never aligned to the
tick grid — is reproduced exactly by the curve's value at its first sample, at every tick boundary
inside it, and at its end.

Every boundary gets **two** knots, on the frame just before it and on the frame itself, because
`Discrete` is the clip's default progression type: the frame before the boundary must hold the value
the curve had coming into it, and the boundary frame is the first sample on the new value. Placing them
with `floor`/`ceil` of the boundary's exact frame position (rather than rounding) is what keeps a step
from landing a sample early.

## The control surface

| id | args | result | A16 |
| --- | --- | --- | --- |
| `automation.ramp_set` | `track`, `parameter`, `mode` = `sample` \| `block` | `track`, `parameter`, `mode`, `mode_before`, `changed`, `automation` | `true_inverse` — a LIVE checkpoint on the automation clip (`addJournalCheckPoint()` before the flag moves) |
| `automation.ramp_get` | `track` (optional filter: the `ch-<n>`/`trk-<n>` target), `include_block_mode` | `parameters[]` (`track`, `parameter`, `clip`, `mode`, `ramp_live`, `knots`, `frames`, `moves_inside_block`, `refused_knots`, `automation`), `count`, `sample_accurate_count`, `live_ramp_count`, `unaddressable_object_count`, `ramp_capacity` | `not_mutating` |

`automation.ramp_get` reports what the **audio thread** did, not what the project asked for: a clip in
`sample` mode whose `ramp_live` is `false` after a render is a bug report, and a `refused_knots` above
zero is the capacity fallback the limitation section below names.

It enumerates the **clips**, not the tracks that hold them: every automation track the engine plays
automation from — the song's own, the pattern store's and the hidden **global** automation track — and then
each clip's own objects. A clip that holds no device parameter the surface can name (the song's tempo, a
pattern-internal control) is counted in `unaddressable_object_count` rather than dropped silently. An entry's
`track` is the target the parameter is addressed by (`ch-<n>` / `trk-<n>`, the pair
`automation.ramp_set` and `automation.add_point` take), `parameter` its `<plugin>/<index>` id, so the
reported id addresses the parameter back; the clip's own holder track is in the nested `automation.track`,
empty for the hidden global track.

The flag is serialized **only when it is on** (`<automationclip ... sample_accurate="1">`), so a project
that never asked for it saves the bytes it always saved, and `AutomationClip::loadSettings()` **resets
the flag on absence** — which is what makes the clip's own journal checkpoint a real inverse even for
the FIRST `ramp_set`.

## Where sample accuracy cannot hold

Stated here rather than left to be discovered, and carried in the A16 row's own text
(`src/core/ControlReversibilityTableAutomationRamp.cpp`):

1. **A device that never reads a per-sample value keeps the block's single value.** The ramp lives in
   the model; the consumer has to ask for it, through `AutomatableModel::valueBuffer()`. The consumers
   that do are the mixer channel fader (`MixerChannel::updatePostFaderBuffer`), the instrument tracks
   and the fx chains; a parameter whose own `process()` reads `value()` once per block gets the block's
   start value and no per-sample movement.
2. **A clip in a PATTERN has no single block timeline** — its automation is re-read against the
   pattern's own tick grid — so `automation.ramp_set` **refuses** it typed (`Refused`) rather than
   pretending.
3. **`CubicHermite` (tangent-edited) curves are approximated.** The stored shape inside one tick is a
   cubic and a ramp is piecewise linear, so a tangent-edited curve is interpolated as one straight
   segment per tick. `Linear` and `Discrete` are exact; the surface reports the progression type beside
   the mode.
4. **A block denser than the capacity falls back and counts it.** `MaxKnots` (32) holds two knots per
   tick boundary plus the block's ends — about 400 BPM with a 512-frame block at 44.1 kHz and 192 ticks
   per bar. Beyond that the surplus knots are refused (`refused_knots` in `automation.ramp_get`) and the
   ramp interpolates over the knots it kept. Nothing allocates.
5. **A transport jump inside a block is read with the block's own ramp.** A loop wrap or a seek that
   lands mid-block leaves the remaining frames of that block on the old position's curve; the next
   block is exact again.

## The proof

`tests/src/core/SampleAccurateAutomationTest.cpp`, registered as the ctest **`SampleAccurateAutomationTest`**:

* renders real periods through `Song::processNextBuffer()` — the entry
  `AudioEngine::renderStageNoteSetup()` calls on the render thread, with the dummy device stopped — and
  compares the per-sample buffer with the clip's own curve at **every frame of every block**, in `sample`
  mode and in `block` mode. The `block`-mode run is the negative control: if the measurement could not
  tell the two apart, the test fails on its own comparison rather than passing vacuously;
* checks the ramp builder against the curve for a `Discrete` and a `Linear` clip over a block whose start
  is deliberately inside a tick;
* probes the whole ramp path (build + per-sample fill, 64 blocks) with `AllocationProbe.h` and requires
  **0 allocations**;
* checks the ramp's own bounds: capacity refusals are counted, the value holds past the last knot, and an
  out-of-range knot index returns the empty knot rather than reading out of bounds;
* drives `automation.ramp_set` / `automation.ramp_get` through the registry, tests the A16 inverse for
  real (`set` → `control.undo` → read the mode back) and the typed refusals (a bad mode, a device that is
  not there, a parameter with no curve).
