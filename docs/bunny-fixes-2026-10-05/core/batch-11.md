# Bunny core lane — batch 11

Six findings, all checked by hand against the current tree before any code was
touched. Five were real and are fixed with a regression test that failed first;
one is real but cannot be proved headlessly, so no product code was changed.

### Cluster 5493 — Failed full-track bounce can destroy an existing output

Verdict: REAL-UNTESTED

Evidence: `src/core/BounceInPlace.cpp:347` sets `renderTarget = outPath` for a
whole-track render, so the child renderer opens the caller's destination
directly; `renderFailed` reports failure on a nonzero exit or a timeout without
removing what was written. No caller guards it — `ControlCommandsRangeBounce.cpp:101`,
`ControlCommandsFreeze.cpp:142,183`, `ControlCommandsTrackFlatten.cpp:90` and
`ControlCommandsBounceLand.cpp:68` all pass their output path straight through.
The defect is real by inspection, but the renderer is the application binary
spawned as a child via `QCoreApplication::applicationFilePath()`, so a
deterministic failing test needs a renderer-injection seam that is an
out-of-scope refactor for this lane. Left the code alone.

Commit: none
Test: none
Red/green: none

### Cluster 3735 — Trimming a WAV onto itself truncates the source

Verdict: FIXED

Evidence: `trimWav` opened the source with `SFM_READ` and the target with
`SFM_WRITE` and never compared the two, so `trimWav(path, path, ...)` truncated
the source before a frame was copied. `namesTheSameFile` at
`src/core/BounceInPlace.cpp:372` now compares the canonical path (falling back
to the absolute one for a target that does not exist yet), and `trimWav`
refuses at line 427 with a reason. `openTrimWindow` at line 391 carries the
open/clamp/seek sequence.

Commit: 14cd371ca
Test: BounceInPlaceTest::trimWavRefusesToWriteOntoItsOwnSource,
BounceInPlaceTest::trimWavRefusesASymlinkThatNamesTheSource
Red: `cmake --build build --target zene BounceInPlaceTest` then
`cd build/tests && ctest -R '^BounceInPlaceTest$' --output-on-failure` → EXIT=1
(the refusal carried no reason)
Green: `cd build/tests && ctest -R '^BounceInPlaceTest$' --output-on-failure` → EXIT=0

### Cluster 5495 — Browser query scan counts omit files rejected by tag filters

Verdict: FIXED

Evidence: `++context->scanned` sat after the `tagsMatch` early return in
`considerPath`, while `include/BrowserCatalog.h:193` documents `scanned` as the
files examined after the kind filter and before text filtering. A query with a
tag filter over ten matching-kind files reported 1. The counter is now
incremented before the tag filter — the only filter that precedes it.

Commit: bcb7dd5d2
Test: BrowserCatalogTest::scanCountCoversWhatTheTagFilterRejected
Red: `cd build/tests && ctest -R '^BrowserCatalogTest$' --output-on-failure` → EXIT=1
(scanned 1, expected 2)
Green: same command → EXIT=0

### Cluster 919 — A metadata-aware exclusion can include the item it should exclude

Verdict: FIXED

Evidence: `considerPath` appended a cheap match before probing, and
`BrowserSearch::termHolds` (`src/core/BrowserSearch.cpp:122`) returns true for a
negated term that is absent from the cheap text — which is exactly what an
unfielded exclusion asks for. So `kick -drum` accepted `kick_48k.wav`, whose
embedded title is "Kick Drum". `metadataCanOverturn`
(`src/core/BrowserCatalog.cpp:207`) consults the new
`BrowserSearch::hasUnfieldedExclusion()` (`src/core/BrowserSearch.cpp:130`), and a
cheap match is re-probed through `probeAndAppend` when metadata could overturn
the answer. A probe that learns nothing leaves the cheap match standing; a valid
probe that still excludes drops the row.

Commit: f8d551dc1
Test: BrowserCatalogTest::metadataCanStillExcludeACheapMatch
Red: `cd build/tests && ctest -R '^BrowserCatalogTest$' --output-on-failure` → EXIT=1
(probed 0, expected 1)
Green: same command → EXIT=0

### Cluster 3738 — Metadata probes can be spent on non-audio files

Verdict: FIXED

Evidence: every unmatched candidate was opened with `probeAudioMetadata`
regardless of its `BrowserKind`, so a library of presets and projects could
exhaust `BrowserMaxProbes` (`include/BrowserCatalog.h:164`) before the audio
candidates were reached. `considerPath` now returns before the probe for any
kind but `BrowserKind::Audio` (`src/core/BrowserCatalog.cpp:234`). The matched
set is unchanged: libsndfile cannot read a preset, project or MIDI file, so
those probes only ever returned invalid metadata and never matched.

Commit: a24df21b5
Test: BrowserCatalogTest::probesAreSpentOnAudioCandidatesOnly
Red: `cd build/tests && ctest -R '^BrowserCatalogTest$' --output-on-failure` → EXIT=1
(probed 2, expected 1)
Green: same command → EXIT=0

### Cluster 2689 — The browser query's scan limit does not bound directory traversal

Verdict: FIXED

Evidence: `filesUnder` built each directory with `QDir::entryInfoList()`, which
materialises a whole listing before the limit applies to any of it, and
decremented the budget once per FILE. `include/BrowserCatalog.h:158-161`
documents `BrowserMaxScanEntries` as bounding the ENTRIES examined, so a tree of
empty directories or one enormous directory was unbounded in time and memory
while the result still reported `truncated: false`. The walk now streams with
`QDirIterator` through `readDirectory` (`src/core/BrowserCatalog.cpp:258`,
descend rule factored to `walkMayDescend` at line 241) and spends one unit of
budget per entry visited, directories included; running out mid-directory sets
`stoppedEarly` so `truncated` is reported honestly. Ordering within a directory
becomes filesystem order, which `runBrowserQuery`'s path sort absorbs.

Commit: 1cbf45367
Test: BrowserCatalogTest::scanLimitBoundsTheWalkOverDirectories
Red: `cd build/tests && ctest -R '^BrowserCatalogTest$' --output-on-failure` → EXIT=1
(`truncated` was false)
Green: same command → EXIT=0

## Verification

`cmake --build build --target zene BrowserCatalogTest BounceInPlaceTest
ProjectToolsTest ControlBrowserCommandsTest LibraryBrowserTest BrowserTagMenuTest`
→ EXIT=0, then
`cd build/tests && ctest -R '^(BrowserCatalogTest|BounceInPlaceTest|ProjectToolsTest|ControlBrowserCommandsTest|LibraryBrowserTest|BrowserTagMenuTest)$' --output-on-failure`
→ EXIT=0, "100% tests passed, 0 tests failed out of 6".

`bash tests/complexity-gate.sh --check` → EXIT=0 (the first pass failed: `trimWav`
and `filesUnder` had reached CCN 11, and each was split into a helper rather
than the ratchet moved).
`bash tests/file-length-gate.sh --check` → EXIT=0.

Leftovers: cluster 5493 is still unfixed in code — the render-to-temp-then-
replace change is real work but needs a renderer-injection seam to be testable,
so it is left for a lane that can afford that refactor. No other finding is
half-done.
## Rework correction (2026-10-08)

Source: `58887e4f9` (REWORK in `review-core.md`, item 7; it reports on `f8d551dc1`).

- Cluster 919 is narrowed: the probe fixed the audio case, but with `probe=true` and an unfielded
  exclusion `considerPath` sent every cheap match to the audio-only probe path, and a preset or project
  match returned from there without being appended. Presets and projects were silently dropped. Fixed in
  `7801d192c` (`fix(rework f8d551dc1)`), test `BrowserCatalogTest::probeKeepsCheapMatchesOfPresetsAndProjects`
  (red at HEAD: matched 0, expected 2).
- Cluster 3738 (`a24df21b5`): the statement that the matched set is unchanged is only true after `7801d192c`.
  Before it, the probe-on path could lose non-audio matches.
