# Space Bunny fixer lane `theory` — batch-01 (2026-10-05)

Six findings, all in `src/core/ChordProgression.cpp`. They collapse to **three
distinct defects**: the arpeggio-overflow report was filed four times (932, 4659,
48, plus the broken-pattern half of 4659) and the negative-start report twice
(1885, 930). Two real defects were fixed alongside the third, all in one file,
each as its own commit with its own RED.

| Cluster | Verdict | Commit |
|---|---|---|
| 932 | FIXED | `4865cb348` |
| 4659 | DUPLICATE | of 932 |
| 48 | DUPLICATE | of 932 |
| 1885 | FIXED | `b22c4fb39` |
| 930 | DUPLICATE | of 1885 |
| 52 | FIXED | `977718c8c` |

---

### Cluster 932 — Arpeggios can place notes beyond their allotted room

**Verdict:** FIXED

**Evidence:** `src/core/ChordProgression.cpp:147` (pre-fix) clamped a note's
length to a floor of 1 *after* `space` had already been driven to 0 or negative,
so a voice with no room still emitted a one-tick note starting past the span.
`layOutArpeggio` (`:161`) sizes the slot as `max(1, span / voiceCount)`, which for
`span == 1` and a triad is 1 — placing voices 2 and 3 at `pos+1` and `pos+2`.
`appendNote` is the single choke point shared by all four patterns, so the
one-line guard there fixes arpeggio up, arpeggio down and broken alike.
The invariant it protects is written down in the header itself
(`include/ChordProgression.h:173-175`: "every note starts inside that window and
no note runs past it") and was already half-tested by
`layOutChordRespectsItsRoom`.

**Commit:** `4865cb348`

**Test:** `ChordProgressionTest::aOneTickStepNeverPlacesANotePastIt`

**Red/green:**
```
cmake --build build --target ChordProgressionTest   # EXIT=0
cd build/tests && ctest -R '^ChordProgressionTest$' --output-on-failure   # EXIT=8
  FAIL!  aOneTickStepNeverPlacesANotePastIt() 'note.pos >= 10 && note.pos < 11' returned FALSE. (arpeggio_up: 64@11:1)
# after the fix: same ctest -> EXIT=0
```
Sibling sweep after all three fixes: `ChordTrackTest`,
`ControlChordWriteTest`, `ControlChordCommandsTest`, `ArrangerTimelineTest`,
`SampleAccurateAutomationTest` — all `Passed`, EXIT=0.

---

### Cluster 4659 — Short arpeggios place notes beyond their step

**Verdict:** DUPLICATE

**Evidence:** Same defect as 932, reported from the broken-pattern call site
(`src/core/ChordProgression.cpp:410-415`, which calls `appendNote` with the same
`max(1, span / keys.size())` slot) and pointing at the same floor clamp in
`appendNote`. It is not a second bug to fix separately — the `Broken` branch is
covered by the `4865cb348` fix and asserted in the new test's pattern loop.
Its extra suggestion ("cap the number of slots to available ticks") is the same
outcome arrived at differently.

**Commit:** none (covered by `4865cb348`)

**Test:** `ChordProgressionTest::aOneTickStepNeverPlacesANotePastIt` (asserts
`Broken` alongside both arpeggios)

**Red/green:** as cluster 932.

---

### Cluster 48 — Arpeggios can spill past their allotted room or leave a silent tail

**Verdict:** DUPLICATE

**Evidence:** The overflow half is identical to 932. The "silent tail" half —
`slot = span / voiceCount` truncating, so a 5-tick room with 3 tones fills only
the first 3 ticks — is real but is **not a defect**: the tail is a consequence of
integer slot division, and the suggested remedy (per-voice boundaries that
distribute the remainder) would change the notes emitted for every ordinary
arpeggio, breaking the existing `layOutChordRespectsItsRoom` expectation
(`include/ChordProgression.h` documents "no note runs past it" — it says nothing
about filling every tick). That is a design change, not a fix, and rule 7 bars
it. The overflow half stands as fixed under 932.

**Commit:** none for the tail (out of scope by design); overflow covered by `4865cb348`

**Test:** none for the tail; `aOneTickStepNeverPlacesANotePastIt` for the overflow

**Red/green:** as cluster 932.

---

### Cluster 1885 — Negative progression starts produce invalid or collapsed output

**Verdict:** FIXED

**Evidence:** `validateKey` (`src/core/ChordProgression.cpp:249-271`) checked
`root`, `octave` and `stepTicks` but not `start`, so a negative start reached both
output paths and each mishandled it differently — `generateNotes` clamps
`pos = max(pos, 0)` at `:461` and emits a collapsed one-tick chord at tick 0 that
collides the next step, while `trackEventsFor` (`:505`) emits `event.pos < 0`
that `ChordTrack::set` refuses (`src/core/ChordTrack.cpp:241`). Neither result is
usable, so there was no consistent clipping rule to pick — refusing is the one
rule both paths can honour. The control layer already bounded `tick` to
`0..MaxSchemaInteger` (`src/core/ControlChordSupport.cpp:348`), so no reachable
request changes meaning and no command schema changed.

**Commit:** `b22c4fb39`

**Test:** `ChordProgressionTest::aNegativeStartIsRefused`

**Red/green:** RED captured in isolation (fix reverted, test kept):
```
cmake --build build --target ChordProgressionTest   # EXIT=0
cd build/tests && ctest -R '^ChordProgressionTest$' --output-on-failure   # EXIT=8
  FAIL!  aNegativeStartIsRefused() '!chordsFor(...)' returned FALSE. (a walk starting before tick 0 was accepted)
# fix restored: same ctest -> EXIT=0
```

---

### Cluster 930 — Negative starts can emit invalid events or phantom notes

**Verdict:** DUPLICATE

**Evidence:** Same defect as 1885, same missing check in the same
`validateKey`, differing only in the reproduction numbers (`start = -2000`,
`stepTicks = 1920` vs `-100`/`100`) and in emphasising the event path rather than
the note path. Both halves are what `b22c4fb39` fixed.

**Commit:** none (covered by `b22c4fb39`)

**Test:** `ChordProgressionTest::aNegativeStartIsRefused`

**Red/green:** as cluster 1885.

---

### Cluster 52 — NaN variation passes validation

**Verdict:** FIXED

**Evidence:** `validateWalk` bounded variation with
`request.variation < 0.0f || request.variation > 1.0f`
(`src/core/ChordProgression.cpp:293`, pre-fix). NaN compares false against both
bounds, so it passed; `drawVariation` then handed it to `std::lround` at `:115`
(position jitter) and `:120` (velocity), where the result is
implementation-defined. The wire cannot deliver it — `QJsonValue::toDouble` has
no NaN form — but `chordsFor` is exported and any C++ caller can. `std::isfinite`
now comes first; every finite value is judged exactly as before.

**Commit:** `977718c8c`

**Test:** `ChordProgressionTest::theNamesAndBoundsAreRefused`

**Red/green:** RED captured in isolation (fix reverted, test kept):
```
cmake --build build --target ChordProgressionTest   # EXIT=0
cd build/tests && ctest -R '^ChordProgressionTest$' --output-on-failure   # EXIT=8
  FAIL!  theNamesAndBoundsAreRefused() '!chordsFor(...)' returned FALSE. (a NaN variation passed validation)
# fix restored: same ctest -> EXIT=0
```

---

## Gates

Both run before each of the three commits, each `EXIT=0`:

```
bash tests/complexity-gate.sh --check    # EXIT=0  (PASS, no regressions)
bash tests/file-length-gate.sh --check   # EXIT=0  (PASS, no regressions)
```

## Leftovers

None. The only thing consciously not done is cluster 48's "distribute the
remainder across voices" suggestion, left alone on purpose as a design change
rather than a fix (see its Evidence). All three fixes touch only
`src/core/ChordProgression.cpp` (+18/-2) and add three cases to the existing
`tests/src/core/ChordProgressionTest.cpp` (+68); no new test file, no CMakeLists
or fork-sources change, no command or schema touched, nothing on an audio-thread
path.