# Cluster new-p9-501 — Vibed allocates while starting a note

**Verdict: NEEDS-CONTEXT.** The defect is real: `Vibed::playNoteImpl` allocates on the
realtime render path at every note start. A behaviour-preserving, realtime-safe fix is
blocked by four owner decisions (D1–D4 below). The code cannot answer them: the buffers
scale with `oversample × sampleRate / pitch`, and the reachable pitch has no code-enforced
floor. No product code was changed in this round.

## Revision after QA round 1 (Luna DISPUTE)

- The disposition now states the defect is real, with file:line evidence below.
- Reachable pitch, string-length and simultaneous-note bounds are determined from the code
  (section "Bounds"). Simultaneous notes have no code-enforced cap.
- The blocking owner decisions are named with the numbers that bear on them (section
  "Owner decisions").
- No registered allocation regression was added. Section "Regression status" gives the
  reason and the exact test to add once D1–D3 are decided.

## Finding

Luna's patrol reported that `Vibed::playNoteImpl` allocates heap memory on the audio path
when a note starts (`plugins/Vibed/Vibed.cpp:204`). Each enabled string then allocates its
delay lines and impulse buffer.

## The path is realtime

- `src/core/AudioEngine.cpp:918` opens `const RealtimeScope realtime;`. Everything under it
  is realtime code.
- `src/core/NotePlayHandle.cpp:307` calls `m_instrumentTrack->playNote( this, buffer );`.
- `src/tracks/InstrumentTrack.cpp:700` calls `m_instrument->playNote( n, workingBuffer );`.
- `include/Instrument.h:106-110` forwards `playNote` to `playNoteImpl`.
- Worker-thread entry (`src/core/AudioEngineWorkerThread.cpp:156`) was cited by Luna and is
  consistent with this; it was not re-traced here.

## Allocation sites (one note start, one active string)

| Allocation | Location |
| --- | --- |
| `new StringContainer{...}` on first call (guard, then new) | `plugins/Vibed/Vibed.cpp:206`, `:208` |
| `m_outsamp` (`oversample` samples) | `plugins/Vibed/VibratingString.cpp:44` |
| `m_impulse` (`stringLength` floats, or `len` when `state`) | `plugins/Vibed/VibratingString.cpp:53` / `:58` |
| Two `DelayLine` objects via `initDelayLine` | `plugins/Vibed/VibratingString.cpp:62-63`, `:71-75` |
| Two delay-line data buffers, `stringLength` samples each | `plugins/Vibed/VibratingString.cpp:77` |
| Per-string temporary `VibratingString{...}` move-assigned into the array | `plugins/Vibed/Vibed.cpp:81` |

Derived from code, **not measured**: 1 + 6 operator-new calls per active string. For the
default preset (only string 1 active) that is 7 allocations per note start.

## Bounds (determined from the code)

| Quantity | Bound | Source |
| --- | --- | --- |
| Key | clamped to 0..127 | `src/core/Note.cpp:124`, `include/Note.h:92` |
| Default key / base frequency | key 57 (A4), 440 Hz | `include/Note.h:90`, `:96` |
| 12-TET frequency | `440 · 2^((key + slide − baseNote + masterPitch + detune)/12 + instrumentPitch/1200)` | `src/core/NotePlayHandle.cpp:644-645` |
| Instrument pitch | ±100 cents | `include/Pitch.h:36-37`, `src/tracks/InstrumentTrack.cpp:68` |
| Master pitch (only when the track uses it) | ±12 semitones | `src/core/Song.cpp:110`, `src/tracks/InstrumentTrack.cpp:179` |
| MPE per-note pitch | ±4800 cents, applied as a ratio, not gated by `IsNotBendable` | `include/MpeExpression.h:53`, `src/core/midi/MpeExpression.cpp:54-57`, `src/core/NotePlayHandle.cpp:656` |
| Note detune | value read from the automation clip at 0 (`BaseDetuning`); **range not traced** | `src/core/NotePlayHandle.cpp:41-43` |
| Microtuner / session tuning | data-driven (keymap base frequency, scale intervals); no output clamp found in `keyToFreq`; **session-tuning path not traced** | `src/core/Microtuner.cpp:65-100`, `src/core/NotePlayHandle.cpp:609-634` |
| Vibed oversample | 1..16 | `plugins/Vibed/Vibed.cpp:118-119` |
| Vibed detune | ±0.1 | `plugins/Vibed/Vibed.cpp:114-115` |
| Harmonic octave factor | 0.25..7.0 | `plugins/Vibed/Vibed.cpp:78` |
| `m_oversample` | `2·oversample / int(sampleRate/baseSampleRate)`; 2..32 when the ratio is 1 | `plugins/Vibed/VibratingString.cpp:39` |
| `stringLength` | `m_oversample · sampleRate / pitch + 1`, then `× (1 − detune)` | `plugins/Vibed/VibratingString.cpp:46-47` |

**Derived pitch floor.** The lowest 12-TET note these ranges reach is 7.72 Hz (key 0,
master −12, instrument −100 cents). With harmonic factor 0.25 the string is 1.93 Hz. With an
additional MPE −4800 cents the note is 0.48 Hz and the string 0.12 Hz. These are the code's
limits, not audible limits. The Microtuner and session-tuning paths are not closed by these
ranges, so the true floor is not established by the code.

**Simultaneous notes.** No per-instrument or global cap was found. `NotePlayHandleManager::acquire`
extends its pool when empty (`src/core/NotePlayHandle.cpp:744`, `extend` at `:762`). Chord and
arpeggio sub-notes are their own `NotePlayHandle`s (`src/core/NotePlayHandle.cpp:96-98`). Whether
`acquire` runs on the audio thread was not traced; if it does, it is a second allocation on that
path.

## Derived buffer sizes (computed, not measured)

Assumptions: output 48 kHz, base 44.1 kHz (ratio 1, so `m_oversample = 2·oversample`), detune 0,
three buffers per string (impulse plus two delay lines). Detune ±10% scales the sample counts by
1.1 or 0.9. Nine active strings multiply the per-string column by nine.

| Case | String Hz | Samples per buffer | Per buffer | Per string |
| --- | --- | --- | --- | --- |
| C4 (261.6 Hz), harm 1.0, oversample 1 | 261.6 | 366 | 1.5 KB | 4.4 KB |
| C2 (65.4 Hz), harm 1.0, oversample 16 | 65.4 | 23,484 | 94 KB | 0.28 MB |
| Key 0 (16.35 Hz), harm 0.25, oversample 16 | 4.09 | 375,743 | 1.5 MB | 4.5 MB |
| 12-TET floor, harm 0.25, oversample 16 | 1.93 | 796,171 | 3.2 MB | 9.6 MB |
| MPE floor, harm 0.25, oversample 16 | 0.121 | 12,738,750 | 51 MB | 153 MB |

## Why a pool cannot be sized without a decision

- The buffer size grows with `oversample × sampleRate / pitch`. At the MPE floor one string
  needs about 153 MB, and nine strings and any polyphony multiply that. No fixed pool covers
  the reachable domain.
- So any realtime-safe design must bound the largest string (D1), bound simultaneous string
  states (D2), and choose what happens beyond those bounds (D3).
- A pool alone is not sufficient. The per-note initialisation is also O(string length) and runs on
  the audio thread: the fill loop in `initDelayLine` (`plugins/Vibed/VibratingString.cpp:78-79`),
  `setDelayLine` (`plugins/Vibed/VibratingString.h:105`), and `resample`
  (`plugins/Vibed/VibratingString.cpp:94`). Moving the allocation off the thread does not remove
  this work (D4).

## Owner decisions (blocking)

- **D1 — Largest supported string length** (equivalently the lowest supported string
  frequency). Example: a 2^17-sample cap at oversample 16 and 48 kHz gives a floor of about
  11.7 Hz (32 × 48000 / 131072), a 512 KB buffer and 1.5 MB per string. Strings below the cap are
  the behaviour change.
- **D2 — Polyphony.** How many simultaneous string states an instrument (or the process) may
  hold, and what a new note does when the pool is full.
- **D3 — Policy beyond D1/D2.** Silence, clamp the pitch, drop the string, or keep allocating as a
  documented realtime exception. Each changes audible output or keeps the defect.
- **D4 — Initialisation cost.** Whether the O(length) initialisation may stay on the audio thread
  up to the D1 cap. If not, a note-ahead hook is needed, and none was found. Note creation on the
  audio thread was not traced in this pass.

My recommendation, for the owner to accept or reject: a fixed per-instrument pool sized by D1 and D2,
notes beyond the cap silent (D3), and initialisation kept on the audio thread (D4), with the cap set
where the owner judges sub-audible strings begin. This is a recommendation only; the owner picks the
numbers.

## Related observed defect (separate; not fixed here)

A zero frequency reaches Vibed. With the Microtuner enabled and an unmapped key, or the session
table unmapped, `m_frequency` is set to 0 (`src/core/NotePlayHandle.cpp:622`, `:638`). The play path
checks only the key range (around `src/core/NotePlayHandle.cpp:241-250`), then calls `playNote`
(`:307`, `src/tracks/InstrumentTrack.cpp:700`) with no frequency check. Vibed then divides by the
pitch (`plugins/Vibed/VibratingString.cpp:46`), which gives infinity converted to `int` (undefined
behaviour), and the result feeds `std::make_unique<float[]>` (`:53`, `:58`).

This is from reading the code; it was **not run**. It should be filed as its own cluster.

Also unverified: `plugins/Vibed/VibratingString.cpp:39` divides by `int(sampleRate/baseSampleRate)`,
which is zero if the output rate is below the base rate. Whether that is reachable was not checked.

## Regression status

- **No product change, so no RED/GREEN run.** Luna asked for a registered allocation regression.
  That test would assert zero operator-new calls in `Vibed::playNoteImpl` on note start (pattern:
  `tests/src/core/InstrumentLatencyPdcTest.cpp:396-400`, `test::tlCountAllocations`). It fails on the
  current tree, and it can only pass after D1–D3 are decided and implemented. Committing it red would
  fail `ctest`; committing it green would enshrine the defect. It should be added with the fix.
- **Probe limit.** The probe counts `operator new` only; `malloc` and Qt implicit sharing are not
  counted (`docs/KNOWN-LIMITATIONS.md`, "The allocation probe").
- **Existing coverage.** `ctest -N -R '[Vv]ibed'` lists 0 tests. The cited
  `tests/reference/Vibed/Vibed.cpp` is frozen reference code, built only as `partc_ref_vibedstrings`
  (`tests/CMakeLists.txt:2566-2584`), not a ctest entry.
- **No measurement.** The product `vibedstrings` module is not built in this tree
  (`build/plugins/Vibed` holds no `.so`), so the derived numbers above have not been checked against
  a probe run.

## Commands run (exact, with EXIT codes)

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-501.r2/logs/`.

| Command | EXIT | Log / result |
| --- | --- | --- |
| `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity-1.log`, line 108: "FAIL: complexity ratchet regressed" |
| `bash tests/file-length-gate.sh --check` | 1 | `gate-filelength-1.log`, line 38: "FAIL: file-length ratchet regressed" |
| `cd build/tests && ctest -N -R '[Vv]ibed'` | 0 | `ctest-N-vibed-1.log`: "Total Tests: 0" |
| fff grep "FAIL" and "Vibed" over the log folder | — | Only the two FAIL lines above; no Vibed entry in either gate log |

Both gates fail on ratchet regressions in files this cluster does not touch. The same two gates
failed on the base commit in round 1. No limit or baseline was changed.

## Files changed by this round

- `docs/qwen-fixes-2026-10/cluster-new-p9-501.md` only.
