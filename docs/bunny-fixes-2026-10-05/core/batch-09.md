# Core batch-09 — 2026-10-05

Lane `core`, branch `bugs/bunny-core-2026-10-05`. Six findings: 4 FIXED, 1 SKIPPED-SCOPE,
1 already committed in this batch's earlier steps is folded in below (all four fixes are on the
branch; shas are short).

### Cluster 3671 — Edge snapping can overflow while measuring distance

Verdict: FIXED

Evidence: `src/core/ArrangementEdits.cpp:350-355` measured the snap distance and the tolerance in
32-bit `tick_t` (`tick_t` is `std::int32_t`, `include/LmmsTypes.h:35`), so `candidate - proposed`
could overflow and `tolerance + 1` wrapped negative at the maximum tolerance. The fix measures both
in `std::int64_t` and rejects a negative candidate before the subtraction.

Commit: 07895d855

Test: `ArrangementModesTest::aMaximumToleranceStillSnaps`

Red/green: build EXIT=0, `ctest -R '^ArrangementModesTest$'` EXIT=8 (actual 190, expected 192;
15 passed, 1 failed) → build EXIT=0, same ctest EXIT=0 (16 passed).

### Cluster 905 — Replacing a section accepts invalid spans

Verdict: FIXED

Evidence: `src/core/ArrangementSections.cpp:66-75` — `replace()` stored any span it was handed,
unlike `add()`, so `start=100, end=50` was accepted and could then be saved. It now applies the same
guard as `add()`: `start < 0 || end <= start` returns false.

Commit: 8b0f0905d

Test: `ArrangeToolsTest::aSectionReplacementRefusesAnInvalidSpan`

Red/green: build EXIT=0, `ctest -R '^ArrangeToolsTest$'` EXIT=8 (12 passed, 1 failed) → build
EXIT=0, same ctest EXIT=0 (13 passed).

### Cluster 5484 — Invalid section start values are treated as tick zero

Verdict: FIXED

Evidence: `src/core/ArrangementSections.cpp:122` — `QString::toInt()` returns zero for a missing or
nonnumeric `start`/`end`, so `<section start="oops" end="192"/>` loaded as a section from tick 0.
`sectionOf()` now takes an out-parameter and reports whether both attributes converted, and `load()`
skips elements that fail.

Commit: b9d708209

Test: `ArrangeToolsTest::aMalformedSectionSpanIsNotReadAsTickZero`

Red/green: build EXIT=0, `ctest -R '^ArrangeToolsTest$'` EXIT=8 (size 3, expected 1; 13 passed,
1 failed) → build EXIT=0, same ctest EXIT=0 (14 passed).

### Cluster 5483 — Malformed articulation JSON silently clears the map

Verdict: FIXED

Evidence: `src/core/ArticulationMap.cpp:114` — `load()` passed `QJsonDocument::fromJson(...).array()`
to `fromJson()`, so a parse error and valid JSON with an object root both became an empty array that
`fromJson` accepted, clearing the map without a word. `load()` now checks `QJsonParseError` and
`document.isArray()`, warns and clears; an absent or empty attribute still loads as "none" quietly.

Commit: 5bacd2203

Test: `NotePlaybackTest::aMalformedArticulationJsonWarnsAndLoadsWithNone`

Red/green: build EXIT=0, `ctest -R '^NotePlaybackTest$'` EXIT=8 (no warning captured; 10 passed,
1 failed) → build EXIT=0, same ctest EXIT=0 (11 passed).

### Cluster 5482 — Invalid articulation data leaves a valid prefix loaded

Verdict: FIXED

Evidence: `src/core/ArticulationMap.cpp:114` — `fromJson()` pushes the entries it accepted before
the bad one and returns false, and `load()` moved that partial vector into the map after warning
that the track loads with none. A valid prefix therefore stayed active and saved back as if it were
the complete map. `load()` now clears the vector on failure.

Commit: e2107a837

Test: `NotePlaybackTest::anInvalidArticulationLeavesNoPrefixLoaded`

Red/green: build EXIT=0, `ctest -R '^NotePlaybackTest$'` EXIT=8 (map not empty; 11 passed,
1 failed) → build EXIT=0, same ctest EXIT=0 (12 passed).

### Cluster 2647 — Reading the new automation ramp still takes a mutex

Verdict: SKIPPED-SCOPE

Evidence: `src/core/AutomatableModel.cpp:554` — `valueBuffer()` does take `m_valueBufferMutex`
(declared `include/AutomatableModel.h:579`) before the new ramp check, so the finding is real. Making
the ramp read path lock-free requires changing the ramp's storage and publication so it cannot be
mutated while the audio thread reads it — an audio-thread storage/concurrency redesign that the
realtime rule (no locking on the render path) makes a larger change than this defect, and it is not
deterministically testable headlessly (the blocking requires a second thread holding the mutex
across a render period).

Commit: none

Test: none

Red/green: none.

Leftovers: cluster 2647 is left for a change that redesigns the automation ramp's storage; the
articulation `load()` still warns only via qWarning (no control-visible error surface was added, to
keep the control-command schema untouched), and `docs/bunny-fixes-2026-10-05/core/batch-09.md` is
the only file added on top of the four fix commits.
