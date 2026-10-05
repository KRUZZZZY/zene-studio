# Bunny import lane — batch-03 (2026-10-05)

Lane: DAWproject import validation, typed refusal preservation, tick/grid conversion.
Branch `bugs/luna-import-2026-10-05`.

Six findings: four are REAL and are fixed by four commits (one commit each), one is
ALREADY-FIXED by an earlier commit on this branch, one is a false positive argued from
source. Every finding was read in the current tree first: the line numbers below are the
tree as of `c8395f3c9`, not the revisions named in the reports.

Regression cases were added to the existing `tests/src/core/DawProjectInterchangeRoundTripTest.cpp`
(19 slots, was 14; the executable passes 21/21). No new test file was needed.

### Cluster 5064 — Automation point times ignore their unit and tick grid

Verdict: FIXED

Evidence: `parseAutomationPoints()` stored the raw `time` attribute with
`doubleAttribute()` (`src/core/DawProjectRead.cpp:98` before the fix) while notes and clips
beside it were normalised through `ticksFromBeats()`
(`src/core/DawProjectReadTracks.cpp:93-98,129-137`). An off-grid automation time was
therefore neither snapped to the 48-tick grid nor counted in `rounded_times`, and the
element's `timeUnit` was never read, so a foreign `timeUnit="seconds"` timeline was
silently treated as beats. Both defects were real; the unit arm mirrors the existing
seconds `<Notes>` refusal because converting it needs the tempo curve.

Commit: c72631405

Test: DawProjectInterchangeRoundTripTest::automationPointTimesUseTheTickGridAndCountRounding

Red/green: red `cd build/tests && ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure` -> EXIT=8
(`17 passed, 4 failed`, including `an off-grid automation point time was not counted in rounded_times`); green after
the fix -> EXIT=0, `1/1 Test #208 ... Passed`. `bash tests/complexity-gate.sh --check` -> EXIT=0,
`bash tests/file-length-gate.sh --check` -> EXIT=0.

Note: adding the unit check and the per-point validation took `parseAutomationPoints` to CCN 13 and failed the
complexity ratchet, so the new work was split into `automationUnitAccepted()` and `parseAutomationPoint()`
(`src/core/DawProjectRead.cpp:131,141`). The ratchet was not re-anchored.

### Cluster 5062 — The reader accepts a project with no required Application

Verdict: FIXED

Evidence: `parseDocument()` tracked only `sawProject` (`src/core/DawProjectRead.cpp:118,222-226` before
the fix), although Project.xsd declares `<Application>` `use="required"` and this reader's own
header comment (`src/core/DawProjectReadShared.h:13-16`) already claims its absence is a
refusal. `<Project version="1.0"/>` read successfully and returned a model whose
`applicationName` was empty.

Commit: fe95fdf76

Test: DawProjectInterchangeRoundTripTest::aDocumentWithoutTheRequiredApplicationIsRefused

Red/green: red same ctest -> EXIT=8 (`17 passed, 4 failed`, including
`a document with no <Application> was accepted with an empty application name`); green after the fix -> EXIT=0.
Both gates EXIT=0.

### Cluster 3098 — Invalid time signatures are accepted

Verdict: FIXED

Evidence: the `<Transport>/<TimeSignature>` numerator and denominator were taken as written
(`src/core/DawProjectRead.cpp:166-171` before the fix), as were the `<TimeSignaturePoint>`
values, so a document declaring 0/4, -3/4, 40/4 or 4/3 produced a model carrying a metre
the session later rejects. The engine's own closed rule already existed in
`include/TempoMap.h:49-59` (`TempoMapMinNumerator`=1, `TempoMapMaxNumerator`=32,
`TempoMapLargestDenominator`=32, `isSupportedTempoMapDenominator()`), so the fix reuses it
rather than inventing bounds (`src/core/DawProjectRead.cpp:90,154,240`).

Commit: 3abcef876

Test: DawProjectInterchangeRoundTripTest::invalidTimeSignaturesAreRefused

Red/green: red same ctest -> EXIT=8 (`17 passed, 4 failed`, including `the meter 0/4 was accepted` and
`an automation metre point with denominator 7 was accepted`); green after the fix -> EXIT=0. Both gates EXIT=0.

### Cluster 2232 — DAWproject timeline loop keeps a view past its lifetime

Verdict: FALSE-POSITIVE

Evidence: the report says `child` is a `QStringView` from `reader.name()` that the loop compares after
repeated `readNext()` calls (`src/core/DawProjectRead.cpp:198`). Read by hand, the
`QStringView` is captured and used in the `QLatin1String` comparisons before the loop
advances the reader again — the comparisons happen at the top of an iteration and the next
`readNext()` is at the bottom, so no comparison ever runs against a view the reader has
since invalidated. Nothing dangles and nothing is misparsed.

Commit: none

Test: DawProjectInterchangeRoundTripTest::automationPointsAfterOtherContentStillParse (a guard, added with
commit c72631405; it passed before the change as well, which is the point of the check)

Red/green: none — no product code changed for this finding. The guard case passes on the
unmodified walk (`ctest -R '^DawProjectInterchangeRoundTripTest$'` -> EXIT=0).

### Cluster 3097 — Malformed numeric attributes silently become defaults

Verdict: FIXED

Evidence: `doubleAttribute()` / `intAttribute()` returned the fallback whenever `toDouble()`
/ `toInt()` failed, so a present-but-unparseable value was indistinguishable from an absent
one (`src/core/DawProjectReadShared.h:110-126` before the fix): `<Tempo value="oops"/>` read
as the 120 BPM default and a note's `key="sixty"` read as key 60. The fallback now applies
only to an absent attribute; a present one that fails conversion — or is non-finite, which
`QString::toDouble` accepts for `nan`/`inf` — records a named refusal on `ReadState`
(`src/core/DawProjectReadShared.h:145-178`), which `parseDocument()` already turns into a
failed read, so no new API plumbing was needed.

Commit: f5dcbf816

Test: DawProjectInterchangeRoundTripTest::malformedNumericAttributesAreRefused

Red/green: red same ctest -> EXIT=8 (`17 passed, 4 failed`, including
`a malformed tempo value "oops" was read as the default`); green after the fix -> EXIT=0. Both gates EXIT=0.

Note: the ten call sites in `src/core/DawProjectReadTracks.cpp` and the five in
`src/core/DawProjectRead.cpp` were updated to pass `state`. This is a behaviour change for
documents that previously carried junk attributes and still parsed: they are now refused
with a named error instead of being silently defaulted. That is the defect being fixed.

### Cluster 4127 — Huge DAWproject times can overflow the tick conversion

Verdict: ALREADY-FIXED

Evidence: the conversion is already bounded on both paths. `ticksFromBeats()` clamps
non-finite and out-of-range beats (`src/core/DawProjectReadShared.h:145-153`, added by commit
`fb9b053d4`), and `dawProjectTicksFromBeats()` guards through `dawProjectBeatRepresentable()`
(`src/core/DawProjectModel.cpp:76-89`), which is exactly the rule the finding asks for. The
two existing regression cases `beatConversionRejectsValuesThatAreNotBeats` and
`importRefusesAnUnrepresentableBeat` cover it and pass.

Commit: none (covered by fb9b053d4)

Test: DawProjectInterchangeRoundTripTest::beatConversionRejectsValuesThatAreNotBeats,
DawProjectInterchangeRoundTripTest::importRefusesAnUnrepresentableBeat

Red/green: none — no change made. `ctest -R '^DawProjectInterchangeRoundTripTest$' --output-on-failure`
-> EXIT=0 on the current tree.

Leftovers: none. All four REAL fixes are committed one per defect (f5dcbf816, 3abcef876,
c72631405, fe95fdf76) and the whole `DawProjectInterchangeRoundTripTest` executable is
green; a suite wider than the tests covering the four changed files was not run, since no
other test target includes `DawProjectRead.cpp`, `DawProjectReadShared.h` or
`DawProjectReadTracks.cpp`.