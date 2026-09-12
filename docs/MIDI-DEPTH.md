# MIDI depth — note probability, velocity randomisation, and note search/transform

Lane `post-alpha/midi-depth` (worktree `zene-pa-mididepth`), base `post-alpha/v0.2` = `0c23587d2`.
Scope per `BACKLOG.md`'s roadmap-gap register, *General MIDI depth*: "**Smallest honest version:**
scale-aware editing, note probability/velocity randomisation, and a search/transform tool for note
events." MIDI learn is a separate lane and is not touched here.

The order of work was: **establish what already exists**, then implement the smallest honest delta.
That inventory is §1, and it changes the shape of the answer — one of the three listed items was
mostly already there and was deliberately *not* rebuilt.

---

## 1. What already existed (audited, with file:line)

### 1.1 Scale machinery: two unrelated things, and the useful one is already wired

| What | Where | What it actually is |
| --- | --- | --- |
| `Interval` / `Scale` | `include/Scale.h:38`, `:66`; impl `src/core/Scale.cpp:75` | **Scala-style tuning**, not pitch-class scales: a list of frequency ratios/cents (`m_numerator`/`m_denominator`/`m_cents`, `include/Scale.h:59-62`). Consumed only by the Microtuner (`src/core/Microtuner.cpp:33,73,120`) and stored per project (`Song::getScale` `include/Song.h:338`, saved by `Song::saveScaleStates` `src/core/Song.cpp:1339`). It answers "how many cents is this degree", not "is this note in C major". |
| `InstrumentFunctionNoteStacking::Chord` / `ChordTable` | `include/InstrumentFunctions.h:83` (`struct Chord`), `:94` (`isScale() { return size() > 6; }`); impl `src/core/InstrumentFunctions.cpp:183` | **Pitch-class scales** as semitone sets, plus chords in the same table. This is the scale vocabulary the piano roll uses. |
| Piano-roll scale + key selector | `src/gui/editors/PianoRoll.cpp:385-405` (models built from `ChordTable`'s `isScale()` entries), `:511-600` (`markSemiTone`, `SemiToneMarkerAction::MarkCurrentScale`), `:5015` (`keyChanged` re-highlights) | **Scale-aware editing already exists as highlighting**: the selected key/scale marks semitones (`include/PianoRoll.h:299 m_markedSemiTones`) and the note grid is drawn against them. |
| Scale/key selection persisted | `src/gui/editors/PianoRoll.cpp:5672-5689` (writes `key`, `chord`, `scale` attributes and the `markedSemiTones` list), `:5697-5705` (reads them back) | The scale selection **and** the marked semitones survive save/reload. |
| Scale/chord applied at playback | `src/core/InstrumentTrack.cpp:579` → `include/InstrumentFunctions.h:65 processNote()`, impl `src/core/InstrumentFunctions.cpp:268,341,508` | The `chordcreator` instrument function stacks in-scale/chord notes at playback, driven by per-track models saved at `src/core/InstrumentTrack.cpp:905`. |

**Verdict: do not rebuild scale awareness.** A producer already has scale highlighting, scale
selection persistence, and playback-time chord/scale stacking. What is missing is not the scale
*vocabulary* but a scale-aware **edit operation** (snap the notes that are out of key), which the
note search/transform module in §2.2 supplies — reusing the same pitch-class idea and accepting a
pitch-class vector, so the piano roll can feed it from the existing `ChordTable`.

### 1.2 Probability: essentially absent, and the one existing chance is not seedable

`grep -rn "probability" include/ src/ plugins/` finds **only vendored code** (the JSON library
inside `plugins/NeuralAmp/rtneural/`). There is no probability field on the note model
(`include/Note.h:272-289` — key, volume, panning, length, position, detuning, type, slide), and
nothing in the note serialisation (`src/core/Note.cpp:190-236`).

The nearest existing thing is the **arpeggiator's** skip/miss rate:

- `include/InstrumentFunctions.h:217 m_arpSkipModel`, created at `src/core/InstrumentFunctions.cpp:309`,
  applied at `:418` (`if (m_arpSkipModel.value() && fastRandInc(100.f) <= m_arpSkipModel.value())`) and `:429`.
- It is per *instrument function*, not per note; it only exists inside the arpeggiator.
- Its randomness is `fastRand()` — `include/lmms_math.h:88`, a `thread_local` LCG seeded to `1`
  — so it is **not project-seeded**: the same project does not reproduce the same take, and no seed
  in the file can make it do so. It is saved as an instrument setting
  (`src/core/InstrumentFunctions.cpp:532,550`), not as a seed.

That is the gap the register names: per-note probability that is **seeded, persisted and actually
heard**.

### 1.3 A note search/transform operation: absent

Nothing in `include/` or `src/` selects note events by predicate and transforms them. The piano roll
has one-off GUI actions (`PianoRoll::shiftSemiTone`, `selectNotesOnKey`, `CopyAllNotesOnKey` at
`src/gui/editors/PianoRoll.cpp:600`, quantise on insert at `src/tracks/MidiClip.cpp:185`) — each is
bound to GUI state and none is callable or testable headlessly.

---

## 2. What was added

Two new modules, both plain functions over `Note`/`NoteVector`, both testable with no Engine, no
track and no GUI.

### 2.1 Seeded note probability and velocity jitter (affects playback, persisted, seedable)

- `include/Note.h` / `src/core/Note.cpp`: `Note::probability()` (default `1.0`) and
  `Note::velocityJitter()` (default `0.0`), carried through the copy constructor and `operator=`
  (both are used on the playback path). Serialised as **optional** attributes `prob` and `veljit`,
  written only when they differ from the default — the same rule the tree already uses for `slide`
  (`src/core/Note.cpp:200-207`), so no DataFile version bump.
- `include/Song.h` / `src/core/Song.cpp`: `Song::midiSeed()` / `setMidiSeed()`, persisted in the
  project header as `midiseed`, **written only when non-zero** (`Song.cpp` `saveProjectFile`), read
  back on load, reset by `createNewProject`.
- `include/NoteRandom.h` / `src/core/NoteRandom.cpp`: a stateless integer-finaliser hash
  (`mix()`), `roll()/rollUnit()`, `passesProbability()`, `velocityFactor()`, plus
  `readProjectSeed()/writeProjectSeed()`. **No allocation, no locking, no state** — callable on the
  audio thread by construction.
- `src/tracks/InstrumentTrack.cpp` (`play()`, in the existing per-note scheduling loop, *not* in a
  per-frame loop): a note that loses its roll is skipped before a `NotePlayHandle` is created, and
  velocity jitter is applied to the play handle's own copy of the note (never to the persisted
  note).

**Semantics, stated exactly (the question the brief asks):**

| Question | Answer |
| --- | --- |
| Does a 50% note play on every pass, or reroll? | **Rerolled once per note trigger**, at scheduling time in `InstrumentTrack::play()`. But the roll input is `(seed, key, pos, length)` — all static per note — so the outcome is *stable across passes*: a 50% note plays in every repeat of its clip or in none of them. |
| Where is the decision made? | In the per-note scheduling loop of `play()` (`src/tracks/InstrumentTrack.cpp:788-804`), never in the per-frame loop. No allocation, no lock, no growth: it is a handful of integer ops plus a compare. |
| Per note or per clip? | **Per note.** Stored on the `Note`, so a clip can hold some 100% notes and some 50% notes. |
| Is it seedable / repeatable? | **Yes** — `midiseed` in the project header. Same seed ⇒ same take; a different seed re-rolls it. Additionally, a note whose pitches/positions/lengths are identical gets the same roll (two identical notes in two clips move together) — stated as a consequence, not a bug. |
| Velocity jitter range | Multiplicative factor in `[1-j, 1+j]`, clamped to `[0, 200]` velocity. `j=0` returns exactly `1.0` without computing the hash. |

**Deliberately not a per-pass reroll.** Per-pass variation would need persisted per-note playback
state or a transport-reset hook, and would make "the same take twice" unprovable without extra
state. The register asks for a repeatable take; the seed is the re-roll. This is named again in §5.

### 2.2 Note search/transform operation (headless-testable)

`include/NoteTransform.h` / `src/core/NoteTransform.cpp`:

- `Filter` — optional clauses for key range, velocity range, position range, and scale membership
  (`InScale` / `OutOfScale`, pitch-class based; degrees are taken modulo 12 in either direction),
  plus `invert` for the complement. `matches()` / `select()`.
- Transforms, each returning the number of notes whose value actually changed:
  `transpose()` (clamped to the MIDI range), `offsetVelocity()`, `scaleVelocity()`,
  `quantizePositions()` (`Nearest`/`Floor`/`Ceil`), `snapToScale()` (nearest in-scale pitch, a tie
  resolving downward), plus `sortByPosition()` (restores the `Note::lessThan` order `MidiClip`
  expects) and `pitchClasses()`.
- **GUI hook-up: not done** — the piano roll is not wired to this in this change. The module is the
  headless-testable operation the brief asked for; the hook-up is a follow-up (§5).

---

## 3. Proofs (all measured, unpiped, on this branch)

### 3.1 Build and tests

`JOBS=4 bash tools/local-ci.sh --build-dir build --jobs 4` (committed mode 100644, so run with `bash`),
CI `linux-x86_64` flags plus the printed deviation `-DWANT_QT6=ON` (no Qt5 dev files on this box).

| Step | Pre-change (base `0c23587d2`) | This branch |
| --- | --- | --- |
| configure EXIT | `0` | `0` |
| build EXIT | `0` | `0` |
| ctest EXIT | `0` | `0` |
| ctest totals | `100% tests passed, 0 tests failed out of 25` | `100% tests passed, 0 tests failed out of 28` |

The pre-change column is the base-commit build measured before any edit; the branch column is the
final commit's `local-ci.sh` run. (Mid-lane the `build/` directory was deleted by something outside
this lane — disk pressure on a box running five sibling lanes — and rebuilt from scratch; the
numbers above are from the final, complete run, and the render claims in §3.2 were re-measured on
the final binary and reproduce the same `max|delta|` to the last digit.)

Three new QtTest suites (28 = 25 + 3), all passing, empty failure lists:

| Suite | Slots | What it asserts |
| --- | --- | --- |
| `tests/src/core/NoteRandomTest.cpp` | 9 passed | the roll is a pure function; **same seed ⇒ identical played set, different seed ⇒ different set**; probability extremes are exact (1 ⇒ all, 0 ⇒ none); distribution sanity over 2000 notes; jitter bounds and exact `1.0` at `j=0`; the header seed attribute round-trips and is not written at the default. |
| `tests/src/core/NoteTransformTest.cpp` | 12 passed | every operation against **exact resulting note sets** (keys, positions, velocities): selection by key/velocity/position/scale membership and their complements, clamping at both MIDI rails, velocity clamping and rounding, grid quantise in all three modes, snap-to-scale including the downward tie-break, re-sorting, and two select-then-transform pipelines. |
| `tests/src/core/MidiProbabilityPersistenceTest.cpp` | 6 passed | a default note writes **no** new attribute (exact attribute list = upstream's); `prob`/`veljit` round-trip save→load→save byte-identically; the copy constructor and `operator=` carry them; setters clamp; **a pre-feature project loads with the documented defaults and re-saves byte-identically**; a project with probability set survives a full file round trip with the seed in the header. |

### 3.2 Renders — the repeatability pair, behaviour preservation and the controls

Harness: `tests/data/midi-depth/render-proof.py` over seven fixtures generated by
`tests/data/midi-depth/gen-fixtures.py` from the shipped tutorial project
`data/projects/tutorials/editing_note_volumes.mmp` (`baseline.mmp` is a byte-for-byte copy,
sha256 `9a669ed15e4bc820602a4277c85245f6af0fe3139d18d9405b2da77ca435fc2a`). Each fixture is rendered
twice through `build/lmms render <project> -f wav -a`, once on the pre-change binary and once on the
branch binary, and the `data` chunk is compared sample-for-sample.

```
python3 tests/data/midi-depth/render-proof.py render  <build-dir> <out-dir>
python3 tests/data/midi-depth/render-proof.py compare <pre-dir>  <post-dir>
```

`compare` exits **0** and reports:

| Claim | Verdict | max&#124;delta&#124; | audio sha256 |
| --- | --- | --- | --- |
| `baseline.mmp` pre vs post — **behaviour preservation** | PASS | `0.000000000` | same |
| `prob-all-one.mmp` pre vs post — an explicit `prob="1"` is inert | PASS | `0.000000000` | same |
| `prob-seed1.mmp` pre vs post — probability is not ignored | PASS | `0.639514536` | different |
| `veljit-seed1.mmp` pre vs post — jitter is not ignored | PASS | `0.387972593` | different |
| `prob-seed1.mmp` **repeat** vs itself — same seed, second pass | PASS | `0.000000000` | same |
| `veljit-seed1.mmp` **repeat** vs itself — same seed, second pass | PASS | `0.000000000` | same |
| `prob-seed1` vs `prob-seed2` — **different seed** | PASS | `1.048172593` | different |
| `veljit-seed1` vs `veljit-seed2` — **different seed** | PASS | `0.637808353` | different |
| `prob-seed1` vs `prob-seed1-vol` — **sensitivity control** (one note, one velocity step) | PASS | `0.006878555` | different |

Liveness: every render is non-silent — post-change peaks `1.0669..1.5276`, pre-change `1.2471..1.2486`
(two silent buffers would compare equal and make the "same" rows vacuous). The probability runs are
audibly *smaller* than the baseline (rms `0.2201` → `0.1922` for seed 1, `0.1506` for seed 2), which
is what "some notes were dropped" looks like from outside.

Two harness decisions, both forced by measurement:

1. **Renders are pinned to one CPU (`taskset -c 0`).** LMMS renders play handles on
   `QThread::idealThreadCount() - 1` workers (`src/core/AudioEngine.cpp:85`), and *unpinned*
   renders of the **same untouched project** differ from each other on 1.6% of samples with
   `max|delta| 2.4e-07` (one ulp). Pinning removes that jitter so the comparison measures the
   change under test rather than the scheduler; pinned repeats are bit-identical.
2. **The compared identity is the `data` chunk, not the whole file.** Even pinned, whole-file
   SHA-256 differs run to run because LMMS writes a `PEAK` chunk whose "peak position" float is
   computed off the audio threads and differs by one ulp while every sample is identical. The
   harness prints the whole-file hashes too (`tests/data/midi-depth/render-proof.py` comments,
   §3.2 output) so the difference is not hidden.

Known fixture caveat: the tutorial project references `samples/shapes/smooth_inv_saw.ogg` for
triple-oscillator usr-wave 2, which LMMS cannot resolve from a non-installed tree and logs as
`Sample not found`. The renders are unaffected for comparison purposes — the payload is
bit-identical across runs and non-silent — and the fixture is kept as a verbatim copy of the shipped
project rather than edited, so `baseline.mmp == data/projects/tutorials/editing_note_volumes.mmp`.

### 3.3 Gate registries

- `tests/fork-sources.txt` — `include/NoteRandom.h`, `include/NoteTransform.h`,
  `src/core/NoteRandom.cpp`, `src/core/NoteTransform.cpp` (sorted into place; the file held 100
  entries before this lane and 104 after — 104 is what the duplication gate reports, and it is one
  more than `tests/QA-GATES.md`'s stale "99 files" note claims).
- `tests/upstream-modifications.txt` — the five inherited files this branch changes, each with a
  reason read off its own diff: `include/Note.h`, `include/Song.h`, `src/core/Note.cpp`,
  `src/core/Song.cpp`, `src/tracks/InstrumentTrack.cpp`.

Gate exit codes, measured unpiped from the committed tree (`cmd > log 2>&1; echo EXIT=$?`):

| Gate | Command | Exit |
| --- | --- | --- |
| 6 — no undeclared divergence in upstream code | `bash tests/no-upstream-regression-gate.sh` | `0` |
| 7 — per-file length ratchet | `bash tests/file-length-gate.sh --check` | `0` |
| 4 — per-method complexity ratchet | `bash tests/complexity-gate.sh --check` | `0` (see below) |
| 8 — token duplication | `bash tests/duplication-gate.sh` | `0` (1.05% against a 5% budget) |
| 3 — no tautological tests | `bash tests/no-tautology-gate.sh` | `0` |

**Gate 4 did real work on this lane.** Its first run on the committed feature reported
`REGRESSION: new function over target: lmms::NoteTransform::matches (CCN 16)` and
`snapToScale (CCN 12)` — my own new code, over the CCN 10 target. It was fixed by extracting
`rangeAccepts()` / `scaleAccepts()` / `nearestInScaleKey()` (which also removed a per-note
`std::vector` allocation from `matches()`), **not** by re-anchoring the baseline. All five gates are
green on the final commit; the exit codes above are from that run.

Gate 6 covers the five `upstream-modifications.txt` entries but also honours unrelated declared
divergences that predate this lane (`Brewfile`, `cmake/modules/InstallHelpers.cmake`, the docs), so
the pass is "no *undeclared* change since `tests/gate-base.txt`", not "this lane touched nothing
inherited".

---

## 4. Commit list

```
12b3cacd5 feat(midi-depth): seeded note probability and velocity jitter, and a note search/transform API
43bd26845 test(midi-depth): render fixtures and the headless render proof harness
ea1645914 docs(midi-depth): what already existed, what was added, the measured proofs
<fix>     refactor(midi-depth): keep NoteTransform::matches and snapToScale under the CCN target
```

The fourth commit is the Gate 4 fix described in §3.3 — a pure extraction inside
`src/core/NoteTransform.cpp` with no behaviour change (the render proof's `max|delta|` values are
identical before and after it).

---

## 5. What is NOT done (and why)

- **GUI hook-up of the transform module.** `NoteTransform` has no caller in `src/gui/`. The piano
  roll command surface ("select out of key → snap", "select loud → quieten") is the next slice. The
  module API was shaped for it (`Filter`/`select` + transforms returning change counts).
- **Per-pass reroll.** As stated in §2.1, the roll is stable across passes; variation comes from the
  seed. Persisted per-note playback state or a transport-reset hook would be needed, and the
  problem it solves (a different take on every pass) is a different product decision from the one
  the register asks for.
- **No piano-roll scale highlight was rebuilt** — §1.1 shows it exists and is persisted. A
  `ChordTable` → pitch-class-vector bridge for the GUI is not written; the transform API takes a
  plain `std::vector<int>` of pitch classes so the GUI can build it in one call.
- **No chord track, no groove pool, no MIDI learn.** Out of this lane's scope; MIDI learn is another
  lane, and a chord track / groove pool are weeks-per-tool items the register ranks separately.
- **Probability/velocity jitter is not exposed in the GUI editor** (no drag handle, no right-click
  entry). The values are editable only by file/API today, so the feature is reachable by the render
  path and by tests, not yet by mouse. This is the single biggest gap between this slice and a
  producer using it, and it is the natural next commit.
- **Coverage of the new files is unmeasured.** Gate 2 (coverage) needs a full coverage build
  (`tests/run-coverage.sh --with-coverage`) and was not run in this lane; the new sources have no
  coverage record yet.
- **`tests/QA-GATES.md`'s "99 files" scope note** is not updated by this branch (the integration lane
  unions the registry across sibling lanes); the number measured here is 103.
