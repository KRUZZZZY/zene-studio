FIXED
new-p9-378: FIXED
5244: FIXED
new-p16-178: FIXED

# Cluster new-p9-378 (same-file batch: new-p9-378, 5244, new-p16-178)

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

## Evidence: RED / GREEN / 4b (r3 re-proof of the committed fix)

Logs: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-378.r3/logs/`. All build commands run from the worktree; test binaries/ctest run from `build/tests`. The product fix is already committed (`b49dac66a7`); the r3 run re-proves it per bug by reversing only that bug's product patch, then re-applying it. Patches: `guard.patch` (only the `edit()` hunk in `src/core/RackZones.cpp`) and `load.patch` (header + `loadSettings` signature/body + the three caller chain-count arguments), both in the same log folder; `git apply -R --check` on both returned `0` before the cycles.

GREEN on the committed tree:

- `cmake --build build --target zene RackZonesTest InstrumentLayerZonePublishTest RackZoneFeedRunTest > 01-build-green.log 2>&1` → `BUILD_EXIT=0` (log 01)
- `ctest -R '^RackZonesTest$|^InstrumentLayerZonePublishTest$|^RackZoneFeedRunTest$' --output-on-failure > 02-test-green-family.log 2>&1` (from `build/tests`) → `EXIT=0` (3/3 passed; log 02)

4b new-p9-378 (guard only):

- RED: `git apply -R guard.patch; echo APPLY_R_EXIT=$?` → `APPLY_R_EXIT=0`; `cmake --build build --target RackZonesTest > 03-build-4b-guard-red.log 2>&1` → `BUILD_EXIT=0`; `./RackZonesTest throwingEditReleasesTheModelChangeGuard projectZoneNamingAMissingChainIsDiscarded invalidFirstZoneDoesNotHideAValidLaterZone > 04-test-4b-guard-red.log 2>&1` → `TEST_EXIT=1`. Guard slot `FAIL! ... '!heldAfterAlongside' returned FALSE. (RackZonesTest.cpp:525)`; both load slots `PASS`. Totals: 4 passed, 1 failed.
- GREEN: `git apply guard.patch; echo REAPPLY_EXIT=$?` → `REAPPLY_EXIT=0`; `cmake --build build --target RackZonesTest > 05-build-4b-guard-green.log 2>&1` → `BUILD_EXIT=0`; `./RackZonesTest throwingEditReleasesTheModelChangeGuard projectZoneNamingAMissingChainIsDiscarded invalidFirstZoneDoesNotHideAValidLaterZone > 06-test-4b-guard-green.log 2>&1` → `TEST_EXIT=0`. Totals: 5 passed, 0 failed.

4b 5244 + new-p16-178 (shared load fix, each slot proved separately):

- RED: `git apply -R load.patch; echo APPLY_R_EXIT=$?` → `APPLY_R_EXIT=0`; `cmake --build build --target RackZonesTest > 07-build-4b-load-red.log 2>&1` → `BUILD_EXIT=0`; `./RackZonesTest throwingEditReleasesTheModelChangeGuard projectZoneNamingAMissingChainIsDiscarded invalidFirstZoneDoesNotHideAValidLaterZone > 08-test-4b-load-red.log 2>&1` → `TEST_EXIT=2`. Guard slot `PASS`; `projectZoneNamingAMissingChainIsDiscarded` `FAIL` (`Actual (rack.zones().zoneCount()): 2 Expected (1)`, `RackZonesTest.cpp:570`); `invalidFirstZoneDoesNotHideAValidLaterZone` `FAIL` (2 vs 1, `:596`). Totals: 3 passed, 2 failed.
- GREEN: `git apply load.patch; echo REAPPLY_EXIT=$?` → `REAPPLY_EXIT=0`; `cmake --build build --target zene RackZonesTest InstrumentLayerZonePublishTest RackZoneFeedRunTest > 09-build-4b-load-green.log 2>&1` → `BUILD_EXIT=0`; `./RackZonesTest throwingEditReleasesTheModelChangeGuard projectZoneNamingAMissingChainIsDiscarded invalidFirstZoneDoesNotHideAValidLaterZone > 10-test-4b-load-green.log 2>&1` → `TEST_EXIT=0` (5 passed, 0 failed); `ctest -R '^RackZonesTest$|^InstrumentLayerZonePublishTest$|^RackZoneFeedRunTest$' --output-on-failure > 11-test-green-family-rerun.log 2>&1` → `CTEST_EXIT=0` (3/3 passed). `git status --short` after re-apply: clean.

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > 12-gates.log 2>&1` → `GATES_EXIT=0`, `RESULT: PASS` (fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity, file-length all PASS; no new complexity/file-length item in the changed files). `submit_check.py` pre-commit (`13-submit-check-precommit.log`, `SUBMIT_EXIT=1`): report `PASS`, sole `FAIL` is `working tree clean` for this uncommitted report edit — cleared by committing it.

## Files committed

- `src/core/RackZones.cpp` — RAII guard in `edit()`; `loadSettings()` validates through `isValidRackZone(zone, chainCount)`.
- `include/RackZones.h` — `loadSettings(..., int chainCount = 64)` declaration.
- `src/core/Rack.cpp`, `src/core/InstrumentLayers.cpp`, `src/core/InstrumentMidiParts.cpp` — pass their chain count.
- `tests/src/core/RackZonesTest.cpp` — 3 regression slots.
- `docs/qwen-fixes-2026-10/cluster-new-p9-378.md` — this report (format/evidence updated in the follow-up commit naming cluster new-p9-378).

## Self-check

### Regression test FAILS without the fix and PASSES with it (exit codes)

- new-p9-378: RED `TEST_EXIT=1` (log 04, guard slot FAIL at `RackZonesTest.cpp:525`) with only `guard.patch` reversed; GREEN `TEST_EXIT=0` (log 06) after re-applying it. Build exits `0` in both (logs 03, 05).
- 5244: RED `TEST_EXIT=2` (log 08, `projectZoneNamingAMissingChainIsDiscarded` FAIL 2 vs 1 at `:570`) with only `load.patch` reversed; GREEN `TEST_EXIT=0` (log 10) after re-applying it. Build exit `0` (logs 07, 09).
- new-p16-178: same RED run (log 08, `invalidFirstZoneDoesNotHideAValidLaterZone` FAIL 2 vs 1 at `:596`, stored first zone is `chain=999` so it hides the valid one) and same GREEN (log 10). Affected-family rerun: `CTEST_EXIT=0`, 3/3 passed (log 11; also log 02 before the cycles).

### Deterministic: no wall-clock timing, sleeps or races

- Guard slot uses the synchronous `LMMS_TESTING` seam `AudioEngine::isChangeInModelHeld()` (`include/AudioEngine.h:618-623`) read immediately after the caught throw; fixed `std::runtime_error` throws; no sleep, no thread, no clock.
- Load slots use fixed XML documents and direct `zoneCount()`/`resolve()` assertions on the control thread. Injected values: `chain="99"` / `chain="999"` against a 2-chain rack.

### Fix is at the cause and complete: every caller and sibling checked

- `edit()`: RAII guard at the single bracketing point; every mutation path (`addZone`, `insertZone`, `removeZone`, `assign`, `clear`, `reindexChains`, `loadSettings` swap) funnels through `edit()`, so all of them get the exception-safe release. Nesting semantics unchanged (recursive mutex, same guard object type used by `Track`/`EffectChain`).
- `loadSettings()`: every `RackZones::loadSettings` caller was checked and passes its owner's chain count — `Rack` (`chainCount()`, `src/core/Rack.cpp:425`, chains already loaded at `:410-417`), `InstrumentLayers` (`layerCount() + 1`, matching `ControlCommandsRackLayers.cpp:297`), `InstrumentMidiParts` (`Channels`, matching `ControlCommandsPluginParts.cpp:166`). The only remaining no-argument caller is the existing test at `tests/src/core/RackZonesTest.cpp:140` (`chain=0`, valid under the default 64). Command-side validation already used `isValidRackZone` (`ControlCommandsRackZones.cpp:138`); loading now uses the same rule. Header and definition changed together; `zene` and all three family test targets rebuilt (logs 01, 09).

### No collateral damage: old projects and saved data still load as before

- Valid saved data loads identically: `isValidRackZone` range rules equal the previous `isValidRange` calls, and the only newly refused zones are those naming a chain the owner does not have (or a negative chain), which could never route. The existing suite's load/round-trip slots pass unchanged (3/3 family, log 11).
- No realtime/undo change: `edit()` still runs entirely under the model-change guard with the same nesting; no new allocation, lock, wait, or log on audio paths; no new control command; no undo step added.

### ALREADY-FIXED / FALSE-POSITIVE code citation

- Not applicable: all three verdicts are FIXED (REAL). Pre-fix evidence: `src/core/RackZones.cpp:96-100` (manual guard bracketing with no exception path), `:254-263` (load checked ranges only, no chain check), `resolve()` first-match-wins (`:143-156`) plus `RackZoneFeed::chainFor()` single-shot reject (`src/core/RackZoneFeed.cpp:76-82`) for the hiding case.

### Registration and gates

- All touched product files are already in `tests/fork-sources.txt`; no new file was created, so no registration change. `feature_check.py` → `GATES_EXIT=0`, `RESULT: PASS` (log 12). Report format: verdict `FIXED` is now line 1 and the three `<cluster>: <VERDICT>` lines follow (lines 2-4), satisfying `submit_check.py`'s first-lines and one-line-per-batch-unit checks (pre-commit log 13: both `PASS`; the only FAIL was the dirty tree for this edit). This section answers each ASK item above.
