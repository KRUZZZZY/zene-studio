FIXED

# Cluster new-p16-139: track routes can bind the same parameter more than once

Finding (Luna patrol, "BUG 9"): `ModulationLayer::findRoute` compared `channel`, `chain` and `effect` for track
routes too, although `ModulationRoute::track >= 0` makes those fields unused. Two track routes on the same track and
parameter that differed only in the unused fields both passed duplicate detection.

## Decision: REAL, fixed at the cause

### Evidence (current tree, file:line)

- The duplicate check is `findRoute`, and its comparison compared the unused fields for every route:
  `src/core/ModulationLayer.cpp:525-541` (before the fix, the `same` expression at the original `:531-533`).
- The fields are documented as unused for a track route: `include/ModulationLayer.h:136-139`
  ("'parameter' is the display name, channel/chain/effect are unused").
- A track route resolves by track and parameter name only, so two such routes that differ in channel/chain/effect
  resolve to the same model: `src/core/ControlModulationSupport.cpp:133` (`if (route.track >= 0) { return trackRouteModel(...); }`)
  and `src/core/ControlModulationSupport.cpp:83-97` (`trackRouteModel` looks up `instrument->parameterName(i) == route.parameter`).
- Both callers of `findRoute` for track routes rely on it as the duplicate guard:
  `src/core/ControlCommandsModulatorVoices.cpp:170` (`refusedInstrumentRoute`) and
  `src/core/ControlCommandsModulatorRoutes.cpp:174` (`refusedRoute`). The rack path is the other caller.
- Reachability: the bind command builds track routes with `channel = -1`, `chain = 0`, `effect = 0`
  (`src/core/ControlCommandsModulatorVoices.cpp:184-193`), so the live command path cannot create a differing pair by
  itself. `readRoute` (`src/core/ModulationLayer.cpp:174-184`) reads `channel`/`chain`/`effect` from any saved route,
  including a track route, and `loadSettings` does not check for duplicates. A project file holding a track route with
  non-default unused fields therefore lets the next bind of that track and parameter through, and the two routes then
  write the same model. Trigger is a route pair differing only in unused fields, which a saved project can carry.

### The fix

`src/core/ModulationLayer.cpp:525-541`: for a track route (`route.track >= 0`) the candidate matches on track and
parameter only; a rack route keeps the full rack address (channel, chain, effect) plus track and parameter, as before.

```cpp
const bool rackFieldsMatch = route.track >= 0
    || (candidate.channel == route.channel && candidate.chain == route.chain && candidate.effect == route.effect);
const bool same = candidate.track == route.track && rackFieldsMatch
    && candidate.parameter == route.parameter;
```

Callers checked: both `findRoute` callers (`ControlCommandsModulatorVoices.cpp:170`,
`ControlCommandsModulatorRoutes.cpp:174`) and the existing test that asserts a rack address is not an instrument
parameter (`tests/src/core/InstrumentRoutingTest.cpp:614-617`) keep their meaning: a rack candidate has `track == -1`,
so it never matches a track route.

### Regression test

`tests/src/core/ModulationLayerTest.cpp:606` `aTrackRouteIsFoundByItsTrackAndParameterAlone()` (new slot, in the
listed `ModulationLayerTest`). It binds a track route (track 7, parameter "Cutoff") and asserts:
- the same track and parameter with other channel/chain/effect and depth is found (the bug);
- the same parameter on track 8 is not found;
- a rack route with that display name is not found;
- rack routes still bind by channel: another channel for "Gain" is not found, the same address is.

It uses only `ModulationLayer` and `routeOf`/`sineAt` from `tests/src/core/ModulationTestSupport.h`; no timing, no
sleeps, no audio.

### Commands and results

Ccache was disabled for the builds below (`CCACHE_DISABLE=1`): the first attempt failed with
`ccache: error: failed to create temporary file ... Permission denied` in the sandbox, not in the code
(`LOGS/build-red-1.log`, EXIT=2). Nothing else differs.

| Step | Command (cwd = worktree unless noted) | Log (LOG FOLDER) | EXIT |
| --- | --- | --- | --- |
| RED build | `CCACHE_DISABLE=1 cmake --build build --target ModulationLayerTest -j4` | build-red-2.log | 0 |
| RED test | `cd build/tests && ctest -R '^ModulationLayerTest$' --output-on-failure` | test-red-1.log | **8** |
| RED assertion | `FAIL!  : ...aTrackRouteIsFoundByItsTrackAndParameterAlone() 'layer.findRoute(again) != nullptr' returned FALSE` | test-red-1.log:23 | - |
| GREEN build | `CCACHE_DISABLE=1 cmake --build build --target ModulationLayerTest zene -j4` | build-green-1.log | 0 |
| GREEN test | `cd build/tests && ctest -R '^ModulationLayerTest$' --output-on-failure` | test-green-1.log | 0 |
| GREEN count | `ctest -R '^ModulationLayerTest$' -V` ("Totals: 15 passed, 0 failed") | test-green-2.log | 0 |
| Family build | `cmake --build build -j4 --target ModulationLayerValueTest --target ModulationLayerProjectRoundTripTest --target ModulationRateTest --target InstrumentRoutingTest --target ModulationAudioViewTest --target ControlModulatorCommandsTest --target InstrumentLayersCommandsTest` | build-family-2.log | 0 |
| Family test | `cd build/tests && ctest -R '^(ModulationLayerTest\|ModulationLayerValueTest\|ModulationLayerProjectRoundTripTest\|ModulationRateTest\|InstrumentRoutingTest\|ModulationAudioViewTest\|ControlModulatorCommandsTest\|InstrumentLayersCommandsTest)$' --output-on-failure` | test-family-1.log | 0 (8/8 passed) |
| 4b: save fix | `git diff -- src/core/ModulationLayer.cpp > LOGS/fix.patch` | fix.patch | 0 |
| 4b: revert | `git apply -R LOGS/fix.patch` | - | 0 |
| 4b: RED again | `CCACHE_DISABLE=1 cmake --build build --target ModulationLayerTest -j4`; `cd build/tests && ctest -R '^ModulationLayerTest$' --output-on-failure` | build-4b-revert.log, test-4b-revert.log | build 0, **test 8** (same assertion) |
| 4b: reapply | `git apply LOGS/fix.patch`; rebuild; ctest | build-4b-fix.log, test-4b-fix.log | apply 0, build 0, **test 0** |
| Gates | `python3 feature_check.py <worktree>` (see note) | gates-2.log | **0**, RESULT: PASS |

Gate note: `feature_check.py` writes its per-gate logs to `<worktree>/../<name>-feature-check`, which is outside the
write-jail (`projects/zene-studio-program/` is not writable from this lane) and failed with `PermissionError`
(`gates-1.log`, EXIT=1). I ran a copy of the same script (`LOGS/feature_check_logdir.py`) with two lines changed: the
log folder is `LOGS/feature-check/`, and `ROOT` is pinned to the original `~/.local/share/zene-bunny-fix/qwen` so
`FIX-BASE` and the cached base listing resolve as before. Gate logic and baselines are unchanged. Result table:

```
feature-check zene-qwen-new-p16-139 (base 37eaf0755, 2 changed files; ...)
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

No new file was added, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` needed no entry.
No header changed, so no dependent target needed a rebuild beyond the ones listed above. No command was added, so the
ControlCommandsSnapshot canary does not move. The fix adds no allocation, lock, log or Qt call; `findRoute` is
control-thread code, not on the audio path.

## Files committed

- `src/core/ModulationLayer.cpp`
- `tests/src/core/ModulationLayerTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p16-139.md`

## Open points (not changed)

- `ModulationLayer::loadSettings` does not check for duplicate routes, so a saved project that already holds two track
  routes on one parameter still loads both. This fix stops a new bind from adding a second one; it does not repair a
  file that already has one. Changing the load path is a behaviour change for old projects, so I left it for a decision.

## Self-check

Output of `submit_check.py` on commit 0b00d4b308 (LOGS/submit-check-1.log, EXIT=1):
PASS commits on the branch, PASS working tree clean, PASS report, PASS tests added or changed, **FAIL gates**.

The gates FAIL is the sandbox, not the code. `submit_check.py` runs the unmodified `feature_check.py`, which writes to
`<worktree>/../<name>-feature-check` and fails with `PermissionError` before any gate runs (reproduced directly, same
message). The gate content passed: the redirected run (`LOGS/gates-2.log`, EXIT=0, RESULT: PASS, table above). I did not
change the shared script or create directories outside the write-jail. The gate row needs a run outside this sandbox,
or a log path the lane can write. Nothing in the diff is unchecked by the gates.

1. **RED / GREEN proof.** RED: the test failed before the fix (`test-red-1.log`, EXIT=8, assertion at line 23). Also
   RED with the product file reverted (`git apply -R LOGS/fix.patch`, `test-4b-revert.log`, EXIT=8, same assertion).
   GREEN with the fix (`test-green-1.log` EXIT=0, `test-green-2.log` "Totals: 15 passed"; `test-4b-fix.log` EXIT=0 after
   reapplying the patch).
2. **Deterministic.** The test builds an in-memory `ModulationLayer` and calls `addRoute` / `findRoute`. No timer, sleep,
   thread or audio period. Inputs come from `routeOf` and `sineAt` (`tests/src/core/ModulationTestSupport.h`) and
   literal values.
3. **At the cause, complete.** `findRoute` (`src/core/ModulationLayer.cpp:525`) is the only route-address comparator in
   the layer. Searched with `rg` for `candidate.channel ==` / `.effect ==` / `.chain ==`: no other comparator. Callers
   checked: `ControlCommandsModulatorVoices.cpp:170` (instrument bind), `ControlCommandsModulatorRoutes.cpp:174` (rack
   bind), `tests/src/core/InstrumentRoutingTest.cpp:614-617` (rack address is not the instrument parameter, still true
   because a rack candidate has `track == -1`), and `ModulationLayerValueTest.cpp:129-130` (findRoute of rack routes
   returns nullptr where no rack route matches; rack path unchanged). Track-route resolution (`ControlModulationSupport.cpp:133`, `:83-97`) already uses
   track and parameter only, so the matcher now agrees with it.
4. **No collateral damage.** `saveRoute` / `readRoute` / `loadSettings` are untouched, so saved bytes and loading are
   unchanged. No limit or check added. The only behaviour change: a track route is now refused when another track route
   has the same track and parameter, whatever its unused fields. The family run passed 8/8 (`test-family-1.log`), including
   `InstrumentRoutingTest`, `ModulationLayerProjectRoundTripTest` and `ControlModulatorCommandsTest`. Open point (not
   changed): a file that already contains such a pair still loads both (see "Open points").
5. **Verdict.** REAL, so the ALREADY-FIXED / FALSE-POSITIVE evidence clause does not apply.
