FIXED - cluster new-p22-60: rack.layer_set stores a NaN level (a layer's gain becomes NaN)

## Finding and decision

Finding: `rack.layer_set` with `level: NaN` leaves the layer's gain NaN. Verdict **FIXED** (REAL on this branch, `bugs/qwen-new-p22-60` at base `7641e9493`).

Evidence, read in the tree before the change:

- `src/core/ControlCommandsRackLayers.cpp:415` (before the fix): `if (level < 0.0 || level > InstrumentLayers::MaxLevel)`. NaN makes both comparisons false, so NaN passes the range check and the command succeeds.
- `src/core/ControlSchema.cpp:87-100` (`checkRange`): the schema min/max use the same `<` / `>` comparisons, so the schema lets NaN through to the handler too. The registry validates args at `src/core/ControlRegistry.cpp:277`.
- `src/core/InstrumentLayers.cpp:389-390` (`setMix`): `std::clamp(mix.level, 0.0f, MaxLevel)` returns NaN unchanged, so `m_mix[index].level` is stored as NaN and `publishMix` publishes it as the gain.
- `src/core/InstrumentLayers.cpp:134` (`readMix`, the load path): the same clamp, so a saved `layerlevel="nan"` loads as NaN. Confirmed at run time by the RED assertion at `tests/src/core/InstrumentLayerZonePublishTest.cpp:211`.
- Reachability: the JSON wire cannot carry a NaN literal, so the command path is reached in-process (a caller that builds the NaN double, as the test does). I did not verify the Lua or script path that could produce NaN.

## Fix (the cause, plus the two sinks it flows into)

1. `src/core/ControlCommandsRackLayers.cpp:416-417` `layerSetRefusal`: `!std::isfinite(level) ||` is added to the range check. A NaN level is refused as `InvalidArgs`, the same kind an out-of-range level gets. The refusal returns before `addUndoStep`, so a refused call records no undo step.
2. `src/core/InstrumentLayers.cpp:389` `setMix`: a non-finite level returns `false` and the mix is not changed. Every caller ignores the return value (`ControlCommandsRackLayers.cpp:427` `mixWriter`, `:438` `setLayerMix`) and in practice only passes finite values. Header doc updated: `include/InstrumentLayers.h:136`.
3. `src/core/InstrumentLayers.cpp:134` `readMix`: a non-finite `layerlevel` loads as unity, the same as an unparseable one.

Behaviour changes, all limited to non-finite input:
- In-process `setMix` with a NaN level on layer 0 used to store mute/solo with the level forced to 1.0; it now returns `false` and changes nothing. No command reaches this.
- A saved `layerlevel="inf"` used to load clamped to 2.0; it now loads as 1.0. `writeMix` never writes inf.
- Valid saved levels, and every finite level, are unchanged (`0.5` round-trips in the test).

Not changed, recorded as a follow-up: `ControlSchema.cpp` `checkRange` still lets NaN through `minimum`/`maximum` for every other numeric command. Changing it would move every command's behaviour, which is outside this finding. Evidence is the code at `ControlSchema.cpp:87-100`; I did not run it against other commands.

## Regression test

Added to the existing `tests/src/core/InstrumentLayerZonePublishTest.cpp` (no new file, no CMake change):

- `layerLevelRefusesNonFinite`: a NaN `rack.layer_set` is refused as `InvalidArgs`, the layer's level stays as it was, `setMix` refuses a NaN level, and a finite `0.5` still lands.
- `savedLayerLevelNotANumberLoadsAsUnity`: a track is saved and restored through its own path with `layerlevel="0.5"` (loads 0.5) and then `layerlevel="nan"` (loads 1.0, finite).

Why this file: `InstrumentLayersCommandsTest.cpp` is at the file-length ratchet. The gate counted 546 code lines after I added the tests there (the base was under 500), and I moved them. A new test file would need a CMake reconfigure, which this lane's `bunny-guard` blocks ("configuring is the parent's job"). Both tests use a fresh instrument track and are deterministic: no timing, sleeps or races.

Environment note: the instrument fixture skipped in this build (`this build exposes no loadable instrument`), so the layer tests did not run at first. I built the `tripleoscillator` module target (`build-3-instrument-module.log`, EXIT=0); the fixture then loads it. The existing layer tests were skipping for the same reason before my change.

## Commands and exit codes

Build note: ccache could not write its temp dir (`build-1-red.log`, EXIT=2: "Permission denied" under `~/.local/share/zene-bunny-fix/dsh/...`), so every build below uses `CCACHE_DISABLE=1`. Logs are in the run folder `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-60.r1/logs/`.

| Step | Command (run from the worktree, or `build/tests` for ctest) | Log | EXIT |
|---|---|---|---|
| RED, first try | `ctest -R ^InstrumentLayersCommandsTest$` | test-1-red.log | 0 (test skipped: no instrument, not a valid RED) |
| Instrument module | `CCACHE_DISABLE=1 cmake --build build --target tripleoscillator -j4` | build-3-instrument-module.log | 0 |
| Baseline, old file | `./InstrumentLayersCommandsTest` (full) | test-3-full-baseline.log | 0 (5 pass, 11 skip) |
| RED, first version of the test | `./InstrumentLayersCommandsTest layerLevelRefusesNonFinite` (fix applied) | test-2-red-direct.log | 0 (skip) |
| RED, after the instrument module | `./InstrumentLayersCommandsTest layerLevelRefusesNonFinite` (fix applied) | test-4-red-direct.log | 1 (FAIL at test line 468: `!refused.ok` returned FALSE) |
| 4b revert | `git diff -- src/core/ControlCommandsRackLayers.cpp src/core/InstrumentLayers.cpp include/InstrumentLayers.h > logs/fix.patch` ; `git apply -R logs/fix.patch` | fix.patch | 0 ; 0 |
| RED, product reverted (2 tests, old file) | `CCACHE_DISABLE=1 cmake --build build --target InstrumentLayersCommandsTest -j4` ; `./InstrumentLayersCommandsTest` | build-4-red-reverted.log, test-5-red-reverted.log | 0 ; 2 (FAIL at line 470 and line 509) |
| Test moved to `InstrumentLayerZonePublishTest` | `cmake --build build --target InstrumentLayerZonePublishTest -j4` | build-9-red-moved.log | 0 |
| **RED** (product reverted, final test file) | `cd build/tests && ./InstrumentLayerZonePublishTest` | test-9-red-moved.log | **2** (`layerLevelRefusesNonFinite` FAIL at line 169; `savedLayerLevelNotANumberLoadsAsUnity` FAIL at line 211; 5 pass, 2 fail) |
| 4b reapply | `git apply logs/fix.patch` ; `cmake --build build --target zene InstrumentLayerZonePublishTest -j4` ; `./InstrumentLayerZonePublishTest` | build-10-green-moved.log, test-10-green-moved.log | 0 ; 0 ; **0** (7 passed, 0 failed) |
| **GREEN, family rebuild** | `cmake --build build --target zene ReversibilityUndoTest InstrumentLayerZonePublishTest InstrumentLayersCommandsTest RackZonesTest RackMacrosTest RackZoneRoutingTest InstrumentRoutingTest ArticulationPlaybackTest -j4` | build-11-family-final.log | 0 |
| **Family ctest** | `QT_QPA_PLATFORM=offscreen ctest -R '^(InstrumentLayersCommandsTest\|InstrumentLayerZonePublishTest\|ReversibilityUndoTest\|RackZonesTest\|RackMacrosTest\|RackZoneRoutingTest\|InstrumentRoutingTest\|ArticulationPlaybackTest)$'` (from `build/tests`) | test-11-family-final.log | **0** (100% tests passed, 8 out of 8) |
| Gate | `python3 -I logs/gate-driver/feature_check_logdir.py <worktree>` (see note) | gate-3-feature-check.log | **0** (all 8 PASS) |

Gate note: the stock `feature_check.py` writes its logs into `<worktree>/../<name>-feature-check/`, which is outside my write-jail, and failed with `PermissionError` (gate-1-feature-check.log, EXIT=1). I ran the same script, with two paths changed: the log folder points to my log folder, and `ROOT` points at the real `qwen/` directory (the baseline cache). The gate rules, limits and checks are unchanged. The file-length failure in gate-2 (EXIT=1, `InstrumentLayersCommandsTest.cpp` at 546 code lines) was the reason for moving the tests; gate-3 is after the move.

Affected family: 8 ctest tests run, all pass. The layer-test binaries executed 7 (`InstrumentLayerZonePublishTest`) and 16 (`InstrumentLayersCommandsTest`, 1 pre-existing skip for the missing clapinstrument module).

## Self-check

- **Test fails with the product change reverted, passes with it:** yes. RED: `test-9-red-moved.log` EXIT=2 (both new cases fail). GREEN: `test-10-green-moved.log` EXIT=0. Commands are in the table above.
- **Acceptance criterion (the finding): a NaN level is rejected and never stored or loaded as a gain.** Proven by `layerLevelRefusesNonFinite` (command path and `setMix`) and `savedLayerLevelNotANumberLoadsAsUnity` (load path), both GREEN.
- **Deterministic:** no wall-clock timing, sleeps or races. Inputs are a fresh track and a NaN double; the save/restore is in-process.
- **Fix at the cause and complete:** three sinks checked: command refusal (`ControlCommandsRackLayers.cpp:416`), store (`InstrumentLayers.cpp:389`), load (`InstrumentLayers.cpp:134`). The schema hole (`ControlSchema.cpp:87-100`) is recorded as a follow-up and not changed (see above).
- **No collateral damage:** the only valid-data change is none; the limit only refuses non-finite values, which no valid data or command produces. The family ctest (8/8) includes `ReversibilityUndoTest`, which covers `rack.layer_set` mute/solo undo.
- **Undo:** the refusal happens before `addUndoStep`, so a refused call records nothing. The accepted path's undo is unchanged (`ReversibilityUndoTest` passes).
- **Realtime:** no audio-thread code changed. `setMix` and `layerSetRefusal` run on the control thread, and `readMix` on load. The added `std::isfinite` is a branch, with no allocation, lock, log or I/O.
- **Old projects load exactly as before:** finite `layerlevel` values are untouched; the round-trip case (0.5) is tested. Only corrupt NaN/inf input changes (loads as unity).
- **Registration:** no new file; no CMake, fork-sources or all-sources change. Edited inherited files: `include/InstrumentLayers.h`, `src/core/ControlCommandsRackLayers.cpp`, `src/core/InstrumentLayers.cpp`, `tests/src/core/InstrumentLayerZonePublishTest.cpp`. The gate's `no-upstream-regression` row passed.
- **Commands snapshot / canary:** no command was added or its description changed, so the snapshot and canary are unchanged.
- **Sibling commands in the command file:** the file's `rack.layer_*` commands were reviewed. Only `rack.layer_set` takes a `level` argument; `rack.layers` reads it and `layerSetRefusal`/`mixArgs` are its only consumers. The other siblings (`rack.layer_add`, `rack.layer_remove`, `rack.layer_zone_*`) do not touch the level and are unaffected.
- **Gates:** `gate-3-feature-check.log` EXIT=0 (see the gate note for why I ran it through a log-redirected copy).
- **Not verified:** the Lua or script path that could produce a NaN (only the in-process call is tested); the other numeric commands that share `checkRange`; the full `ctest` tree (only the family, 8 tests, and the two layer binaries). The clapinstrument-dependent multi-output test is skipped in this build and stays skipped.
