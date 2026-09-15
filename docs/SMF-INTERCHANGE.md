# Standard MIDI File tempo-map interchange (`interchange.*`)

Feature row 33 of `docs/FEATURE-LIST-0.3.0.md` (section 6, tempo/meter/groove): **tempo-map export /
SMF cross-DAW interchange**. The engine half is `include/SmfInterchange.h` +
`src/core/SmfInterchange.cpp`, the surface is `src/core/ControlCommandsInterchange.cpp`, the A16 rows are
`src/core/ControlReversibilityTableInterchange.cpp`, and the proofs are the ctests `SmfInterchangeTest`
and `SmfInterchangeRoundTripTest`.

The feature is one sentence: **another DAW can read this session's tempo and metre steps, at the same
ticks, from a Standard MIDI File — and this session can read them back.**

## 1. The convention, recorded (the part a re-implementation needs)

| what | value | why |
| --- | --- | --- |
| division (written) | **480 ticks per quarter note** | the PPQ every DAW reads; declared in `MThd` with the high bit clear (a positive division is ticks-per-quarter; the high bit set would be SMPTE) |
| LMMS' own grid | **48 ticks per quarter note** | `DefaultTicksPerBar` (192) / `DefaultBeatsPerBar` (4) — one beat, and the beat `Engine::updateFramesPerTick()` divides by (`sampleRate * 60 * 4 / DefaultTicksPerBar / bpm`) |
| scaling | **10 file ticks per LMMS tick**, an exact integer | 480 / 48. A LMMS tick maps into the file with **no rounding** on the write side; on the read side a foreign division (96, 960, 1000 ppq) is scaled with round-to-nearest and every event that had to be rounded is REPORTED (`rounded_events`) rather than hidden |
| tempo event | meta `FF 51 03 tttttt`, `tttttt` = **microseconds per QUARTER note** = `(60 000 000 + bpm/2) / bpm` | this engine's bpm is defined on the quarter note (`Engine::updateFramesPerTick`), and the SMF tempo is per quarter note **regardless of the metre in force** — which is the same meaning, because a time-signature event in LMMS changes bar/beat arithmetic and not the tick-to-frame rate |
| tempo round trip | **exact for every integer bpm the engine accepts (10..999)** | the quotient spacing of `60e6/bpm` over that range is wider than one microsecond, so the nearest-microsecond encoding cannot collide and `llround(60e6 / µs)` is the identity. Measured for all 990 values in `SmfInterchangeTest::theTempoEncodingIsExactForEveryAcceptedBpm` |
| time-signature event | meta `FF 58 04 nn dd cc bb`: `nn` = numerator, `dd` = log2(denominator), `cc` = 24 (MIDI clocks per metronome click), `bb` = 8 (32nd notes per quarter note) | the format's own definition; the reader turns `dd` back into `2^dd`, so a metre the map can hold (a power of two up to 32) round-trips exactly |
| file shape (written) | **format 1, ONE track: the conductor track** | format 1 is what a DAW reads as a tempo map |
| file shape (read) | format 0 and format 1, **every** track's tempo and metre meta events, merged by tick, **first in file order wins per half** | a foreign file may put its conductor events anywhere, and a tempo in one track plus a metre in another at the same tick must both survive |
| tick-0 rule | the writer **seeds tick 0** with `map.tempoAtTick(0, global)` and `map.timeSignatureAtTick(0, global)` unless the map already carries that half at tick 0 | an SMF has no "global tempo before the first event" — before one, a player assumes 120 bpm. Without the seed, a map whose first event is at bar 5 would export a file whose bars 1-4 play at 120 bpm: a different session. `seed_events` in the export reply counts the halves written this way (0, 1 or 2) |
| events at one tick | the metre is written **before** the tempo | the order every DAW writes a conductor track in |

`interchange.smf_convention` returns these values as data (the PPQ, the LMMS ticks per quarter, the ratio,
the tempo unit, the time-signature byte layout, the file shape, the tick-0 rule and the stated limits), so a
client reads the convention off the wire instead of out of this file.

## 2. The four ids

| id | what it does | SPEC A16 |
| --- | --- | --- |
| `interchange.smf_convention` | the convention above, as JSON | `not_mutating` |
| `interchange.smf_export` | write the tempo map as a format-1 conductor track. Args: `path` (absolute, required), `overwrite` (default false — an existing file is refused, not clobbered). Result: path, bytes, sha256, format, track_count, ticks_per_quarter, lmms_ticks_per_quarter, smf_ticks_per_lmms_tick, event_count, tempo_events, meter_events, seed_events, first_tick, last_tick, active, global_tempo, global_timesig | `not_mutating` — it writes a file **outside** the session and changes nothing inside it |
| `interchange.smf_read` | read a file's conductor events back, **without touching the session**. Result: the file's own division, the events in LMMS ticks, and the counts `rounded_events`, `superseded_events`, `capacity_events`, `importable` | `not_mutating` |
| `interchange.smf_import` | replace the tempo map with the file's conductor events. The imported map is **active** (a conductor track *is* a tempo map, so importing one switched off would import tempo changes the timeline does not obey). One command, one undo | `true_inverse` — a recorded ACTION checkpoint: the map captured before the import is written back through `TempoMapPublisher::edit` when the stack unwinds |

The load-bearing id for the acceptance contract is `interchange.smf_read`: it is what makes a round trip
checkable **against the file** rather than against the file's hash. The hash is reported by the export
because a caller wants to identify its artefact, and it is deliberately not what any proof compares.

## 3. The proofs

`SmfInterchangeTest` (surface + the file's own bytes):

- the four ids are registered in the `interchange` group with both schemas, a description, no `requires`
  excuse and an A16 row of the expected class, and the convention is on the wire;
- **the file is checked byte by byte by the test's OWN parser** (`tests/src/core/SmfInterchangeTestSupport.h`,
  which shares no code with the module under test): `MThd`, header length 6, format 1, one `MTrk` whose
  declared length reaches the end of the file, division 480, the track's meta events, `FF 51 03` with
  428571 µs at tick 0 (140 bpm) and 666667 µs at tick 3840 (90 bpm), `FF 58 04` with `nn=3 dd=2 cc=24 bb=8`
  at tick 3840, and a final `FF 2F 00`;
- the tempo encoding is the identity for all 990 accepted bpm values;
- every refusal is typed and writes nothing: a missing file (`not_found`), a required argument that is
  absent (`invalid_args`), a file that is not a Standard MIDI File (`invalid_args`, naming `MThd`), an SMPTE
  division (`invalid_args`), a relative export path (`invalid_args`), an existing file without `overwrite`
  (`refused`), and an import that the session's map is left untouched by.

`SmfInterchangeRoundTripTest` (the required round trip, plus the two edge cases):

- **the map, not the hash**: a map with tempo AND metre changes (ticks 0, 384, 768, 1344; 140/90/180 bpm and
  4/4, 3/4, 7/8) is exported; `interchange.smf_read` returns the file's events and they are compared with
  the authored ones; the session's map is then **cleared**, the file **imported**, and the comparison is
  repeated against (a) the events on the wire, (b) the engine's own `TempoMap` object (`operator==`), and
  (c) `Song::tempoAtTick` and the map's metre at every event tick and its neighbours, against an oracle built
  from the authored list;
- **the tick-0 seed**: a map whose only event is at 384 exports `seed_events == 2`, the file's first event is
  at tick 0 carrying the session's global tempo and metre, and importing it leaves every sampled tempo and
  metre **unchanged**;
- **one undo**: `control.undo` after an import restores the map that was there before it;
- **a foreign division**: a 96-ppq file lands exactly on LMMS' grid, a 1000-ppq file has its event rounded
  and `rounded_events` says so, and a file needing more ticks than the map holds reports `capacity_events`
  and `importable: false` and is refused by the import rather than truncated into the map.

## 4. Stated limits

- **Events are steps, and a tempo curve is not representable.** The map holds steps and the file's tempo
  meta event is a step too, so nothing about a ramp is lost on export: there is no ramp to lose. A future
  curve feature would need a representation this format does not have.
- **Only the conductor track is written.** Notes, clips, automation, markers and track names are not in the
  file. The tempo map is the part of a session with no other portable form; the note export is the
  pre-existing `File > Export MIDI` path (`plugins/MidiExport`) and is untouched by this feature.
- **Only the tempo and metre meta events are read.** The rest of a foreign file is ignored rather than
  refused (the reader walks every chunk, including running-status channel events and sysex, so it does not
  fall over on them).
- **A full map can export one event more than it can hold.** `TempoMap::MaxEvents` is 128 and the tick-0 seed
  is not part of the map: a 128-event map whose first event is later than tick 0 writes 129 conductor ticks,
  and re-importing that file is refused (`capacity_events: 1`, `importable: false`) rather than truncated.
  The export itself succeeds and the file is correct; it is the round trip of that one shape that refuses.
- **No tempo map for a foreign session.** Import replaces the map; it does not merge, and there is no
  "import into a range".
- **UI absence — one line: Standard MIDI File tempo-map interchange is drivable through the socket, not from
  the interface.** `File > Export MIDI` is the pre-existing note export and is neither changed by nor wired
  to these ids; there is no import entry for a tempo map anywhere in `src/gui/`.
