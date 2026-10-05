# theory batch-02 — 2026-10-05

Six findings, four distinct defects: two fixed here, two already fixed by earlier
commits on this branch. Clusters 5510, 50 and 4660 are one defect reported
three times.

### Cluster 5510 — Progression timeline arithmetic can overflow

Verdict: FIXED

Evidence: every position of a walk is `start + index * stepTicks` in a signed
int32 (`tick_t = std::int32_t`, `src/core/ChordProgression.cpp:467` and `:519`),
and `validateKey` bounded the root, octave, step and start but not the extent.
Both `tick` and `step_ticks` reach `MaxSchemaInteger = 2147483647` at the
control layer (`src/core/ControlChordSupport.cpp:346-347`) with `steps` up to
`MaxSteps = 64`, so `start = 0, stepTicks = INT32_MAX, steps = 3` passed every
bound and only the second step overflowed. `validateKey` now computes the last
position in 64 bits and refuses a walk that does not end on a tick.

Commit: da1ee6566

Test: ChordProgressionTest::aWalkThatDoesNotFitInATickIsRefused

Red/green:
- Red: `ctest -R '^ChordProgressionTest$' --output-on-failure` -> EXIT=8,
  'a walk whose steps overflow a tick was accepted'
- Green: same command -> EXIT=0

### Cluster 50 — Valid progression requests can overflow signed tick arithmetic

Verdict: DUPLICATE (of cluster 5510; the same three lines and the same fix)

Evidence: reported `start = 0, stepTicks = INT32_MAX, steps = 3`, which is the
case proved in cluster 5510 and refused by the same `validateKey` bound. Fixed
by da1ee6566; a second commit for it would have been the same change twice.

Commit: da1ee6566 (shared)

Test: ChordProgressionTest::aWalkThatDoesNotFitInATickIsRefused (shared)

Red/green: as cluster 5510.

### Cluster 4660 — Progression timeline arithmetic can overflow

Verdict: DUPLICATE (of cluster 5510; the same arithmetic, both call sites)

Evidence: cites `ChordProgression.cpp:432-433` (note generation) and `:485`
(`trackEventsFor`). Those two sites plus `chordEnd()` were the only places the
extent is computed, and the single validation bound now covers all three, so no
separate fix applies.

Commit: da1ee6566 (shared)

Test: ChordProgressionTest::aWalkThatDoesNotFitInATickIsRefused (shared)

Red/green: as cluster 5510.

### Cluster 49 — Position variation can make adjacent chords overlap

Verdict: ALREADY-FIXED

Evidence: `chordEnd()` at `src/core/ChordProgression.cpp:442-451` already draws
the NEXT chord's jitter with the same `drawVariation()` the next chord is laid
out with, and returns `min(nextNominal, max(0, nextNominal + jitter))`. The room
of chord *i* is therefore bounded by chord *i+1*'s actual jittered start, which
is the fix the finding asks for. The reported case (a chord filling its room to
tick 100 while the next is jittered to 88) yields room ending at 88, not 100.

Commit: none (pre-existing on this branch)

Test: none added — the behaviour is already asserted by the existing variation
cases in ChordProgressionTest.

Red/green: none.

### Cluster 51 — Negative starts produce invalid chord-track events

Verdict: ALREADY-FIXED

Evidence: commit b22c4fb39 "fix(chord): a progression walk may no longer start
before tick 0" added the `request.start < 0` refusal to `validateKey`
(`src/core/ChordProgression.cpp:275-279`), which is before the new extent bound,
with the regression test `aNegativeStartIsRefused`. The finding's claim that
`trackEventsFor()` can return an event `ChordTrack::set()` refuses no longer
holds: `ChordTrack::rangesAreWritable` refuses `pos < 0`
(`src/core/ChordTrack.cpp:241`) and no negative-position event is ever built.

Commit: b22c4fb39 (pre-existing on this branch)

Test: ChordProgressionTest::aNegativeStartIsRefused (pre-existing)

Red/green: none in this batch.

### Cluster 55 — Malformed chord-track numbers can become valid zero values

Verdict: FIXED

Evidence: `ChordTrack::loadSettings` read `pos`/`len`/`root`/`octave` with
`toLongLong()`/`toInt()` and never checked the conversion's ok flag, while the
line below already refused an event whose chord NAME cannot be resolved. A
malformed event therefore did not fail the load — it became a different valid
chord: an absent `pos` became 0, a malformed `root` became 0 and a malformed
`octave` became 4, which is exactly the writer's own default for an absent one.
The parse is now checked per attribute in `eventFromElement()`, with `pos`
required and a present-but-malformed optional value skipped rather than defaulted.

Commit: a5c26cd5d

Test: ChordTrackTest::malformedNumbersAreSkipped

Red/green:
- Red: `ctest -R '^ChordTrackTest$' --output-on-failure` -> EXIT=8,
  'a chord element with no position became a chord at tick 0'
- Green: same command -> EXIT=0

## Full verification for the batch

- `ctest -R '^(ChordProgressionTest|ChordTrackTest|ControlChordCommandsTest|ControlChordWriteTest|ArrangerTimelineTest)$' --output-on-failure` -> EXIT=0,
  5/5 passed (the two other executables that cover the changed files).
- `bash tests/complexity-gate.sh --check` -> EXIT=0. First pass EXIT=1: the
  check pushed `ChordTrack::loadSettings` to CCN 16 and then the extracted
  `eventFromElement` to CCN 12. Resolved by splitting the readers
  (`requiredTick`/`optionalTick`/`optionalInt`/`fitsTick`) rather than
  re-anchoring the ratchet.
- `bash tests/file-length-gate.sh --check` -> EXIT=0.

Leftovers: none. The complexity gate's baseline was NOT re-anchored; both fixes
pass `--check` against the existing baseline.
