# Modulation layer (#602): modulators that drive a set of parameters, and per-note expression

**Status: in, on the 0.3.0 release line.** The engine is `include/ModulationLayer.h` /
`src/core/ModulationLayer.cpp`; the surface is ten registered commands (`modulator.*` plus
`note.expression.*`); the proofs are two registered ctests. This file is the design record — the
decisions that cannot be recovered from the code alone, the comparison with the lane that solved the
same range problem for rack macros, and the honest limits.

Worktree: `projects/lmms-fl-research/zene-030-w18`, branch `030/w18-modulation`, based on the
verified integration tip `5e8335855199319b3ac29f416a359e102ad82bb8`.

---

## 1. What was measured before it was built, and what a modulator therefore IS

At the base, three objects already exist that a reader could mistake for #602, and none of them is it:

| object | what it is | why it is not a modulation layer |
|---|---|---|
| `LfoController` / `PeakController` (`include/`, `src/core/`) | per-parameter `Controller`s: a source that writes **one** connected parameter through a `ControllerConnection` | one source → one target. #602's whole shape is a source driving a **set**, each by its own depth, and the depth is what a `ControllerConnection` does not carry |
| `EnvelopeAndLfoParameters` (`include/EnvelopeAndLfoParameters.h`) | a synthesizer's own per-parameter envelope/LFO, owned by an instrument | scoped to one instrument's parameters, not addressable from outside it |
| #601 MPE per-note expression (`include/MpeExpression.h`, `Note::mpePitchCents` &c.) | a **captured controller gesture** attached to one note | belongs to one note, comes from MIDI input, has no timeline and cannot drive anything but that note. It is #602's per-note *half* |

So a modulator had to be a new object. It is a **timeline-locked LFO** — a shape, a rate in Hz, a
phase and a polarity — plus a list of **routes**, and it lives on `Song` (`Song::modulationLayer()`),
because the thing it modulates is a set of parameters spread across the project rather than anything
one channel owns.

**Why an LFO and not an envelope or a per-note expression.** The measured gap in this tree is a
*source that a control-surface caller can create, point at several parameters and leave running*.
`PeakController` already supplies the other natural source (an envelope follower) and could feed the
same route list later; a captured per-note gesture (MPE) is a different data path with a per-note
lifetime. `docs/MPE.md` records that `#601`'s per-note plumbing is the right home for the per-note
half, and §5 below explains why this lane drives it instead of building a second one.

## 2. Decision 1 — how a DEPTH is expressed, and why it matches the macro lane in the unit and differs in the write

A route carries `depth` in `-1..1`, a **fraction of the target parameter's own `min..max`**. That is
`docs/RACK-MACROS.md` §2's unit, and it is the same unit for the same reason: the engine defines a
parameter's range, so recording the engine's numbers would pin the assignment to the range one build
happened to report. `docs/RACK-MACROS.md` states it as

```
fraction = clamp(low + value * (high - low), 0, 1)
written  = min + fraction * (max - min)
```

**The difference is what the fraction is applied to, and it is the whole point of #602.** A macro
**replaces** the parameter's value (an absolute write over a window). A modulator **adds** to it:

```
written = clamp(base + depth * output * (max - min), min, max)
```

Three consequences, and each is a reason:

1. **The offset is independent of each parameter's own absolute value** — the item's own words. Two
   parameters with different ranges *and different current values* move by the same fraction of their
   own range: `depth 0.2` moves a `0..100` parameter by 20 and a `-100..100` parameter by 40, from
   wherever each one happens to sit. A macro cannot do that, because a macro *is* the value.
2. **A modulator can therefore be pointed at several parameters without knowing them.** A macro's
   window has to be chosen per target (`low`/`high`), because it is defining an absolute mapping. A
   route's depth is one number that means the same thing on every target, which is what makes a
   modulation layer drivable by an agent that has not enumerated the parameters yet.
3. **`depth = 0` is a bound-but-silent route**, and the block skips it. That is what makes "which
   parameters is this modulator *pointed* at" and "how much is it doing" independent questions, and
   it is why `modulator.depth_set` does not touch the base (below).

`high < low` inverts a macro's drive; a modulator inverts by a **negative depth**, which is the same
idea stated in the one number it has.

## 3. Decision 2 — the base, and why every edit restores before it rebuilds

An additive write needs a value to add *to*, and that value must not be the model's current one:
`base + offset` written into the model every block would compound. The layer therefore captures a
per-route **base** — the parameter's own value at the moment the route was resolved — and every
block writes `base + offset`, never `current + offset`.

The trap that follows is the one `docs/RACK-MACROS.md` §4 names for the automation system and this
design has to answer: **what is the base after someone edits the parameter or the layer?** The rule
this release adopts is stated once, in
`rebuildModulationRuntimeRestoring()` (`include/ModulationLayer.h`):

> Every layer edit **hands every target back to its recorded base first**, then re-resolves and
> re-captures.

That one rule buys three properties, and they are why the implemention is not three special cases:

- **Re-capturing is idempotent.** The models are at their unmodulated values when the new base is
  read, so editing a modulator cannot bake a modulated value into it — `modulator.target_set`,
  `rate_set` and `target_remove` all go through it.
- **A stopped or deactivated modulator hands the parameter back.** `Song::stop()` calls
  `restoreModulationBases()` on the same runtime, and `modulator.rate_set` with `active:false`
  rebuilds with the restore; neither leaves a filter parked where the last block put it.
- **A route's base moves when the user's value moves**, but only at an edit: rebinding the same
  address re-captures from whatever the model holds *then*, and `bindDriveUndoAndRebind` in
  `tests/src/core/ControlModulatorCommandsTest.cpp` proves the re-bind reads the handed-back value
  rather than a stale one.

**Stated limit, because it is a real one:** while a modulator is active the parameter's own control
is *taken over* for as long as the layer keeps writing it. A fader shows the base, the audio hears
`base + offset`, and a user's move of that control is overwritten on the next block until the
modulator is deactivated, removed, or `control.undo` takes the edit back. This is the same takeover
the automation system performs on an automated control, and it is the price of a relative write
without a per-block read-modify-write.

## 4. The audio path, and the lifetime rule

`Song::processModulation()` runs once per audio block, immediately after `followTempoMap()`
(`src/core/Song.cpp`, `processNextBuffer`). It returns before copying anything unless the layer holds
a modulator, and the publisher it then reads is a **seqlock** with `TempoMapPublisher`'s exact shape
and its exact honest limit: the version bumps are the only atomics, so the value copy is a benign
data race by the letter of the C++ memory model, sound because there is exactly ONE writer (the
control thread) and it publishes only when an edit lands.

- The snapshot is a fixed-capacity value (`ModulationRuntime`: 16 sources, 128 entries). No
  allocation, no lock, no growth. `ModulationLayerTest::theAudioPathAllocatesNothing` measures
  **0 allocations over 64 blocks** with two routes configured.
- The modulator runs on **wall-clock seconds measured at the play head**
  (`ticks * framesPerTick / baseSampleRate`), so a rate in Hz is Hz and the LFO follows the timeline
  rather than the transport's musical position.
- **The write targets are `QPointer<AutomatableModel>`, deliberately.** A target can be destroyed
  under the audio thread — a device unloaded, a chain or channel removed, a project opened — and a
  `QPointer` nulls itself in `~QObject`, so the audio thread *skips* a destroyed target instead of
  dereferencing it. That is the same guarded-key technique the Darwin fix applied to
  `Song::m_oldAutomatedValues`, and the audio-thread half is a null check.
- **The lifetime rule, stated:** the resolved write set is rebuilt on every layer edit, and it is
  **cleared** by `Song::clearProject()` (so project new/open drops every route with the layer) and
  re-resolved by `Song::loadProject()`'s `<modulation-layer>` branch — a route never carries a
  pointer from a project that is no longer open. A target destroyed by a path this lane did not hook
  (a `plugin.unload`, say) leaves a null in the runtime until the next edit, which the audio path
  skips harmlessly: the reachable failure is a modulator that temporarily does nothing, never a
  dereference of freed memory.

## 5. Decision 3 — the per-note half reuses #601 rather than inventing a store

`note.expression_set` / `note.expression_get` / `note.expression_clear` read and write the fields
task `#601` already put on a `Note` — the three optional attributes `mpepitch` / `mpepressure` /
`mpetimbre` and their accessors — via `Note::setMpeExpression()` and `Note::clearMpeExpression()`.
They are **not** a second expression store, and nothing about the serialized format changes: a note
that carries no expression still serializes with exactly the upstream attribute set.

The gap this closes is real and was named by `docs/MPE.md` §6: `#601`'s per-note expression shipped
with **no control-surface command at all**, so a captured gesture was readable by nothing outside the
process. The inverse is a `MidiClip` checkpoint — the mechanism `note.velocity_set` reverses with,
because a `Note` is a `SerializingObject` and the `MidiClip` that owns the note list is the
`JournallingObject`.

**`#601`'s own limit carries over unchanged, and is not restated as a new one:** only the **pitch**
axis is applied by playback; pressure and timbre are captured, stored and editable, and reach no
instrument. `note.expression_set` accepts all three and says so in its description.

## 6. Persistence

`<modulation-layer version="1" modulators="N">` as a child of `<song>`, with one `<modulator name=
shape= rate= phase= unipolar= active=>` per modulator and one `<route channel= chain= effect=
parameter= depth=/>` per route.

- **Written only when the layer is not empty** (`ModulationLayer::shouldPersist()`), so a project that
  never used a modulator re-saves byte-identically — the same rule, and the same reason, as
  `TempoMap::shouldPersist()` in `docs/TEMPO-MAP.md` §3.
- **A corrupt or empty block loads as an EMPTY layer**, never a repaired one: a modulator whose rate
  or phase is outside the engine's bounds is refused rather than clamped, and a route with no
  parameter name is dropped. `ModulationLayerTest::aCorruptBlockLoadsAsAnEmptyLayer` measures all
  three shapes. Nothing is guessed, so a truncated block degrades to "no modulation" rather than to
  something nobody authored.
- `ModulationLayerTest::theLayerPersistsAndReloads` requires saved = loaded = re-saved.
- An **unknown shape name** is not persisted as such: the reader falls back to `sine` (its declared
  default) and keeps the modulator, which is why the round-trip test asserts that the fallback is
  what lands rather than pretending the name survived.

## 7. The surface

| id | class | notes |
|---|---|---|
| `modulator.get_state` | `not_mutating` | the layer, every route with its address, its depth and whether it still **resolves**, plus `driving` — the number of routes the audio thread will actually write, so a dead route is visible rather than hidden |
| `modulator.create` | `true_inverse` (action checkpoint) | a name and an LFO; drives nothing until a route is bound |
| `modulator.remove` | `true_inverse` (action checkpoint) | hands the targets back, then drops the modulator; the undo re-inserts it with its routes at its own index |
| `modulator.rate_set` | `true_inverse` (action checkpoint) | the source (shape/rate/phase/unipolar/active) as one object; `active:false` hands the targets back |
| `modulator.target_set` | `true_inverse` (action checkpoint) | binds one parameter; refused at bind time if it does not resolve or is already driven |
| `modulator.depth_set` | `true_inverse` (action checkpoint) | the amount only; the base is deliberately not re-read |
| `modulator.target_remove` | `true_inverse` (action checkpoint) | unbinds and hands the parameter back |
| `note.expression_set` | `true_inverse` (MidiClip checkpoint) | pitch/pressure/timbre; an axis the call omits keeps its value |
| `note.expression_get` | `not_mutating` | one note, or every note of a clip that carries expression |
| `note.expression_clear` | `true_inverse` (MidiClip checkpoint) | drops the expression and its attributes; a no-op on a note without one is a typed refusal |

The layer is not a `JournallingObject`, so **every layer edit is an action checkpoint**
(`control::addUndoStep`), not a `Song` journal checkpoint — the same finding `docs/TEMPO-MAP.md` §5
records for the tempo map, and for the same reason: a `Song` checkpoint captures the track container,
and the layer is not in it.

`modulator.rate_set` carries the shape, phase and polarity as well as the rate. The name is
`AGENT-TOOLING.md` §5's; the extra arguments ride with it because an LFO's shape, rate, phase and
polarity are **one source object**, and splitting them across four ids would be four commands that
can each leave the source in a state no caller asked for.

## 8. Where the code is, and how it is split

| file | what |
|---|---|
| `include/ModulationLayer.h`, `src/core/ModulationLayer.cpp` | the layer, the LFO arithmetic, the runtime and its publisher, the persistence, the per-block application |
| `include/ControlModulationSupport.h`, `src/core/ControlModulationSupport.cpp` | the target resolver, the ids and the JSON shapes the group shares |
| `src/core/ControlCommandsModulator.cpp` | `get_state` / `create` / `remove` / `rate_set` |
| `src/core/ControlCommandsModulatorRoutes.cpp` | `target_set` / `depth_set` / `target_remove` |
| `src/core/ControlCommandsNoteExpression.cpp` | the three `note.expression.*` ids |
| `tests/src/core/ModulationLayerTest.cpp` | the engine (LFO, bounds, resolver, relative write, no-op paths, allocations, persistence) |
| `tests/src/core/ControlModulatorCommandsTest.cpp` | the surface (ids, schemas, A16 classes, refusals, inverses, the per-note group) |

The group is split into translation units for the same reason the automation, warp, rack and comp
groups are: this fork's file-length ratchet measures a file as a unit, and a group's boilerplate
alone does not fit twice under the limit.

`rebuildModulationRuntimeRestoring()` is called from inside `ModulationLayerPublisher::edit()`, so a
forgotten re-publish is impossible: the publisher's own contract is that every mutation routes
through `edit()`.

## 9. Not done here, and known limits

1. **No UI, and no Lua binding.** Nothing in `src/gui/` creates, draws or edits a modulator, and
   `ScriptBindings` is not extended: the reachable path is the control surface, so a user without a
   socket client still cannot make a modulator. `docs/KNOWN-LIMITATIONS.md` and the release notes
   carry the same sentence.
2. **Block-quantised, not sample-accurate.** Modulation is applied once per audio block (about 11 ms
   at the default period), so a fast LFO is quantised to the block rate.
3. **The source is an LFO only.** There is no envelope follower, no random/chaos source (which would
   also need per-modulator state and would make a render non-deterministic), and no audio-rate source.
4. **Targets are device parameters inside a mixer channel's rack chains.** The address is the rack
   macro target's, so a route cannot name the Song's own master gain, a mixer channel's volume, an
   instrument's own parameters, or an automation-visible model that is not an effect parameter.
   Widening the address is the same work for macros and modulators, which is why they share a
   resolver rather than each growing their own.
5. **One parameter, one depth.** A second route to the same address is a typed refusal; two
   modulators cannot share a target in this release.
6. **No curves, no easing, no per-route offset.** A route is a depth and nothing else; a phase offset
   per route is not in it (an LFO's phase is per modulator).
7. **A modulator is not automated and is not a target of another modulator.** There is no
   macro-to-modulator or modulator-to-depth path.
8. **`Song::stop()` restores the bases but does not clear the routes**, deliberately: the next play
   starts from the same bases. Switching projects does clear them (`clearProject`).
9. **`ControlRegistryTest::telemetryCommandsAreAbsentWhenTheClientIsCompiledOut` was NOT re-measured
   by this lane.** That assertion lives in the `#else` of `ZENE_TELEMETRY_ENABLED` (the telemetry
   client compiled out, which is not this build's configuration) and counts the registry's commands
   with a literal that several later lanes have already moved past. This lane adds ten commands to
   both configurations, so the literal needs `+10` — but the precise telemetry-OFF registry count
   needs a `-DZENE_TELEMETRY=OFF` build to measure, and inventing the number is exactly what the
   rack lane's report refused to do for the same slot. It is recorded here instead.

## 10. How to reproduce

```sh
cd ~/Documents/AI_KOS_PROJECT/projects/lmms-fl-research/zene-030-w18
bash tools/local-ci.sh --configure-only --build-dir build         # the CI linux job's own flags
cmake --build build --target zene ModulationLayerTest ControlModulatorCommandsTest -j2
cd build/tests && ctest -R 'ModulationLayerTest|ControlModulatorCommandsTest' --output-on-failure
cd build/tests && ctest -R 'ReversibilityContractTest|ControlRegistryTest' --output-on-failure
cd build/tests && ctest -R 'ReversibilityUndoTest|RackMacrosTest|ControlTempoMapCommandsTest' --output-on-failure
cd build/tests && ctest -R agent_surface -V                        # the ten new ids, swept headless
cd build/tests && ctest -R ControlCommandsSnapshot --output-on-failure   # the offline snapshot ratchet
bash tests/fork-sources-gate.sh ; echo EXIT=$?                     # gate 9
bash tests/no-upstream-regression-gate.sh ; echo EXIT=$?           # gate 6
bash tests/file-length-gate.sh --check ; echo EXIT=$?              # gate 7 (baseline not written)
bash tests/complexity-gate.sh --check ; echo EXIT=$?               # gate 4 (lizard; measures .py too)
bash tests/duplication-gate.sh --check ; echo EXIT=$?              # gate 8
```
