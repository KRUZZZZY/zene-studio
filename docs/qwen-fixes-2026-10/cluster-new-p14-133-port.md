FIXED

# Cluster new-p14-133-port: LFO white-noise state handed to the render thread

## Decision

**REAL.** The LFO controller connected its wave-shape model's `dataChanged()` signal to
`updateSampleFunction()` with `Qt::DirectConnection`, and that slot wrote
`m_sampleFunction` (and, for white noise, `m_heldSample`) on the GUI thread while
`updateValueBuffer()` read and wrote the same fields on the render thread. Making the
fields atomic (cluster new-p9-249) removed the UB but not the logical race: the GUI could
change the sample function and reseed the held white-noise sample in the middle of a
render period, so the shape the period rendered and the function it called could disagree.

The fix gives the render thread ownership at its boundary:

- `src/core/LfoController.cpp:52-70` derives the sample function from the wave shape with
  `sampleFunctionFor()`.
- `src/core/LfoController.cpp:120-135` reads the published shape once at the start of
  `updateValueBuffer()`, stores the derived function, and seeds `m_heldSample` there when
  that shape is WhiteNoise. No GUI slot writes either field.
- `include/LfoController.h:89-96` makes the render state explicit: `m_duration`,
  `m_phaseOffset`, `m_currentPhase`, `m_sampleFunction` and `m_heldSample` are atomics and
  `m_renderedWaveShape` records the shape the render last used.
- `include/LfoController.h:113-115` no longer declares `updateSampleFunction()`;
  `src/core/LfoController.cpp:300-320` no longer calls it after `loadSettings()`. A search
  found only those two callers: the constructor connection and the load tail, both removed.
- `src/core/LfoController.cpp:255-260` updates the UI-written phase under the engine change
  mutex and stores it atomically, so `updatePhase()` cannot race a render period either.

## Test matrix

| Case | Test | What it covers |
| --- | --- | --- |
| failure (the reported race) | `LfoControllerUserWaveTest::guiWaveShapeChangeLeavesRenderStateUntilTheBoundary` | after a Square render, a GUI switch to WhiteNoise must leave `m_sampleFunction`/`m_heldSample` untouched; the next boundary applies the shape and seeds a finite held sample |
| normal render | `LfoControllerUserWaveTest::whiteNoiseHoldsOneSampleForThePeriod` | once the boundary applies WhiteNoise, one held sample drives the whole period (no phase wrap) |
| reconfiguration | `LfoControllerUserWaveTest::renderBetweenWaveChangeAndSlotDoesNotCallNullFunction` | UserDefined (null function) to WhiteNoise with the model signal blocked still renders finite values |
| reconfiguration / publication | `LfoControllerUserWaveTest::parameterStateSharedWithRenderIsAtomic` | duration, function and held sample remain lock-free atomics; speed/multiplier behaviour is unchanged |
| sibling lifetime | `LfoControllerUserWaveTest::replacedWaveRemainsAliveWhileRenderLeaseIsHeld` | the new-p15-90 user-wave ownership path is unchanged and still safe |
| exhaustion | not applicable | this change has no capacity or exhaustion path; no new limit or check was added |
| audio thread | product inspection of `updateValueBuffer()` | only atomic loads/stores, one raw-pointer read and a pure switch are added; no allocation, free, mutex, wait, I/O or logging. The atomics are covered by the existing `is_always_lock_free` static assertions. |
| platform build | `build-4-zene.log`, `build-7-green-restored.log` | `zene` and the test build on this Linux tree; the change uses C++20 `<atomic>` only, with no Qt-6-only or platform-specific API |

The regression test is deterministic: it advances `Controller::triggerFrameCounter()`
instead of relying on a live render thread, and uses `quiet_NaN()` as the held-sample
sentinel (the noise generator never produces NaN). There is no sleep, wall-clock bound or
concurrent assertion.

## Commands and EXIT codes

All logs are under
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p14-133-port.r11/logs`.

| Step | Command | EXIT |
| --- | --- | --- |
| RED build (new test, unfixed product) | `cmake --build build --target LfoControllerUserWaveTest -j4 > .../build-1-red.log 2>&1; echo EXIT=$?` | 0 |
| RED run | `cd build/tests && ctest -R LfoControllerUserWaveTest --output-on-failure > .../test-1-red.log 2>&1; echo EXIT=$?` | 8 |
| GREEN build | `cmake --build build --target LfoControllerUserWaveTest -j4 > .../build-3-green.log 2>&1; echo EXIT=$?` | 0 |
| GREEN run | `cd build/tests && ctest -R LfoControllerUserWaveTest --output-on-failure > .../test-3-green.log 2>&1; echo EXIT=$?` | 0 |
| save product-only patch | `git diff -- include/LfoController.h src/core/LfoController.cpp > .../fix.patch; echo PATCH_EXIT=$?` | 0 |
| reverse product patch | `git apply -R .../fix.patch; echo APPLY_REVERSE_EXIT=$?` | 0 |
| RED-control build | `cmake --build build --target LfoControllerUserWaveTest -j4 > .../build-6-red-control.log 2>&1; echo BUILD_EXIT=$?` | 0 |
| RED-control run | `cd build/tests && ctest -R LfoControllerUserWaveTest --output-on-failure > .../test-6-red-control.log 2>&1; echo EXIT=$?` | 8 |
| restore product patch | `git apply .../fix.patch; echo APPLY_EXIT=$?` | 0 |
| restored build (`zene` + test) | `cmake --build build --target LfoControllerUserWaveTest zene -j4 > .../build-7-green-restored.log 2>&1; echo BUILD_EXIT=$?` | 0 |
| restored run | `cd build/tests && ctest -R LfoControllerUserWaveTest --output-on-failure > .../test-7-green-restored.log 2>&1; echo EXIT=$?` | 0 |
| LFO family build | `cmake --build build --target EnvelopeAndLfoParametersTest LfoGraphTest DelayLfoAutomationTest -j4 > .../build-5-family.log 2>&1; echo EXIT=$?` | 0 |
| LFO family run | `cd build/tests && ctest -R Lfo --output-on-failure > .../test-5-family.log 2>&1; echo EXIT=$?` | 0 |
| gates (final) | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../feature_check-2-final.log 2>&1; echo EXIT=$?` | 0 |

The first RED failure was:

```
FAIL!  : LfoControllerUserWaveTest::guiWaveShapeChangeLeavesRenderStateUntilTheBoundary() 'lfo.m_sampleFunction.load() == &Oscillator::squareSample' returned FALSE. (the GUI wrote the render-owned sample function)
   Loc: [../../tests/src/core/LfoControllerUserWaveTest.cpp(137)]
```

With the product patch reversed the same test failed at the same assertion
(`test-6-red-control.log`, exit **8**); with it restored it passed (`test-7-green-restored.log`,
exit **0**, `1/1`). The LFO family passed `4/4` (`test-5-family.log`, exit **0**).

The gate table (`feature_check-2-final.log`, after the report; the earlier run is
`feature_check-1.log`) is `8/8 PASS`, `RESULT: PASS`, exit **0** (unpiped):
fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology,
doc-figures, complexity and file-length.

## Other findings

- `ControllerSurfaceTest::TemplatesSaveListReadAndApply()` fails in this environment at
  `tests/src/core/ControllerSurfaceTest.cpp:484` (`surface.saveTemplate()` returns false;
  it writes under `ConfigManager::userPresetsDir()`, which does not exist in this sandbox).
  It is unrelated to the LFO change (the same failure is recorded in
  `docs/qwen-fixes-2026-10/cluster-new-p21-90.md` and `cluster-new-p20-95.md` as a
  pre-existing environmental failure). The LFO-relevant controller tests
  (`PhaseDSidechainTest`, `UndoHonestyTest`, `MidiControllerMenuTest`,
  `AutomationPrepassProofTest`) all passed (`test-8-controller-tests.log`).

## Self-check

1. **The regression test FAILS with the product change reverted and PASSES with it.**
   `logs/fix.patch` holds only `include/LfoController.h` and `src/core/LfoController.cpp`.
   Applying it in reverse (`git apply -R`) and rebuilding gives `test-6-red-control.log`,
   exit **8**, at `LfoControllerUserWaveTest.cpp:137`. Restoring it gives
   `test-7-green-restored.log`, exit **0**. The first RED run before the fix was
   `test-1-red.log`, exit **8**.
2. **The test is deterministic.** It calls `Controller::triggerFrameCounter()` for each
   render period and checks the atomics directly; there is no audio thread, sleep, wall
   clock or race in the test. The held-sample check uses a NaN sentinel, which the noise
   generator never produces, so an unchanged value is unambiguous.
3. **The fix is at the cause and complete.** `updateSampleFunction()` had exactly two
   callers (the constructor's `Qt::DirectConnection` and the tail of `loadSettings()`);
   both are gone and the slot is no longer declared. `m_sampleFunction`/`m_heldSample` are
   now written only by the render boundary (`src/core/LfoController.cpp:127-134`) and by
   the render's own white-noise resample. `m_phaseOffset`/`m_currentPhase` are atomics and
   `updatePhase()` takes the engine change mutex. The p9-249 and p15-90 sibling tests
   still pass.
4. **No collateral damage.** `saveSettings()`/`loadSettings()` XML attributes are
   unchanged (the redundant `updateSampleFunction()` call was the only removal), so old
   projects load exactly as before. No new limit or shared-path check was added, and no
   control command or undo step changed. `zene` and the LFO family build and pass.
5. **ALREADY-FIXED / FALSE-POSITIVE do not apply.** The reversed-product-patch control
   fails at the assertion above, so the defect was reachable on this branch.
