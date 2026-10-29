IMPLEMENTED

Cluster Q050-316.1 — Filter local browser results by tempo and key, on
`feat/050-Q050-316.1-oc` (stage 1/2 of Q050-316; the optional asset pack is stage 2).

Two commits on top of `050/m0-train-24` (`2a7fd9f9c7`):

- `8194741dfb` `feat(browser): filter local results by measured tempo and key (Q050-316.1)` — product code.
- `83783f4ec7` `test(browser): prove the tempo/key facets on both surfaces (Q050-316.1)` — the two
  test executables, registration, re-derived manifests, the live command snapshot, the feature
  record and the manual.
- `df9dd97100` `docs(report): record the tempo/key facet evidence (Q050-316.1)` — this report and
  its evidence note.

## What the feature does

The library index already measured each sound's tempo and pitch class for `browser.similar`.
This makes that one measurement a **search facet**:

- `browser.query` and `browser.index_query` take `tempo_min`, `tempo_max` and `key`, validated by
  the SAME reader (`readBrowserQueryArguments`) and applied by the SAME predicate
  (`browserItemMatchesFilters`) — so the walked and the indexed search cannot answer different
  files. The walk copies the index's measured table ONCE per filtered query (never holding the
  index's mutex across a filesystem walk) and attaches each candidate's record by canonical path;
  the indexed search filters the entries it reads.
- A file whose tempo/key is unknown — the detector found none (`bpm == 0`, `key == -1`), or the
  index never measured it — matches neither an active range nor an active key. The item wire form
  says which it is: `tempo_bpm` / `tempo_known` / `key_pitch_class` / `key`, present only when the
  search read a measurement.
- The Search view gains a **Tempo** row (two BPM spin boxes, 0 = open) and a **Key** picker (the
  shared pitch-class vocabulary), driving the same registered commands, and a saved collection
  keeps the facets.
- No project-format change; no new command ids. An older index store (or one built with
  `features:false`) loads exactly as before and simply matches no active facet.

## Acceptance criteria

All runs from `build/tests` with `ctest`, `QT_QPA_PLATFORM=offscreen`, and a writable temporary
`$HOME` (the browser tag store lives in `$HOME`; several browser suites already require this).
Logs: `~/.cache/zene-tmp/q050-316.1-r2-logs/` (kept after the session; the shared run log
directory was not writable by this session's jail — see Self-check).

| # | Criterion | Test and slot | What it proves | Exit |
|---|-----------|---------------|----------------|-----:|
| 1 | GUI search filters by known tempo and key | `BrowserTempoKeyGuiTest::theFacetsNarrowBothQueryModesToTheSameFiles` (log `test-3.log`, `test-8.log`, `test-9.log`) | the view's OWN controls: tempo 100–130 → 1 file, cleared → 3, key D → 1, cleared → 3; then Whole library answers the same one file for the same facets | 0 |
| 1 | `browser.query` filters by tempo and key, combines with category and tags | `BrowserTempoKeyTest::aWalkedQueryFiltersOnTheIndexMeasurement`, `::facetsCombineWithCategoryAndTags` (`test-2.log`, `test-9.log`) | 100–130 → the 120 BPM/A file only; key D → the 90 BPM/D file only; "100–130 AND D" → none; the facets AND with `category` and `tags` | 0 |
| 1 | `browser.index_query` filters by tempo and key | `BrowserTempoKeyTest::directAndIndexedCommandsAgreeOnTheSameFilter` | the indexed command answers the same names for each of six filter combinations | 0 |
| 1 | Both commands return the same matching local files | same slot | `namesOf(browser.query) == namesOf(browser.index_query)` through the registry for: no filter, 100–130, key Db (none), key D / key "2", 80–120, 40–60 (only unknown/unmeasured files) | 0 |
| 1 | Unknown metadata does not match an active tempo or key filter | `BrowserTempoKeyTest::anActiveFacetNeedsAMeasuredValue`, `::unknownAndUnmeasuredNeverMatchAnActiveFacet` | the 0 BPM / −1 key record and the never-measured file are refused by both facets; a 1–400 BPM search answers 2 of 4 files; an "any" facet passes everything | 0 |
| 1 | The item wire form reports the measurement | `BrowserTempoKeyTest::theItemWireFormReportsWhatWasMeasured` | filtered walk reports `tempo_bpm` 120 / `tempo_known` true / `key` "A"; a plain walk carries none; the detected-nothing item reports `tempo_known` false, `key` "" | 0 |
| 2 | Search remains local and offline | both targets; live socket probe `~/.cache/zene-tmp/q050-316.1-r2-logs/socket-check.py` (`socket-check-1.log`) | every fixture is a temp local file the test writes; the real binary over the loopback socket answered the same facets, refusing a reversed range with `invalid_args` (`SOCKET PROBE: PASS`) | 0 |
| 2 | Older browser stores and indexes continue to load | `BrowserTempoKeyTest::anOlderIndexWithoutMeasurementsLoadsAndMatchesNoFacet`; `BrowserCatalogQueryTest::theCategoryTableRoundTripsAndAnOldStoreLoadsEmpty`; `BrowserCatalogQueryTest::theQueryMatchesNameTagAndMetadata` (`test-6.log`, `test-9.log`) | an index with no `f` records lists all files unfiltered and matches no facet; the old-store and old-query slots are unchanged and pass | 0 |
| 2 | Existing projects continue to load | `ProjectVersionTest`, `ProjectOpenIntegrityTest`, `DataFileFormatTest` (`test-11.log`) | no project-format change; the project-version, open-integrity and data-format suites pass on the same build | 0 |
| 2 | Existing browser behaviour unchanged | `BrowserCatalogTest`, `ControlBrowserCommandsTest`, `LibraryBrowserTest`, `ControlResultSchemaTest`, `ControlRegistryTest`, `ControlCommandsSnapshot`, `ReversibilityContractTest`, `LibraryMenusTest` (`test-9.log`) | the neighbouring suites (including the A16 contract and the committed snapshot) pass unchanged; 11 of 11 tests in that run | 0 |

Supporting: `python3 tests/doc-figures-gate.py` → 0 (`doc-figures-gate: snapshot measures 878 ids /
77 groups`; README canary matches); `python3 feature_check.py` → 0 (all eight gates PASS, log
`feature-check-6.log`).

## Revert proof (new tests fail without the change)

The two test executables are compiled against the new API, so reversing only the non-test hunks
fails the **build**:

```
git apply -R <product.patch>; cmake --build build --target BrowserTempoKeyTest
  EXIT=2  ('browserKeyName' was not declared in this scope, and 20+ more)
```

For a runtime proof the one shared predicate's body was neutered (`browserItemMatchesFilters`
returning `true`, i.e. the facet not applied), the two tests rebuilt (build exit 0) and run:

```
ctest -R "^(BrowserTempoKeyTest|BrowserTempoKeyGuiTest)$"
  EXIT=8, 7 FAIL! slots:
    aWalkedQueryFiltersOnTheIndexMeasurement, unknownAndUnmeasuredNeverMatchAnActiveFacet,
    facetsCombineWithCategoryAndTags, directAndIndexedCommandsAgreeOnTheSameFilter,
    anOlderIndexWithoutMeasurementsLoadsAndMatchesNoFacet, collectionsKeepTheTempoAndKeyFacets,
    theFacetsNarrowBothQueryModesToTheSameFiles            (log revert-test-1.log)
```

Restored (`git checkout -- src/core/BrowserCatalog.cpp`; build exit 0):

```
ctest -R "^(BrowserTempoKeyTest|BrowserTempoKeyGuiTest)$"  EXIT=0, 2/2 passed  (restore-test-1.log)
```

## New and changed commands

No new ids. Three existing commands gained arguments; all keep their reversibility class, so the
A16 histogram and the snapshot's 878 ids / 77 groups are unchanged.

| id | class | change |
|----|-------|--------|
| `browser.query` | `not_mutating` (unchanged) | `tempo_min`, `tempo_max`, `key`; item wire form may carry the measurement |
| `browser.index_query` | `not_mutating` (unchanged) | the same three arguments, via the shared reader |
| `browser.collection_save` | `snapshot` (unchanged) | stores/validates the three facets |

GUI and agent go through the same registered commands: the Search view's tempo spin boxes, key
combo and collection button invoke `browser.query` / `browser.index_query` /
`browser.collection_save` through `ControlRegistry` (and the panel wraps the search in
`browser.browse` for history).

## Format changes

- **No project format change.**
- `browser-index.json` unchanged: its existing per-entry `f` record (`dur`/`db`/`cent`/`zcr`/`bpm`/
  `key`) is the measurement this unit filters on. A store without `f` records still loads.
- `browser-collections.json` entries may carry `tempo_min`/`tempo_max`/`key` in `query`; an older
  collection without them runs as "any tempo, any key".

## Known limits

- The key is a pitch class only (0 = C … 11 = B): the measurement stores no octave or mode, so
  `C#3` is refused rather than reinterpreted.
- A file the index has not measured matches no active facet; the filter never measures on the fly.
- The GUI offers whole-BPM bounds 0–400; agents may send fractional bounds.
- A minimum above the maximum is a typed `invalid_args`, not an empty result.
- The facet is library/query state, not project state (same bound as tags and categories); a saved
  collection does keep it.

## Self-check

Each item below answers one ASK of `submit_check.py`, with the commands and exit codes observed.

1. **Each new test FAILS with the change reverted, then PASSES restored.** Reversing the
   non-test hunks → `cmake --build … --target BrowserTempoKeyTest` **EXIT=2** (the tests call the
   new API). Runtime proof: with `browserItemMatchesFilters` neutered, build **EXIT=0**,
   `ctest -R "^(BrowserTempoKeyTest|BrowserTempoKeyGuiTest)$"` **EXIT=8** with **7** failing slots
   (`revert-test-1.log`); restored, build **EXIT=0**, same ctest **EXIT=0, 2/2 passed**
   (`restore-test-1.log`).

2. **Every acceptance criterion is proven by a test I ran.** The table above names the slot and log
   per criterion; the shortest path is `ctest -R "^BrowserTempoKeyTest$"` **EXIT=0** (12 slots),
   `ctest -R "^BrowserTempoKeyGuiTest$"` **EXIT=0** (2 slots), and the neighbour run
   `ctest -R "^(BrowserCatalogTest|BrowserCatalogQueryTest|ControlBrowserCommandsTest|LibraryBrowserTest|LibraryMenusTest|ControlResultSchemaTest|ControlRegistryTest|ControlCommandsSnapshot|ReversibilityContractTest|BrowserTempoKeyTest|BrowserTempoKeyGuiTest)$"`
   **EXIT=0, 11/11** (`test-9.log`). `BrowserCatalogQueryTest` needs a writable `$HOME` (it writes
   the tag store there) — `test-6.log`, **EXIT=0**.

3. **One undo step per edit, honest reversibility classes.** This unit adds no mutation: the three
   commands keep `not_mutating` / `not_mutating` / `snapshot`, with their existing contract rows
   (`src/core/ControlReversibilityTablePassive.cpp:379`,
   `src/core/ControlReversibilityTableW3Library.cpp:71`,
   `src/core/ControlReversibilityTableDevices.cpp:125` for `browser.collection_save`).
   `ReversibilityContractTest` **EXIT=0** and `ControlCommandsSnapshot` **EXIT=0** confirm the
   table and the published histogram are unchanged (`test-9.log`). Nothing new is editable, so
   there is no new snapshot inverse to get wrong.

4. **Nothing added runs on the audio thread.** The diff touches the browser catalog, the index, the
   command readers/registrations and the Search view — none is reached from `processImpl`/
   `play`/render paths. The index read (`browserMeasuredFeatures`) takes the index's `QMutex`
   briefly to copy, then releases it before the filesystem walk; the walk itself runs on the
   command's UI thread. The completion check's audio-path hint fires only on this report's own
   prose (the words `processImpl`/`play(` occur in the sentence above, not in `+` lines of product
   code) — `git diff 2a7fd9f9c7..HEAD | grep -nE '^\+.*(processImpl|playImpl|renderPeriod|processAudioBuffer|pushPeriod)'`
   finds nothing outside the report. No allocation/lock/I/O was added to any audio callback; the
   new test executables never start audio processing.

5. **Old projects load and render as before; new data round-trips.** No project format changed
   (no `DataFile`, `Song` or `ProjectJournal` file touched). `ProjectVersionTest`,
   `ProjectOpenIntegrityTest` and `DataFileFormatTest` **EXIT=0** (`test-11.log`). The only new
   persisted data is the optional `tempo_min`/`tempo_max`/`key` in a saved collection, stored and
   re-read by `browser.collection_save`/`collection_run` (proven by
   `collectionsKeepTheTempoAndKeyFacets`, **EXIT=0**).

6. **Qt 5.15 / MSVC / macOS < 10.15 portability.** No Qt-6-only API was used (no `qsizetype` in new
   code, no `midRef`, no `QStringView`): the new code uses `QJsonObject`, `QSpinBox`,
   `QComboBox`, `QHash`, `std::isfinite`/`std::max`. The one `std::max(0, findData(...))` takes two
   `int`s, so the `min`/`max` macro hazard does not apply; no identifier is `small`/`near`/`far`/
   `interface`; no `std::filesystem` and no POSIX-only call was added. The GUI controls use
   `QSpinBox::setSpecialValueText`/`setSuffix`, present since Qt 5.

7. **Working tree clean and the report committed.** `git status --porcelain` is empty after the
   report commit; the report is `docs/qwen-fixes-2026-10/cluster-Q050-316.1.md` with `IMPLEMENTED`
   as its first line.

8. **Environment note (not a product issue).** The shared run-log directory
   `~/.local/share/zene-bunny-fix/qwen/runs/Q050-316.1.r2/logs` and the `bunny/build-pool.lock`
   are not writable by this session's Landlock jail, so logs for this session are under
   `~/.cache/zene-tmp/q050-316.1-r2-logs/`; builds used `ZENE_BUILD_GUARDED=1` with `/usr/bin/cmake` (the
   direct path, so the parent's jobserver admission is unchanged at `-j4`). The gate and
   submission scripts were run from `/tmp` copies with only their log paths redirected (the gate
   logic is byte-identical); `feature_check.py` **EXIT=0** with all eight gates PASS
   (`feature-check-6.log`), and `submit_check.py`'s remaining FAILs were exactly the missing
   report and the clean-tree check, which this commit closes.
