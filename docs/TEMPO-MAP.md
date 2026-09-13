# Tempo map — tempo and time-signature automation on the timeline (D11)

Engine: `include/TempoMap.h`, `src/core/TempoMap.cpp`, `Song::tempoMap()` /
`Song::tempoAtTick()` / `Song::secondsAtTick()` / `Song::followTempoMap()`,
`Engine::updateFramesPerTickForTempo()`.
Surface: `transport.tempo_map_get` / `tempo_map_add` / `tempo_map_remove` /
`tempo_map_clear` / `tempo_map_set_active`
(`src/core/ControlCommandsTransportMap.cpp`).
Proof: `tests/src/core/TempoMapTest.cpp`,
`tests/src/core/ControlTempoMapCommandsTest.cpp`.

Written for the next reader, because the two decisions below are the ones that
cannot be recovered from the code alone.

## 1. What was measured before it was built

At the integration tip `297cdfabd`:

- the transport tempo is **one scalar**: `Song::m_tempoModel` (an `IntModel`),
  read through `Song::getTempo()` and pushed into a single global
  `Engine::s_framesPerTick` by `Engine::updateFramesPerTick()`. Every
  tick↔frame conversion in the engine reads that scalar
  (`TimePos::frames()`, `Timeline::setTicks()`, `Song::currentFrame()`).
- there is **no tempo map**: no `tempoMap` symbol anywhere in first-party code.
- a time signature exists (`Song::m_timeSigModel`, `MeterModel`) and changes the
  *bar/beat* arithmetic (`TimePos::setTicksPerBar`). It has **no reader in the
  conversion**: `Engine::updateFramesPerTick()` divides by
  `DefaultTicksPerBar`, not by `TimePos::ticksPerBar()`, so changing the metre
  has never changed the frame rate. The map keeps that behaviour (a
  time-signature event changes the bar arithmetic and audio/export bar lengths,
  not the tick→frame rate).

So the engine half did not exist and had to be built, not wired.

## 2. Decision 1 — the shape of the map

**One ordered list of events; each event carries an optional tempo and an
optional time signature; add-or-replace merges per property.**

```cpp
struct TempoMapEvent {
    tick_t tick;
    bool hasTempo;   int tempo;                 // MinTempo..MaxTempo
    bool hasTimeSignature; int numerator; int denominator;
};
```

- The list is kept **strictly increasing in tick**; `addEvent()` at an existing
  tick *merges* (the half the call names is replaced, the other half is kept),
  so `add` with a tempo at a tick that already carries a metre does not silently
  drop the metre.
- `tempoAtTick()` and `timeSignatureAtTick()` are **independent step functions**
  over the same list; each skips events that do not carry the property it is
  looking for. A tempo curve and a metre change therefore share one grid without
  fighting, which is the reason for the optional halves rather than two lists.
- Capacity is fixed: `TempoMap::MaxEvents = 128`. That is what makes every
  authoring call and every query **allocation-free**, which the audio-thread
  hand-off needs (section 4). A 129th event is refused typed, never silently
  dropped.
- Rejected on the way in (one rule, `TempoMap::validEvent`): neither half
  present, a negative tick, a tempo outside `MinTempo..MaxTempo` (10..999), a
  numerator outside 1..32, or a denominator that is not a power of two up to 32.

## 3. Decision 2 — behaviour outside the map's range

**Before the first event: the GLOBAL tempo and time signature. On and after an
event: the event's value, held until the next event that carries the same
property, and past the LAST event held indefinitely.**

Why this way round:

1. **Adding an event cannot retime what came before it.** With the global value
   in force before the first event, an event at bar 16 changes bar 16 onwards and
   nothing else. The alternative — returning the *first* event's value for all
   earlier ticks — would silently retime material the user never mapped.
2. **Every tick of an unbounded timeline has an answer.** Holding the last event
   is what makes a render that runs past the last event (the export tail, a
   loop) well defined; snapping back to the global tempo there would put a step
   at the end of the map that nobody authored.
3. **A map that names tick 0 IS the total override.** Adding a tempo at tick 0 is
   the map's own `transport.set_tempo`; this is the documented way to change the
   tempo of the whole timeline through the map.
4. **The empty map is today's engine, by construction, not by a flag.** An empty
   OR inactive map is never consulted: `tempoAtTick()` returns the global tempo
   for every tick, `secondsAtTick()` evaluates `TimePos::ticksToMilliseconds()`
   verbatim, `framesPerTickAtTick()` evaluates
   `Engine::updateFramesPerTick()`'s expression verbatim, and
   `Song::followTempoMap()` returns before it copies the snapshot. The
   tick↔frame path of a project with no map is the path it has always been.

Which means: **the map writes nothing into a project that does not use it.**
`TempoMap::shouldPersist()` is `active || size() > 0`, and it gates the only
write site in `Song::saveProjectFile()`. This release's reproducibility claim —
that a bundled project renders byte-identically across the change — rests on
exactly that, and `TempoMapTest` measures both halves of it: the file bytes and
the audio path.

## 4. The audio-thread read (workspace rule 4)

The map is authored on the control thread and read on the audio thread, so a
reader must never observe a half-written set and may neither allocate nor lock.

- `TempoMap` is a fixed-capacity value type: the reader's copy is a plain value
  copy (about 3 KiB on the stack), no allocation, no growth.
- The hand-off is `TempoMapPublisher`, a **seqlock**: every mutation is bracketed
  by `edit()`, which bumps an atomic version to ODD, applies the change and bumps
  it back to EVEN. `snapshot()` reads the version, copies the value and re-reads
  the version, retrying only while a write is in progress. There is exactly one
  writer (the control thread) and it publishes only when an edit lands, so a
  retry is not expected in practice.
- Honest limit of the technique: the version bumps are the only atomics, so the
  value copy is a benign data race by the letter of the C++ memory model — the
  trade every seqlock makes. It is sound here for the one-writer reason above.
- `Song::followTempoMap()` runs **once per block** and returns immediately unless
  the map is `active()`. When it is active it reads the tempo at the play head
  from the snapshot and, only when that differs from the tempo it last applied,
  calls `Engine::updateFramesPerTickForTempo()` — which writes
  `Engine::s_framesPerTick`, now a `std::atomic<float>` precisely because this is
  the one writer that is not the control thread.
- **Changes take effect at the start of the audio block that contains them.**
  This is not sample-accurate tempo automation; that is a separate, still-open
  in-list item. The tempo is exact at tick granularity and block-quantised in
  time; a block is about 11 ms at the default period.

## 5. The surface

| command | class | notes |
|---|---|---|
| `transport.tempo_map_get` | not_mutating | every event, the active flag, and the map's answers *at the play head*: tempo, time signature, elapsed seconds |
| `transport.tempo_map_add` | true_inverse | add-or-replace at a tick; the first event brings the map into force unless `active:false` |
| `transport.tempo_map_remove` | true_inverse | exact tick, else typed `not_found` |
| `transport.tempo_map_clear` | true_inverse | every event + switch off, one step; an already-empty map is `invalid_args` |
| `transport.tempo_map_set_active` | true_inverse | switches authority without editing events; a no-op change is `invalid_args` |

The four mutating commands record an **action checkpoint**
(`control::addUndoStep`), not a Song journal checkpoint: a Song checkpoint
captures `TrackContainer::saveSettings` — the track container — and the map is
not in it. `ControlTempoMapCommandsTest` proves the inverse by reading the map
back after `control.undo`.

## 6. Not done here

- **No editor.** There is no tempo-map UI; the map is drivable through the
  control socket (`docs/KNOWN-LIMITATIONS.md`, release notes).
- **No sub-block timing.** Block-quantised, per section 4.
- **The time-signature events do not move the tick→frame rate**, matching the
  pre-existing engine (`DefaultTicksPerBar`, section 1). They change the metre
  the bars, the ruler and the export bar length are computed with.
- **No tempo *curves*.** Events are steps. Ramping tempo (accelerando) would need
  a second event type and an integration over the ramp, and is not in scope.
