# Theory lane — batch-07 report

Three of the six findings were real and are fixed; two were already fixed in the
current tree; one is a false positive by documented design. Every finding's cited
revision is older than the tip, so each was re-read at the current tree before
being classified. Line numbers below are from the tree at this report, unless a
paragraph says otherwise.

## Summary

| Cluster | Verdict | Commit |
| --- | --- | --- |
| 3176 NaN groove strength reaches rounding and tick conversion | FIXED | 64de4ecb9 |
| 2260 Zero groove strength can still change notes | FALSE-POSITIVE | none |
| 5237 Duplicate marker IDs near the integer limit are not repaired | FIXED | c5ab72794 |
| 5238 Adding a marker can overflow its ID counter | FIXED | 35d2b5bec |
| 634 Loading a marker bypasses the nonnegative-tick rule | ALREADY-FIXED | none |
| 635 Invalid or duplicate marker IDs make marker addresses ambiguous | ALREADY-FIXED | none |

### Cluster 3176 — NaN groove strength reaches rounding and tick conversion

Verdict: FIXED

Evidence: `applyGroove` clamped the strength with a bare
`std::clamp(strength, 0.0f, 1.0f)` (src/core/GrooveTemplate.cpp:424 before the
fix). `std::clamp` leaves a NaN as a NaN, because its comparisons are `v < lo`
and `hi < v`, both false for a NaN. The next line's guard
`if (amount <= 0.0f && groove.neutral())` is also false for a NaN, so the NaN
slipped past it and went on to `std::lround(amount * (target - pos))` and the
tick conversion. Fixed by reading a NaN as 0 in a new file-local `grooveAmount`
(src/core/GrooveTemplate.cpp:424-427), the same idiom the quantise's humanise
already uses for its own strength (src/core/NoteTransform.cpp:275).

The helper is file-local rather than inlined because inlining the ternary pushed
`applyGroove` to CCN 11 and the complexity ratchet rejected it; extracting it keeps
`applyGroove` within its budget.

Commit: 64de4ecb9

Test: GrooveTemplateTest::aStrengthThatIsNotANumberMovesNothing()

Red/green: the test probes the `amount <= 0.0f` early return deterministically,
against a NEUTRAL groove where a strength of 0 with a random amount is a
guaranteed no-op. (An earlier form of this test compared a NaN call on a
non-neutral groove against strength 0; that form passed on the unfixed code
because `std::lround(NaN)` happens to return a value that collided, so it was not
a valid detector and was replaced.)

- Red: `cmake --build build --target zene GrooveTemplateTest` EXIT=0;
  `ctest -R '^GrooveTemplateTest$' --output-on-failure` EXIT=8, 11 passed /
  1 failed — "a NaN strength is not 0: it passed the amount <= 0 early return
  that a plain 0 strength stops at".
- Green: same build EXIT=0; `ctest -R '^GrooveTemplateTest$' --output-on-failure`
  EXIT=0, 12 passed / 0 failed.
- Gates: `bash tests/complexity-gate.sh --check` EXIT=0;
  `bash tests/file-length-gate.sh --check` EXIT=0.

### Cluster 2260 — Zero groove strength can still change notes

Verdict: FALSE-POSITIVE

Evidence: this is the documented contract, not an accident.
`docs/GROOVE-POOL.md:90-103` states the two knobs are "two different facts":
strength is how far a note travels, while humanise "is a jitter added *afterwards*",
and gives the quantise's own formula as
`newPos = pos + round(strength * (target - pos)) + jitter(timing)` — the jitter term
is not multiplied by strength. The quantise implementation does exactly that
(src/core/NoteTransform.cpp:285-290), and `applyGroove`'s
`randomTicks`/`randomVelocity` arguments are that same separate control applied
after the groove (src/core/GrooveTemplate.cpp:447-452).

So a non-neutral groove applied at strength 0 still applying jitter is the
intended behaviour, and the `amount <= 0.0f && groove.neutral()` guard exists
precisely to stop the neutral case only. Gating jitter on strength instead would
break the documented humanise surface and the contract `ControlGrooveCommandsTest`
relies on. No code changed.

Commit: none

Test: none

Red/green: none

### Cluster 5237 — Duplicate marker IDs near the integer limit are not repaired

Verdict: FIXED

Evidence: `makeIdsUnique`'s reassignment was
`if ((marker.id < 0 || taken.contains(marker.id)) && next < std::numeric_limits<int>::max())`
(src/core/ProjectMarkers.cpp:60 before the fix). When the counter reached
`INT_MAX` the whole condition was false, so a duplicate or missing id was left
exactly as found — `find()` and `replace()` then addressed one marker while
`remove()` (which erases every match) deleted both. Fixed by falling back to the
lowest free id below the rest of the file's ids rather than leaving the collision
(src/core/ProjectMarkers.cpp:78, helper `lowestFreeId` at
src/core/ProjectMarkers.cpp:53).

The fallback is bounded by `MaxMarkers` (512, include/ProjectMarkers.h:60), which
is also the cap `load()` enforces (src/core/ProjectMarkers.cpp:188). While a
marker still needs an id there are at most `MaxMarkers - 1` ids in use, so a
free slot in 1..MaxMarkers always exists and the scan cannot fail. `lowestFreeId`
takes the bound as a parameter because `MaxMarkers` is a private static member the
file-local anonymous-namespace helpers cannot name.

Commit: c5ab72794

Test: ProjectMarkersTest::duplicateIdsAtTheTopOfTheRangeAreStillMadeUnique()

Red/green:

- Red: `cmake --build build --target zene ProjectMarkersTest` EXIT=0;
  `ctest -R '^ProjectMarkersTest$' -V` EXIT=8, 8 passed / 3 failed — "two
  markers loaded under one id, so an address names both of them".
- Green: same build EXIT=0; `ctest -R '^ProjectMarkersTest$' -V` EXIT=0,
  9 passed / 0 failed (this case alone; the sibling Cluster 5238 test is added in
  the next commit).
- Gates: `bash tests/complexity-gate.sh --check` EXIT=0;
  `bash tests/file-length-gate.sh --check` EXIT=0.

### Cluster 5238 — Adding a marker can overflow its ID counter

Verdict: FIXED

Evidence: `restore()` sets `m_nextId = std::max(m_nextId, marker.id + 1)`
(src/core/ProjectMarkers.cpp:158), which is `INT_MAX` for a marker carrying
`INT_MAX - 1`. `add()` then ran a bare `const int id = m_nextId++;`
(src/core/ProjectMarkers.cpp:115 before the fix), a signed overflow — the marker
came back with id `-2147483648`, an address no `find()` or `remove()` can reach.
Fixed by taking a free id from below when the counter is at the top of its range
(src/core/ProjectMarkers.cpp:123).

The existing `if (m_markers.size() >= MaxMarkers) { return -1; }` at the top of
`add()` already reserves `-1` for "there are MaxMarkers already", and that check
passing guarantees a free id exists, so the return value's meaning and the
`marker.add` command schema are unchanged.

Commit: 35d2b5bec

Test: ProjectMarkersTest::addingAfterTheHighestLoadedIdStillGetsAFreshId()

Red/green:

- Red: `cmake --build build --target zene ProjectMarkersTest` EXIT=0;
  `ctest -R '^ProjectMarkersTest$' -V` EXIT=8, 8 passed / 3 failed — "the
  second new id was -2147483648".
- Green: same build EXIT=0; `ctest -R '^ProjectMarkersTest$' -V` EXIT=0,
  10 passed / 0 failed.
- Gates: `bash tests/complexity-gate.sh --check` EXIT=0;
  `bash tests/file-length-gate.sh --check` EXIT=0.

### Cluster 634 — Loading a marker bypasses the nonnegative-tick rule

Verdict: ALREADY-FIXED

Evidence: `load()` clamps the tick on the way in with the same
`std::max<tick_t>(0, ...)` rule that `add()` and `replace()` use
(src/core/ProjectMarkers.cpp:192), with a comment at
src/core/ProjectMarkers.cpp:191 saying so. A hand-edited
`<marker tick="-192">` is therefore stored at tick 0 and saved that way, not
before the start of the timeline.

Covered by ProjectMarkersTest::aHandEditedFileIsRepairedOnTheWayIn(), which loads
`"1|-192|Drop"` and asserts the marker reads back at tick 0. That case passes
without any change for this defect.

Commit: none

Test: ProjectMarkersTest::aHandEditedFileIsRepairedOnTheWayIn()

Red/green: none (already fixed at the tip; the case was added and passes)

### Cluster 635 — Invalid or duplicate marker IDs make marker addresses ambiguous

Verdict: ALREADY-FIXED

Evidence: the reported `toInt()` without checking success is already guarded —
`savedId()` reads the attribute with `bool ok` and returns `-1` unless the parse
succeeded and the id is non-negative (src/core/ProjectMarkers.cpp:41-46), so a
missing or malformed id is not silently mapped to zero. Duplicate ids are already
repaired on the way in by `makeIdsUnique`
(src/core/ProjectMarkers.cpp:64-81), verified by
ProjectMarkersTest::aHandEditedFileIsRepairedOnTheWayIn() and
ProjectMarkersTest::duplicateIdsAtTheTopOfTheRangeAreStillMadeUnique().

The one residual the finding describes — a duplicate surviving repair — is only
reachable at the top of the id range and was Cluster 5237, fixed in `c5ab72794`.

Commit: none (the duplicate residual was fixed as Cluster 5237)

Test: ProjectMarkersTest::aHandEditedFileIsRepairedOnTheWayIn()

Red/green: none (already fixed at the tip)

## Adjacent tests

Both changed files were rebuilt and re-run against the other tests that touch
them: `ctest -R '^(ControlGrooveCommandsTest|ArrangerTimelineTest|SessionPerformTest|ArrangementModesTest|AudioEditingTest|AafImportTest)$' --output-on-failure`
EXIT=0, 6 of 6 passed.

Leftovers: nothing half-done; no finding was left without a verdict, and no
control command, parameter or result schema was added, removed or changed.