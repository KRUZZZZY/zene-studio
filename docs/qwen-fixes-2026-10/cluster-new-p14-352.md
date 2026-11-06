FIXED

new-p14-352: a reversed (or empty) freeze.region take silenced its source from its start onward. Fixed: a region take must end past its start. It is refused when it is made (`Track::freezeTo`, and both freeze verbs before they cut any clip) and when a project carries it (`Track::loadFrozenTake`).

## Evidence

Reproduced by reading (the finding's claim holds on this branch):

| Claim | Evidence |
| --- | --- |
| Silencing ignores the window's end when `end <= start` | `src/core/Track.cpp:1453-1458` (`frozenTakeSilencesSource`): `return position >= m_frozen.startTicks && (m_frozen.endTicks <= m_frozen.startTicks \|\| position < m_frozen.endTicks);` - an inverted window has no end boundary |
| `freezeTo()` accepted the window | `src/core/Track.cpp:1297-1340` before the fix: no order check on `startTicks`/`endTicks` |
| Project load accepted the window | `src/core/Track.cpp:1378-1407` before the fix: `frozenStart`/`frozenEnd` are parsed and only parse-checked |
| Control verb path already refuses it | `src/core/ControlBounceSupport.cpp:81`: `'end' must be past 'start'` (`parseRange`, shared by freeze.region, render.queue_bounce) - so the verb path is not the reachable route |
| Render-window route | `src/core/BounceInPlace.cpp:239-241` (`collectResult`): `rendered.range.end` is recomputed from the frame count, so it can round to `start`. Inspected only; not reproduced with a real render (unverified) |

## Fix

- `include/Track.h:267-271`: `Track::isValidRegionWindow(start, end)` = `end > start`, the single rule.
- `src/core/Track.cpp:1300-1305` (`freezeTo`): a region with an invalid window returns false with an error before anything is touched.
- `src/core/Track.cpp:1408-1414` (`loadFrozenTake`): a region that loads with an invalid window is discarded like an unparseable tick (`m_frozen = FrozenTake{}`); `loadTrack` (`src/core/Track.cpp:936-939`) then unmutes the clips that take had muted. The unmute path is verified by reading only, not by a test.
- `src/core/ControlCommandsFreeze.cpp:160-171` (`renderRefusal`) and `:205-209` (`freezeRange`): refused before the checkpoint and the clip cuts. It folds the existing `!rendered.ok` refusal into one helper, so `freezeRange` keeps its complexity at 10.
- `src/core/ControlCommandsRenderBounce.cpp:191-195` (`applyFreeze`, the render-queue path): same check before the checkpoint and the cuts.

Callers of `freezeTo` checked: `ControlCommandsFreeze.cpp:224` (freeze.track / freeze.region), `ControlCommandsRenderBounce.cpp:201` (render queue), `Track::loadFrozenTake` (project load), and the tests (all pass valid windows or `region=false`). Whole-track takes (`region=false`) never reach the check.

Left unchanged on purpose: the `end <= start` guards in `frozenTakeSilencesSource` (`Track.cpp:1457`) and `playFrozenTake` (`Track.cpp:1483`). They are unreachable for region takes now, and changing them would only touch whole-track or corrupt in-memory state.

Test: `tests/src/core/FreezeRegionRepairTest.cpp:214` `reversedRegionTakeIsRefusedWhenCreatedAndLoaded`.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-352.r1/logs`. `CCACHE_DISABLE=1` is needed in this shell: ccache fails with a permission error on its temp directory (`build-1-red.log`, EXIT=2, not a compile error).

| Evidence | Command | Result |
| --- | --- | --- |
| RED build (test only, before the fix) | `cmake --build build --target FreezeRegionRepairTest -j4 > build-3-red.log 2>&1` | EXIT=0 (build-2 EXIT=2 was my own test compile error, fixed) |
| RED test | `cd build/tests && ctest -R '^FreezeRegionRepairTest$' --output-on-failure > test-1-red.log 2>&1` | EXIT=8: `FAIL! ... '!track->freezeTo(wav, 200, 100, {}, &why, true)' returned FALSE`; 11 passed, 1 failed |
| GREEN build (final) | `cmake --build build --target zene FreezeRegionRepairTest -j4 > build-8-final.log 2>&1` | EXIT=0 |
| Family build (final) | `cmake --build build --target <each of 14 targets> -j4 > build-9-<target>.log 2>&1` | EXIT=0 for all 14 |
| GREEN test (final) | `cd build/tests && ctest -R '^FreezeRegionRepairTest$' --output-on-failure > test-8-green-final.log 2>&1` | EXIT=0: 100% passed, 1/1 |
| Affected family (final) | `cd build/tests && ctest -R '^(FrozenTakeHandoffTest\|FrozenTakeRoundTwoTest\|FreezeRegionRepairTest\|BounceInPlaceTest\|ControlBounceSupportTest\|ControlBounceRenderTest\|TrackPresetCommandsTest\|TrackRenderMenuTest\|MixerOrderByTracksTest\|ControlVerbInverseTest\|AutomationRenderAllocationTest\|WriteRefusalGateTest\|SamplerTakeTest\|TakeLaneTest\|TakeLaneCompTest)$' --output-on-failure > test-6-final-family.log 2>&1` | EXIT=0: 100% passed, **15 of 15** executed |
| Mutation proof, step 1: revert the fix | `git apply -R logs/fix-final.patch` | EXIT=0 |
| Mutation proof, step 2: build unfixed | `cmake --build build --target zene FreezeRegionRepairTest -j4 > build-10-unfixed-final.log` | EXIT=0 |
| Mutation proof, step 3: test must FAIL | `ctest -R '^FreezeRegionRepairTest$' ... > test-7-red-final.log` | EXIT=8: same `returned FALSE` assertion; 11 passed, 1 failed |
| Mutation proof, step 4: re-apply the fix | `git apply logs/fix-final.patch` | EXIT=0 |
| Mutation proof, step 5: rebuild | `cmake --build build --target zene FreezeRegionRepairTest -j4 > build-11-refixed-final.log` | EXIT=0 |
| Mutation proof, step 6: test must PASS | `ctest -R '^FreezeRegionRepairTest$' ... > test-8-green-final.log` | EXIT=0 |
| Gates (manual, see note) | `bash tests/fork-sources-gate.sh`, `bash tests/all-sources-reproduce.sh`, `bash tests/no-upstream-regression-gate.sh`, `tests/scripted/check-namespace`, `bash tests/no-tautology-gate.sh`, `python3 tests/doc-figures-gate.py` | EXIT=0 each (`gates-3-final-exits.log`) |
| Gates: complexity and file-length | `bash tests/complexity-gate.sh --check`; `bash tests/file-length-gate.sh --check` | EXIT=1 each; every REGRESSION row is in a file this branch does not change (see Self-check) |
| feature_check driver | `python3 .../qwen/feature_check.py <worktree>` | Not usable from this jail: `PermissionError` creating `<projects>/zene-qwen-new-p14-352-feature-check` (`gates-1-feature-check.log`, EXIT=1). The gates above are the same commands the driver runs |

## Self-check

- **Regression fails with the fix reverted, passes with it (exit codes):** RED `test-7-red-final.log` EXIT=8 (`freezeTo(wav, 200, 100, ..., true)` returned true before the fix); GREEN `test-8-green-final.log` EXIT=0. Both use the same test file and the same binary sources, differing only in the four product files in `logs/fix-final.patch`.
- **Deterministic:** no sleep, timing, threads or races. Inputs are fixed ticks (200/100, 50/50, 0/0, 10/50); the audio is a `taketest::writeToneWav` fixture in a `QTemporaryDir`; the load path round-trips a `QDomElement` from `saveState()` and sets two attributes. No new seam was needed.
- **Fix at the cause and complete:** the one rule is `Track::isValidRegionWindow`, used at the three places a region take is made or loaded (`freezeTo`, `loadFrozenTake`, and the two freeze callers that cut clips first). Every caller of `freezeTo` is listed above. The control path already refused the case (`ControlBounceSupport.cpp:81`).
- **No collateral damage:**
  - Whole-track takes (`region=false`) never hit the check; `FrozenTakeHandoffTest`, `FrozenTakeRoundTwoTest`, `ControlVerbInverseTest`, `AutomationRenderAllocationTest` pass unchanged.
  - Valid region takes and their save/load (`regionFlagRoundTripsAndWholeTrackTakeIsUnchanged`) pass unchanged.
  - Deliberate behaviour change for saved data: a region take whose saved window is inverted or empty (which silenced its source from its start onward) now loads without the take. Its track plays its own clips and the clips it muted are released. Reviewer: confirm this is the intended rule.
  - The `isValidRegionWindow` check only refuses `end <= start`, which no valid caller produces.
- **Verdict is FIXED, not ALREADY-FIXED or FALSE-POSITIVE:** the code was as the finding described (evidence table above).
- **Realtime / undo:** no audio-thread code changed. The new code is control-thread only (`freezeTo`, `loadFrozenTake`, freeze verbs). The refusals run before the journal checkpoint and the clip cuts, so a refused freeze leaves no journal step.
- **Gates:**
  - Simple gates: all EXIT=0.
  - Complexity: 6 REGRESSION rows, all in files this branch does not change (`src/wasm/WasmSandbox.cpp`, `tests/src/gui/XyPadPairTest.cpp`, `src/core/DawProjectReadTracks.cpp`, `src/core/ControlCommandsMixerStrip.cpp`, `src/core/ControlCommandsMixerTools.cpp`, `src/core/ControlCommandsSessionRecordLand.cpp`). The branch's changed files are `include/Track.h`, `src/core/Track.cpp`, `src/core/ControlCommandsFreeze.cpp`, `src/core/ControlCommandsRenderBounce.cpp`, `tests/src/core/FreezeRegionRepairTest.cpp`, `tests/upstream-modifications.txt`. The earlier `freezeRange` CCN regression (12) is fixed (now 10 or under).
  - File-length: 23 REGRESSION rows, none in the changed files (none names `Track.cpp`, `Track.h`, `ControlCommands*Freeze*`, `ControlCommandsRenderBounce.cpp` or `FreezeRegionRepairTest.cpp`).
  - The driver itself could not write its logs (see table), so its PASS/FAIL table was not produced.
- **Registration:** `include/Track.h` and `src/core/Track.cpp` are inherited and each got an entry in `tests/upstream-modifications.txt` for this change. `ControlCommandsFreeze.cpp`, `ControlCommandsRenderBounce.cpp` and `FreezeRegionRepairTest.cpp` are already in `tests/fork-sources.txt`, so they need no entry.

Files committed for this verdict: `include/Track.h`, `src/core/Track.cpp`, `src/core/ControlCommandsFreeze.cpp`, `src/core/ControlCommandsRenderBounce.cpp`, `tests/src/core/FreezeRegionRepairTest.cpp`, `tests/upstream-modifications.txt`, `docs/qwen-fixes-2026-10/cluster-new-p14-352.md`.
