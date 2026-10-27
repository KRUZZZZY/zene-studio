FIXED - cluster new-p22-60: rack.layer_set stores a NaN level (a layer's gain becomes NaN)

## Finding and decision

Finding: `rack.layer_set` with `level: NaN` leaves the layer's gain NaN. Verdict **FIXED**: REAL on this branch (`bugs/qwen-new-p22-60`, base `7641e9493`). Not ALREADY-FIXED and not FALSE-POSITIVE.

Evidence, read in the tree before the change (pre-fix line numbers):

- `src/core/ControlCommandsRackLayers.cpp:415`: `if (level < 0.0 || level > InstrumentLayers::MaxLevel)`. NaN makes both comparisons false, so NaN passes the range check and the command succeeds.
- `src/core/ControlSchema.cpp:87-100` (`checkRange`): the schema min/max use the same `<` / `>` comparisons, so the schema lets NaN through to the handler too. `ControlRegistry.cpp:277` runs that validation before the handler.
- `src/core/InstrumentLayers.cpp:390` (`setMix`): `std::clamp(mix.level, 0.0f, MaxLevel)` returns NaN unchanged, so the NaN is stored in `m_mix` and published as the gain.
- `src/core/InstrumentLayers.cpp:134` (`readMix`, the load path): the same clamp, so a saved `layerlevel="nan"` loads as NaN. Confirmed at run time: the RED assertion at `tests/src/core/InstrumentLayerZonePublishTest.cpp:211` (the test's `std::isfinite` check) fails on the reverted code, so Qt's `toFloat` parses `"nan"`.
- Reachability: the JSON wire cannot carry a NaN literal, so the command is reached in-process (a caller that builds the NaN double, as the test does). I did not verify whether a Lua or script path can produce NaN.

## Fix (the cause, and the two sinks it flows into)

1. `src/core/ControlCommandsRackLayers.cpp:417` `layerSetRefusal`: `!std::isfinite(level) ||` added to the range check. A NaN level is refused as `InvalidArgs`, the same kind as an out-of-range level. The refusal returns before `addUndoStep` (line 442), so a refused call records no undo step.
2. `src/core/InstrumentLayers.cpp:390` `setMix`: a non-finite level returns `false` and the mix is not changed. Header doc: `include/InstrumentLayers.h:137`. Its two callers ignore the return value: `ControlCommandsRackLayers.cpp:429` (`mixWriter`) and `:440` (`setLayerMix`).
3. `src/core/InstrumentLayers.cpp:135` `readMix`: a non-finite `layerlevel` loads as unity, the same as an unparseable one.

Behaviour changes, all limited to non-finite input:
- In-process `setMix` with a NaN level on layer 0 used to store mute/solo and force the level to 1.0; it now returns `false` and changes nothing. No command reaches this.
- A saved `layerlevel="inf"` used to load clamped to 2.0; it now loads as 1.0. `writeMix` (`InstrumentLayers.cpp:121`) writes only finite levels, so no save produces inf.
- Valid saved levels and every finite command level are unchanged (`0.5` round-trips in the test).

Not changed, recorded as a follow-up: `ControlSchema.cpp` `checkRange` still lets NaN through `minimum`/`maximum` for every other numeric command. Changing it would change every command's behaviour, which is outside this finding. Evidence is the code at `ControlSchema.cpp:87-100`. I did not run it against other commands.

## Regression test

Added to the existing `tests/src/core/InstrumentLayerZonePublishTest.cpp` (no new file, no CMake change):

- `layerLevelRefusesNonFinite`: a NaN `rack.layer_set` is refused as `InvalidArgs` and the layer's level stays the same; `setMix` refuses a NaN level; a finite `0.5` still lands.
- `savedLayerLevelNotANumberLoadsAsUnity`: the track is saved and restored through its own path with `layerlevel="0.5"` (loads 0.5), then `layerlevel="nan"` (loads 1.0, finite).

Why this file: `InstrumentLayersCommandsTest.cpp` is at the file-length ratchet. The gate counted 546 code lines after I added the tests there (the base was under 500), so I moved them. A new test file would need a CMake reconfigure, which this lane's `bunny-guard` blocks ("configuring is the parent's job"). Both tests use a fresh instrument track and are deterministic: no timing, sleeps or races.

Environment note: the instrument fixture skipped in this build ("this build exposes no loadable instrument"), so the layer tests did not run at first. I built the `tripleoscillator` module target (`build-3-instrument-module.log`, EXIT=0); the fixture then loads it. The existing layer tests were skipping for the same reason before my change.

## Commands and exit codes

ccache could not write its temp dir (`build-1-red.log`, EXIT=2: "Permission denied" under `~/.local/share/zene-bunny-fix/dsh/...`), so every build below sets `CCACHE_DISABLE=1`. Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p22-60.r1/logs/`.

| Step | Command (from the worktree, or `build/tests` for the binaries) | Log | EXIT |
|---|---|---|---|
| First try (skipped, not a valid RED) | `ctest -R ^InstrumentLayersCommandsTest$` | test-1-red.log | 0 (skip) |
| Instrument module | `CCACHE_DISABLE=1 cmake --build build --target tripleoscillator -j4` | build-3-instrument-module.log | 0 |
| Baseline, old file, no fix | `./InstrumentLayersCommandsTest` | test-3-full-baseline.log | 0 (5 pass, 11 skip) |
| RED, fix applied, first version of the test | `./InstrumentLayersCommandsTest layerLevelRefusesNonFinite` | test-4-red-direct.log | 1 (FAIL: `!refused.ok` returned FALSE, test line 468) |
| 4b revert | `git diff -- src/core/ControlCommandsRackLayers.cpp src/core/InstrumentLayers.cpp include/InstrumentLayers.h > logs/fix.patch` ; `git apply -R logs/fix.patch` | fix.patch | 0 ; 0 |
| RED, product reverted, old file | `CCACHE_DISABLE=1 cmake --build build --target InstrumentLayersCommandsTest -j4` ; `./InstrumentLayersCommandsTest` | build-4-red-reverted.log, test-5-red-reverted.log | 0 ; 2 (FAIL at test lines 470 and 509) |
| Tests moved to `InstrumentLayerZonePublishTest` | `CCACHE_DISABLE=1 cmake --build build --target InstrumentLayerZonePublishTest -j4` | build-9-red-moved.log | 0 |
| **RED**, product reverted, final test file | `./InstrumentLayerZonePublishTest` | test-9-red-moved.log | **2** (`layerLevelRefusesNonFinite` FAIL at line 169; `savedLayerLevelNotANumberLoadsAsUnity` FAIL at line 211; 5 pass, 2 fail) |
| 4b reapply | `git apply logs/fix.patch` ; `CCACHE_DISABLE=1 cmake --build build --target zene InstrumentLayerZonePublishTest -j4` ; `./InstrumentLayerZonePublishTest` | build-10-green-moved.log, test-10-green-moved.log | 0 ; 0 ; **0** (7 passed, 0 failed) |
| Family rebuild, final tree | `CCACHE_DISABLE=1 cmake --build build --target zene ReversibilityUndoTest InstrumentLayerZonePublishTest InstrumentLayersCommandsTest RackZonesTest RackMacrosTest RackZoneRoutingTest InstrumentRoutingTest ArticulationPlaybackTest -j4` | build-11-family-final.log | 0 |
| **Family ctest**, from `build/tests` | `QT_QPA_PLATFORM=offscreen ctest -R '^(InstrumentLayersCommandsTest\|InstrumentLayerZonePublishTest\|ReversibilityUndoTest\|RackZonesTest\|RackMacrosTest\|RackZoneRoutingTest\|InstrumentRoutingTest\|ArticulationPlaybackTest)$'` | test-11-family-final.log | **0** (100% tests passed, 8 out of 8) |
| Gate, stock script | `python3 feature_check.py <worktree>` | gate-1-feature-check.log | 1 (PermissionError creating the sibling log folder, outside the jail; no checks ran) |
| Gate, same script, logs redirected | `python3 -I logs/gate-driver/feature_check_logdir.py <worktree>` | gate-3-feature-check.log | **0** (all 8 rows PASS) |

Gate note: the stock `feature_check.py` writes its logs into `<worktree>/../<name>-feature-check/`, outside my write-jail. I ran a copy of the same script with two paths changed: the logs go to my log folder (`logs/gate-feature-check/`), and `ROOT` points at the real `qwen/` directory (the baseline cache). The gate rules, limits and checks are unchanged. `gate-2-feature-check.log` (EXIT=1) shows the file-length row failing on `InstrumentLayersCommandsTest.cpp` at 546 code lines, which is why the tests moved; gate-3 runs after the move.

Affected family: 8 ctest tests run, all pass. The two layer binaries executed 7 (`InstrumentLayerZonePublishTest`) and 16 (`InstrumentLayersCommandsTest`, with 1 pre-existing skip for the missing clapinstrument module).

## Self-check

Answers to the completion-check items (`submit-check-1.log`, run after the commit):

- **Regression test fails with the product change reverted and passes with it (exit codes):** yes. RED: `test-9-red-moved.log`, EXIT=2, both new cases fail. GREEN: `test-10-green-moved.log`, EXIT=0, 7 pass.
- **Deterministic (name the seam):** yes. No wall-clock, sleeps or threads. The inputs are a NaN double passed to `run()` and a fresh instrument track. The save/restore is `track->saveState` then `track->restoreState` on the same object, the same path a project load takes.
- **Fix at the cause, every caller and sibling checked:** the three sinks are the command refusal (`ControlCommandsRackLayers.cpp:417`), the store (`InstrumentLayers.cpp:390`) and the load (`InstrumentLayers.cpp:135`). `setMix` has exactly two callers (`ControlCommandsRackLayers.cpp:429` and `:440`, checked with `rg 'setMix\(' src include`); both ignore its return value. The schema hole (`ControlSchema.cpp:87-100`) is a recorded follow-up, not changed (see above).
- **No collateral damage:** the new checks refuse only non-finite values. `writeMix` (`InstrumentLayers.cpp:121`) writes only finite levels, and no valid command produces NaN, so no caller that legitimately uses a level is affected. The limit is not a new bound, so no caller that exceeds a real bound is affected. The family ctest (8/8) includes `ReversibilityUndoTest`, which covers `rack.layer_set` mute/solo undo.
- **Verdict FIXED, not ALREADY-FIXED or FALSE-POSITIVE:** the citation clause does not apply. The evidence is the pre-fix code at the lines above.
- **Sibling commands (the checker's list is the whole `rack` group from the snapshot, not commands in this file: none of `rack.add_chain`, `rack.get_state`, `rack.macro_*`, `rack.remove_chain` appears in `ControlCommandsRackLayers.cpp`):**
  - `rack.layer_set`: the fixed command.
  - `rack.layers`: reads the mix through `mixJson` (`ControlCommandsRackLayers.cpp:76`). Unaffected: it reads finite values only.
  - `rack.layer_add`, `rack.layer_remove`, `rack.layer_zone_add`, `rack.layer_zone_remove`: they change layer structure, and their undo replays layer XML written by `writeMix` (finite levels). They do not call `setMix` and their results are unchanged.
  - `rack.macro_add`, `rack.macro_remove`, `rack.macro_set`, `rack.macro_set_pair`, `rack.macro_target_add`, `rack.macro_target_remove`, `rack.add_chain`, `rack.remove_chain`, `rack.get_state`: none of them writes a layer's mix (no `setMix` call; `rg 'setMix\(' src include` finds only the two layer callers above). Unaffected.
- **Undo:** a refused call records no undo step, because the refusal returns before `addUndoStep` (`ControlCommandsRackLayers.cpp:442`). The accepted path's undo is unchanged.
- **Realtime:** no audio-thread code changed. `layerSetRefusal`, `setMix` and `readMix` run on the control thread and the load path. The added `std::isfinite` is a branch, with no allocation, lock, log or I/O.
- **Old projects load as before:** finite `layerlevel` values are untouched, and the round-trip case (0.5) is tested. Only corrupt NaN or inf input changes (it loads as unity).
- **Registration:** no new file, so no CMake, `fork-sources.txt` or `all-sources.txt` change. Edited files: `include/InstrumentLayers.h`, `src/core/ControlCommandsRackLayers.cpp`, `src/core/InstrumentLayers.cpp`, `tests/src/core/InstrumentLayerZonePublishTest.cpp`. The gate's `no-upstream-regression` row passed.
- **Commands snapshot and canary:** no command was added and no description changed, so the snapshot and canary are unchanged.
- **Gates:** `gate-3-feature-check.log`, EXIT=0, all 8 rows PASS. The submit check's own gate row (`submit-check-1.log`) FAILS for an environment reason only: it runs the stock `feature_check.py`, which hits the same `PermissionError` on its sibling log folder (`gate-1-feature-check.log`, EXIT=1), so it prints no REGRESSION lines. The parent should run the stock script from outside this write-jail to confirm the row.
- **Not verified:** a Lua or script path that could produce a NaN (only the in-process call is tested); the other numeric commands that share `checkRange`; the full `ctest` tree (only the 8-test family and the two layer binaries). The clapinstrument-dependent multi-output test stays skipped in this build.
