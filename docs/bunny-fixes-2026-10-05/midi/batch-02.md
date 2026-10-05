# MIDI lane — batch 02 (2026-10-05)

Six findings, five distinct defects. Two were already fixed by batch-01 on this
branch and one was a second report of a defect fixed here.

### Cluster 558 — An inverted key range is accepted
Verdict: ALREADY-FIXED
Evidence: `src/core/MidiInputTransform.cpp:192` checks the two ends of the range
against each other before anything is stored, and the regression case
`MidiInputTransformTest::anInvertedKeyRangeIsRefused` is in the tree. The
defect was fixed on this branch by `f05430161` (batch-01), which is why the
report's line 157 no longer points at anything unfixed.
Commit: f05430161 (batch-01, not this batch)
Test: MidiInputTransformTest::anInvertedKeyRangeIsRefused
Red/green: none — no code touched in this batch.

### Cluster 4217 — Fractional transform settings are silently truncated
Verdict: ALREADY-FIXED
Evidence: `src/core/MidiInputTransform.cpp:180` compares each value as the
double it is and refuses one that is not a whole number
(`number != std::trunc(number)`), so `transpose: 1.9` is refused rather than
stored as 1. Fixed on this branch by `f02abced5` (batch-01); the regression case
`aFractionalSettingIsRefused` is in the tree.
Commit: f02abced5 (batch-01, not this batch)
Test: MidiInputTransformTest::aFractionalSettingIsRefused
Red/green: none — no code touched in this batch.

### Cluster 5144 — Invalid JSON crashes when the error pointer is null
Verdict: FIXED
Evidence: `src/core/MidiInputTransform.cpp:183` and `:195` both reported a
refusal by writing through the `error` output pointer with no null check, and
the header never said the pointer had to be non-null, so
`fromJson({...}, nullptr)` dereferenced null on the error path. Both writes now
go through one `refuse()` helper (`src/core/MidiInputTransform.cpp:54`), and
the header states that `error` may be null
(`include/MidiInputTransform.h:61`). The helper is also what keeps `fromJson`
inside the CCN target: two inline `if (error != nullptr)` guards took it to
CCN 12, which the complexity gate rejected.
Commit: ccd3e7bd5
Test: MidiInputTransformTest::aRefusalIsRefusedWithoutAnErrorPointer
Red/green: `cmake --build build --target zene MidiInputTransformTest` EXIT=0;
`cd build/tests && ctest -R '^MidiInputTransformTest$' --output-on-failure`
EXIT=8 (Received signal 11 SIGSEGV in
`aRefusalIsRefusedWithoutAnErrorPointer()`) → EXIT=0 (10/10 cases) after the fix.
Gates: complexity-gate.sh --check EXIT=0, file-length-gate.sh --check EXIT=0.

### Cluster 3278 — Malformed project data resets the existing transform
Verdict: FIXED
Evidence: `src/core/MidiInputTransform.cpp:227` called `reset()` and only then
parsed the `inputtransform` attribute, discarding the refusal from
`fromJson()`. The attribute carries the WHOLE transform, so any attribute that
would not parse or would not validate left the track at the identity — the
user's settings gone, with nothing in the journal to undo. `load()` now parses
and validates into a candidate transform and copies it over only once the
attribute is known to be a transform (`src/core/MidiInputTransform.cpp:231-244`).
The regression covers all three malformed shapes: out of range, not a number,
and not JSON at all.
Commit: 2b59db5e1
Test: MidiInputTransformTest::aMalformedSavedTransformKeepsTheOneInForce
Red/green: `cmake --build build --target zene MidiInputTransformTest` EXIT=0;
`ctest -R '^MidiInputTransformTest$'` EXIT=8 (FAIL!
`aMalformedSavedTransformKeepsTheOneInForce()` — Compared values are not the
same) → EXIT=0 (11/11 cases) after the fix.
Gates: complexity-gate.sh --check EXIT=0, file-length-gate.sh --check EXIT=0.

### Cluster 4221 — Invalid saved transform data is discarded without notice
Verdict: DUPLICATE
Evidence: Same defect and same line as cluster 3278 — `load()` reset the
transform, then ignored `fromJson()` failures, so a saved `transpose: 49` lost
the transform silently. This report's example is precisely the
`load()`-with-`{"transpose": 49}` case the new regression case exercises
first. Fixed once, as 3278.
Commit: 2b59db5e1 (the same commit)
Test: MidiInputTransformTest::aMalformedSavedTransformKeepsTheOneInForce
Red/green: as cluster 3278.

### Cluster 1713 — Long SMF event times produce invalid or wrapped delta times
Verdict: FIXED
Evidence: Both halves of the report held. `appendVlq`
(`src/core/SmfInterchange.cpp:89`) can emit five bytes while an SMF delta time
is a variable-length quantity of at most four (`0x0FFFFFFF` file ticks =
26,843,545 LMMS ticks at `SmfTicksPerLmmsTick` = 10), and
`trackBody` narrowed the scaled tick to `quint32` even though `tick_t` is
`std::int32_t` (`include/LmmsTypes.h:35`) — a tick above 429,496,729 times ten
and wrapped, taking every delta after the wrap with it. The walk is now 64-bit
throughout (`src/core/SmfInterchange.cpp:234-268`) and narrows only inside the
new `appendDelta`, after the delta itself has been checked; the limit is the
named constant `SmfMaxDeltaTicks` (`include/SmfInterchange.h:122`). The
regression also pins the other side of the boundary: the widest gap the format
CAN carry is still written and parses.
Commit: a2426a910
Test: SmfInterchangeTest::aGapTooWideForTheFormatIsRefused
Red/green: `cmake --build build --target zene SmfInterchangeTest` EXIT=0;
`ctest -R '^SmfInterchangeTest$'` EXIT=8 (FAIL! `writeConductorTrack(...)`
returned FALSE — the unrepresentable export was not refused) → `ctest -R
'^SmfInterchange'` EXIT=0 (2/2) after the fix, plus
`ctest -R '^CommandPaletteTest$'` EXIT=0 (the group these commands register
into).
Gates: complexity-gate.sh --check EXIT=0, file-length-gate.sh --check EXIT=0.

Leftovers: nothing half-done. One thing seen while reading the code and NOT
changed, because it is unobservable and out of this finding's scope:
`trackBody` used to write the FIRST event's delta as an unscaled LMMS tick
while every later delta was scaled by ten. It is unobservable because
`exportEvents` always leaves an event at tick 0 at the front of the list (the
tick-0 seed is either merged into the front event or inserted), so that first
delta is always 0. The new code scales it consistently, which is why the
written bytes are unchanged for every file this module has ever written.
