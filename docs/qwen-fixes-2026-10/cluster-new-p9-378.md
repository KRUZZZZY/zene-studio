# Cluster new-p9-378 (same-file batch: new-p9-378, 5244, new-p16-178)

new-p9-378: FIXED
5244: FIXED
new-p16-178: FIXED

## Verdicts

- `new-p9-378: FIXED` — REAL. `RackZones::edit()` (`src/core/RackZones.cpp:91-101` pre-fix) called `requestChangeInModel()`, ran `change()` and `m_changed()`, and only then called `doneChangeInModel()`. If `change()` (e.g. `std::vector` `push_back` out of memory) or `m_changed()` threw, the guard leaked and audio workers could stay blocked on `m_changeMutex`. Fixed with the tree's existing RAII guard (`AudioEngine::requestChangesGuard()`), see below.
- `5244: FIXED` — REAL. `RackZones::loadSettings()` validated key/velocity ranges with `isValidRange()` but never checked `zone.chain` against the rack's chain count (it did not even take a chain count), so a `<zone chain="99">` with a 2-chain rack was stored and `resolve()` could hand a note to chain 99. Partial credit: ranges were already checked (`src/core/RackZones.cpp:255-260` pre-fix); only the chain was missing. Fixed by validating every parsed zone through `isValidRackZone(zone, chainCount)`.
- `new-p16-178: FIXED` — REAL, same root cause as 5244. `loadSettings()` read the chain index but never compared it with the rack's chain count, so an overlapping zone on chain 999 stored before a valid zone on chain 1 won `resolve()` (first-match-wins) and `RackZoneFeed::chainFor()` (`src/core/RackZoneFeed.cpp:73-83`) rejected its unrouted chain, so the note took the common path and the later valid zone was never considered. Fixed by the same discard: invalid-chain zones are never stored, so they cannot hide later ones.

## Changes

- `src/core/RackZones.cpp` — `edit()`: replaced manual `requestChangeInModel()` / `doneChangeInModel()` bracketing with `const auto guard = engine != nullptr ? engine->requestChangesGuard() : AudioEngine::RequestChangesGuard{};` (the idiom used across the tree, e.g. `src/core/Track.cpp`, `src/core/EffectChain.cpp`). Order preserved (`request`, `change()`, `m_changed()`, `done` in the destructor); the destructor releases the guard on exception unwinding, so a throwing mutation or hook cannot leave it held. Nesting unchanged (`m_changeMutex` is recursive).
- `src/core/RackZones.cpp` + `include/RackZones.h` — `loadSettings()` now takes `int chainCount = 64` and discards a parsed zone unless `isValidRackZone(zone, chainCount)`. That shared validator checks the key range, the velocity range, and `0 <= chain < chainCount`. The duplicated range checks replaced by the single call keep identical range semantics. The default 64 is the feed's routing limit (`RackZoneFeed` refuses chains `>= 64` anyway); owners with fewer chains always pass their own count.
- `src/core/Rack.cpp:425` — `m_zones.loadSettings(rack, chainCount());` (chains are already loaded by then, so the count is final).
- `src/core/InstrumentLayers.cpp:378` — `m_zones.loadSettings(element, layerCount() + 1);` (sibling owner of the same list, validated with the same `isValidRackZone` rule the layer command uses).
- `src/core/InstrumentMidiParts.cpp:101` — `m_zones.loadSettings(..., Channels);` (16 MIDI channels).
- `tests/src/core/RackZonesTest.cpp` — 3 new slots (details below).

No new control command, no undo change, no realtime-path change (all edits stay under the existing model-change guard; loading stays on the control thread).

## Tests

- `throwingEditReleasesTheModelChangeGuard()` (new-p9-378): two throwing phases against standalone `RackZones` — an `alongside` step that throws after the mutation, and a changed hook that throws after the mutation — plus a normal add. Each phase catches the exception and reads the `LMMS_TESTING` seam `AudioEngine::isChangeInModelHeld()` (`include/AudioEngine.h:618-623`, maintained in `src/core/AudioEngine.cpp:1697-1719`). Deterministic: no timing, no sleep. If the guard leaked, the test releases it with `doneChangeInModel()` before failing so later slots are not poisoned.
- `projectZoneNamingAMissingChainIsDiscarded()` (5244): loads a 2-chain rack through `Rack::loadSettings()` with zones `chain="99"` (keys 0–127) and `chain="1"` (keys 60–72); expects `zoneCount()==1`, `zone(0)->chain==1`, `resolve(65,100)==0`.
- `invalidFirstZoneDoesNotHideAValidLaterZone()` (new-p16-178): loads a 2-chain rack with two overlapping zones (keys 36–60), first `chain="999"`, then `chain="1"`; expects `zoneCount()==1`, `resolve(40,100)==0` with `chain==1`.

## Evidence: RED / GREEN / 4b

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-378.r1/logs/`. All commands run from the worktree unless noted (`build/tests` for ctest).

Initial RED (tests added, no product fix), `EXIT=8`:

- `cmake --build build --target RackZonesTest > .../logs/build-red-tests.log 2>&1; echo EXIT=$?` → `EXIT=0`
- `ctest -R '^RackZonesTest$' --output-on-failure > .../logs/test-red-all.log 2>&1` (from `build/tests`) → `EXIT=8`
  - `FAIL! ... throwingEditReleasesTheModelChangeGuard() '!heldAfterAlongside' returned FALSE. (edit left the model-change guard held after the alongside step threw)` (`RackZonesTest.cpp:525`)
  - `FAIL! ... projectZoneNamingAMissingChainIsDiscarded() Actual (rack.zones().zoneCount()): 2 Expected (1)` (`:570`)
  - `FAIL! ... invalidFirstZoneDoesNotHideAValidLaterZone() Actual (rack.zones().zoneCount()): 2 Expected (1)` (`:596`)
  - 9 passed, 3 failed.

Guard GREEN (only the `edit()` fix), `EXIT=8` overall (load still unfixed, as intended):

- `cmake --build build --target RackZonesTest > .../logs/build-green-guard.log 2>&1` → `EXIT=0`
- `./RackZonesTest throwingEditReleasesTheModelChangeGuard projectZoneNamingAMissingChainIsDiscarded invalidFirstZoneDoesNotHideAValidLaterZone > .../logs/test-green-guard.log 2>&1` → `EXIT=8`; guard slot `PASS`, both load slots still `FAIL` (2 vs 1). The full `ctest -R '^RackZonesTest$'` run is in the same log: 10 passed, 2 failed.

Load GREEN (all fixes), `EXIT=0`:

- `cmake --build build --target zene > .../logs/build-green-load.log 2>&1` → `EXIT=0`
- `cmake --build build --target RackZonesTest InstrumentLayerZonePublishTest RackZoneFeedRunTest > .../logs/build-tests-green-load.log 2>&1` → `EXIT=0`
- `ctest -R '^RackZonesTest$|^InstrumentLayerZonePublishTest$|^RackZoneFeedRunTest$' --output-on-failure > .../logs/test-green-load.log 2>&1` (from `build/tests`) → `EXIT=0` (3/3 passed)
- `./RackZonesTest > .../logs/test-green-verbose.log 2>&1` → `EXIT=0`; `Totals: 12 passed, 0 failed`.

4b (per-bug isolation; patches split by hunk so each reversal touches only that bug's product code):

- new-p9-378. `git diff -- src/core/RackZones.cpp > .../logs/combined-rackzones.patch`; split hunks into `.../logs/guard.patch` (only the `edit()` hunk) and `.../logs/load.patch` (remaining hunks + `include/RackZones.h`, `src/core/Rack.cpp`, `src/core/InstrumentLayers.cpp`, `src/core/InstrumentMidiParts.cpp`). `git apply -R --check` both → `0`.
  - RED: `git apply -R .../logs/guard.patch; echo APPLY_R_EXIT=$?` (0), `cmake --build build --target RackZonesTest > .../logs/build-4b-guard-red.log 2>&1` (0), `./RackZonesTest throwingEditReleasesTheModelChangeGuard projectZoneNamingAMissingChainIsDiscarded invalidFirstZoneDoesNotHideAValidLaterZone > .../logs/test-4b-guard-red.log 2>&1` → `EXIT=1`; guard slot `FAIL` (guard held), load slots `PASS`. Totals: 4 passed, 1 failed.
  - GREEN: `git apply .../logs/guard.patch; echo REAPPLY_EXIT=$?` (0), build (0), `./RackZonesTest ... > .../logs/test-4b-guard-green.log 2>&1` → `EXIT=0`; `Totals: 5 passed, 0 failed`.
- 5244 and new-p16-178 share one fix (one `load.patch`); each test is proved against it separately.
  - RED: `git apply -R .../logs/load.patch; echo APPLY_R_EXIT=$?` (0), build (0), `./RackZonesTest throwingEditReleasesTheModelChangeGuard projectZoneNamingAMissingChainIsDiscarded invalidFirstZoneDoesNotHideAValidLaterZone > .../logs/test-4b-5244-red.log 2>&1` → `EXIT=2`; guard slot `PASS`, `projectZoneNamingAMissingChainIsDiscarded` `FAIL` (zoneCount 2 vs 1, `:570`), `invalidFirstZoneDoesNotHideAValidLaterZone` `FAIL` (zoneCount 2 vs 1, `:596`). Totals: 3 passed, 2 failed. The new-p16-178 RED is in the same run, against the two overlapping zones: the stored first zone is `chain=999`.
  - GREEN: `git apply .../logs/load.patch; echo REAPPLY_EXIT=$?` (0), build (0), `./RackZonesTest ... > .../logs/test-4b-load-green.log 2>&1` → `EXIT=0`; all three slots `PASS`. Totals: 5 passed, 0 failed.

Sibling rebuild (header changed):

- `cmake --build build --target RackZoneRoutingTest RackMacrosTest InstrumentRoutingTest InstrumentSetupUiTest MidiInputTransformTest > .../logs/build-siblings.log 2>&1` → `EXIT=0`
- `ctest -R '^RackZoneRoutingTest$|^RackMacrosTest$|^InstrumentRoutingTest$' > .../logs/test-siblings.log 2>&1` → `EXIT=0`
- `ctest -R '^InstrumentSetupUiTest$|^MidiInputTransformTest$' > .../logs/test-siblings2.log 2>&1` → `EXIT=0`

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/gates.log 2>&1` → `EXIT=0`, `RESULT: PASS` (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS; no new complexity/file-length item in the changed files).

## Files committed

- `src/core/RackZones.cpp` — RAII guard in `edit()`; `loadSettings()` validates through `isValidRackZone(zone, chainCount)`.
- `include/RackZones.h` — `loadSettings(..., int chainCount = 64)` declaration.
- `src/core/Rack.cpp`, `src/core/InstrumentLayers.cpp`, `src/core/InstrumentMidiParts.cpp` — pass their chain count.
- `tests/src/core/RackZonesTest.cpp` — 3 regression slots.
- `docs/qwen-fixes-2026-10/cluster-new-p9-378.md` — this report.

## Self-check

### Decisions with evidence

- `new-p9-378` REAL: pre-fix `src/core/RackZones.cpp:96-100` bracket points. The RAII fix matches the tree's guard idiom (`RequestChangesGuard`, `include/AudioEngine.h:100-151`, `requestChangesGuard()` at `:603-606`).
- `5244` REAL: pre-fix `loadSettings()` (`src/core/RackZones.cpp:254-263`) checked ranges only; no chain check anywhere on the load path (the command path does check via `isValidRackZone`, `src/core/ControlCommandsRackZones.cpp:138`). Chain discarded only now.
- `new-p16-178` REAL: pre-fix `resolve()` first-match-wins (`src/core/RackZones.cpp:143-156`) plus `chainFor()` single-shot reject (`src/core/RackZoneFeed.cpp:76-82`) hides a later valid zone; discarding at load removes the hiding.

### Regression proof

- Each new slot FAILS without its fix and PASSES with it: initial RED (see table), guard-only GREEN, load GREEN, and both isolated 4b reversals re-proved. No wall-clock, sleeps, or races: the guard test reads the synchronous `isChangeInModelHeld()` seam; the load tests use fixed XML and direct `resolve()`/counts.

### Cause, completeness, collateral

- Single-pass load validation: the invalid zone is never swapped into `m_zones`, so workers never see it (stronger than post-load filtering).
- Callers checked: every `RackZones::loadSettings` caller (`Rack`, `InstrumentLayers`, `InstrumentMidiParts`) and every `addZone` side (commands validate through `isValidRackZone` before calling; `addZone` itself is unchanged and behavior outside the bugs is identical — valid saved data loads byte-for-byte the same).
- Ranges: old projects with only key/velocity ranges load exactly as before (`isValidRackZone` range rules equal the previous `isValidRange` calls). A zone with a negative chain is now also discarded (previously stored; it could never route).
- Realtime/undo: `edit()` still runs entirely under the model-change guard with the same nesting; no new allocation, lock, wait, or log on audio paths; no new control command.

### Registration

- All touched product files are already in `tests/fork-sources.txt`; `feature_check.py` passes (no new file, no new complexity item), so no registration change is needed.
