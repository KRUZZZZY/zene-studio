FIXED

# cluster new-p9-3 - Parameter rescan left audio readers with deleted model pointers

## Verdict

REAL, and fixed at the cause in `plugins/ClapEffect/ClapParameter.cpp` and
`plugins/ClapEffect/ClapParameter.h`.

The finding is right about the defect and slightly off about which reader is on the
audio thread. `ClapInstrument::perNoteTarget()` is documented and called as a
**control-thread** entry point (`include/Instrument.h:200-204` says "Control thread";
its only caller is `src/core/ModulationLayer.cpp:261` inside `resolveRoute()`, which
runs from `Song::restorePublisherBackedSection` and `editModulationLayer`, not from
`renderNextPeriod`). The *model* it walks, however, is also read by the render thread:
`AutomatableModel` objects register with the audio engine (`AudioEngine::registerAutomatableModel`)
and the render thread reads their values once per period. So the two windows named by
the finding are both real:

1. `ClapParamModelList::sync()` called `removeAllBut()` (which deleted removed models
   inside `requestChangeInModel()`/`doneChangeInModel()`) and only afterwards assigned
   `m_models = std::move(next)` (`plugins/ClapEffect/ClapParameter.cpp:131-132` before
   the fix). Between `doneChangeInModel()` and that assignment the published list still
   named the freed model; `ClapEffectControls::paramModels()` / `modelFor()` and the
   control thread's `perNoteTarget()` read that vector. The vector itself was also
   republished with no guard, so a concurrent reader of `models()` could read a torn
   vector header, and the window overlapped a real deletion.
2. `ClapParamModel::setDescriptor()` (the kept-model path, `sync()` line 126 before the
   fix) rewrote the model's range via `setRange()` **before** the guard was ever taken.
   The render thread may be automating that parameter, and `setDescriptor()`'s own
   comment says so (`ClapParameter.cpp:73-75`: "the model's, which the audio thread may
   be reading while it automates the parameter"). The `requestChangeInModel()` guard is
   exactly the engine's mechanism for excluding the render thread for a whole period
   (`AudioEngine::renderNextPeriod()` holds `m_changeMutex` for the entire period,
   `src/core/AudioEngine.cpp:1220`; `requestChangeInModel()` takes it,
   `src/core/AudioEngine.cpp:1718-1731`).

`removeAllBut()` also took its own guard, and both `requestChangeInModel()` and the
model destructor's `AutomatableModel::~AutomatableModel()` guard are on a
`std::recursive_mutex` (`include/AudioEngine.h:888`), so nesting was safe - but it did
not cover publication or the range refresh.

## Fix

One guarded operation for the whole update, at the cause:

- `plugins/ClapEffect/ClapParameter.cpp:115-144`: `sync()` takes the engine's change
  guard once (RAII `requestChangesGuard()`, no-op when there is no engine) and holds it
  across the descriptor refresh (`setDescriptor`), creation of new models, publication
  of the new vector, and deletion of removed models.
- The list is published **before** anything is deleted:
  `std::vector<ClapParamModel*> previous = std::move(m_models); m_models = std::move(next);`
  then `removeAllBut(previous, m_models)` (`ClapParameter.cpp:139-143`). No reader can
  reach a removed model through `models()` at any point.
- `removeAllBut()` now takes the old list explicitly and no longer takes its own guard,
  because `sync()` holds it around the call (`ClapParameter.cpp:147-156`); this keeps
  the order "publish, then delete" possible.
- `plugins/ClapEffect/ClapParameter.h:93-103` and `:126-128` document the contract and
  the new signature; every caller was checked (`sync()` is the only caller of
  `removeAllBut()`; `ClapEffectControls` and `ClapInstrument` are the only users of
  `ClapParamModelList`, and both go through `sync()`).

Realtime: nothing was added to the audio thread. The guard acquisition happens on the
GUI/control thread in `ClapEffectControls::poll()` / `ClapInstrument::poll()` /
constructors. No allocation, free, lock, wait or log was added to a render path.

## Test

Added `ClapEffectIntegrationTest::testParameterRescanHoldsTheModelGuardAcrossTheWholeUpdate`
(`tests/src/plugins/ClapEffectIntegrationTest.cpp:96` declaration, `:790-841` body). It
is deterministic and uses the existing `AudioEngine::isChangeInModelHeld()` test seam
(the same technique as `tests/src/core/AudioPortsModelTest.cpp:548-590`):

- A direct-connected `Model::propertiesChanged` slot on the kept Gain model records
  `Engine::audioEngine()->isChangeInModelHeld()` at the moment `setRange()` runs inside
  `setDescriptor()`.
- A direct-connected `QObject::destroyed` slot on the removed Bypass model records
  whether the published `controls->paramModels()` still contains that pointer, and
  whether the guard is held at the deletion.
- The rescan is the fixture's existing full rescan (`CLAP_TEST_GAIN_RESCAN=all`, which
  removes Bypass and adds Drive/Tone) plus a new fixture switch
  `CLAP_TEST_GAIN_GAIN_MAX` (`tests/data/clap-test-plugin/clap-test-gain.c:155-161`) that
  makes Gain's range change to 0..2 on that rescan, so the kept-model range-refresh path
  is exercised too.

No threads, sleeps or wall-clock deadlines: each observation is made by the model's own
signal, emitted synchronously from inside the operation under test.

### RED (before the fix) - both defects observed

```
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-3/build/tests
ctest -R "^ClapEffectIntegrationTest$" --output-on-failure > .../logs/test-3-red.log 2>&1; echo EXIT=$?
EXIT=8

FAIL!  : ...testParameterRescanHoldsTheModelGuardAcrossTheWholeUpdate() 'guardAtRangeChange == 1' returned FALSE. (a parameter rescan changed a kept model's range outside the engine model-change guard)
FAIL!  : ...testParameterRescanHoldsTheModelGuardAcrossTheWholeUpdate() '!publishedWhenDestroyed' returned FALSE. (a parameter rescan deleted a model that the published model list still named)
```

(`guardAtDeletion == 1` already passed pre-fix: deletion was inside the guard, but the
published list still named the model.)

### GREEN (with the fix)

```
cd /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-3/build/tests
ctest -R "^ClapEffectIntegrationTest$" --output-on-failure > .../logs/test-4-green.log 2>&1; echo EXIT=$?
EXIT=0
100% tests passed out of 1
```

### Step 4b - the test catches the bug

```
git diff -- plugins/ClapEffect/ClapParameter.cpp plugins/ClapEffect/ClapParameter.h > .../logs/fix.patch
EXIT=0
git apply -R .../logs/fix.patch
EXIT=0
cmake --build build --target ClapEffectIntegrationTest zene -j4 > .../logs/build-7-reverted.log 2>&1
EXIT=0
ctest -R "^ClapEffectIntegrationTest$" --output-on-failure > .../logs/test-7-reverted.log 2>&1
EXIT=8        # FAILS without the product change (same two FAIL! lines as the RED run)
git apply .../logs/fix.patch
EXIT=0
cmake --build build --target ClapEffectIntegrationTest zene -j4 > .../logs/build-8-reapplied.log 2>&1
EXIT=0
ctest -R "^ClapEffectIntegrationTest$" --output-on-failure > .../logs/test-8-reapplied.log 2>&1
EXIT=0        # PASSES with it
```

Test matrix:

- normal path: the existing `testParameterRescansRebuildTheModels` (info rescan rename,
  full rescan keeps Gain, removes Bypass, adds Drive/Tone) - passes.
- failure/removal path: the new test's `CLAP_TEST_GAIN_RESCAN=all` (Bypass removed).
- reconfiguration path: the new fixture switch changes Gain's range to 0..2 during the
  same rescan, and the new test asserts the model keeps its identity and the new max.
- audio-thread constraints: the fix only adds guard acquisition on the control thread;
  the render thread path is untouched.
- supported platforms: no new platform-specific code; the test uses the existing
  offscreen Qt property of `ClapEffectIntegrationTest`.

## Whole CLAP family

```
cmake --build build --target ClapBusMapTest ClapHostTest ClapLoaderErrorTest ClapInstrumentViewTest ClapTestInstrumentUnitTest clapeffect clapinstrument clap-test-gain clap-test-instrument -j4
EXIT=0   (logs/build-5-clap-family.log shows the first target batch; logs/build-6-clap-family.log the remainder)

cd build/tests && ctest -R "Clap" --output-on-failure > .../logs/test-6-clap-family.log 2>&1; echo EXIT=$?
EXIT=0
100% tests passed out of 8   (ClapBusMapTest, ClapHostTest, ClapTestInstrumentStateTest,
                             ClapTestInstrumentEventCountTest, ClapTestInstrumentActivationCountersTest,
                             ClapLoaderErrorTest, ClapEffectIntegrationTest, ClapInstrumentViewTest)
```

Headers were touched (`ClapParameter.h` is moc'ed by both plug-in targets and compiled
into both CLAP test targets), so `zene`, `clapeffect`, `clapinstrument`,
`ClapEffectIntegrationTest` and `ClapInstrumentViewTest` were all rebuilt. No
`clap-test-gain` fixture existed in the build tree at first, which made the first
`ctest` run report "1 skipped" - that skip is not a pass; the fixture was then built
explicitly and every later run executed the cases.

## Gates

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-3 > .../logs/gates-1.log 2>&1; echo EXIT=$?
EXIT=0
RESULT: PASS
  PASS  fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace,
        no-tautology, doc-figures, complexity (no new item in your files),
        file-length (no new item in your files)
```

Registration: `plugins/ClapEffect/ClapParameter.{h,cpp}` are already listed in
`tests/fork-sources.txt` (`:1520-1521`); no product file was added, and no test
registration or CMake flag changed.

## Concurrency note

Local TSan does not run on this box, so the race was reasoned about, not raced: the
render thread holds `m_changeMutex` for the whole period (`AudioEngine.cpp:1220`), so
`requestChangesGuard()` on the control thread excludes it for the entire guarded region;
the published-vector check is an ordering invariant (signal emitted inside the
operation), which is exactly what the test measures without a race.

## Self-check

- **The regression test fails with the product change reverted and passes with it (exit
  codes).** Yes: `logs/test-7-reverted.log` EXIT=8 (after `git apply -R fix.patch`,
  build EXIT=0) and `logs/test-8-reapplied.log` EXIT=0 (after `git apply fix.patch`,
  build EXIT=0). The RED run with the change never applied is `logs/test-3-red.log`
  EXIT=8. Both FAIL! lines are quoted above.
- **The test is deterministic: no wall-clock timing, sleeps or races.** Yes. It uses the
  `AudioEngine::isChangeInModelHeld()` test seam and two direct-connected model signals
  (`Model::propertiesChanged`, `QObject::destroyed`) emitted synchronously from inside
  the operation under test; no threads are started and no timer or sleep is used. The
  rescan is driven by the fixture's environment switch and a direct `poll()` invocation,
  not by the 100 ms poll timer.
- **The fix is at the cause and complete: callers and siblings checked.** `sync()` is the
  only caller of `removeAllBut()` (verified with `rg -n "removeAllBut"`); the only users
  of `ClapParamModelList` are `ClapEffectControls` and `ClapInstrument`, and both reach
  it only through `sync()`, so both get the guarded path. The VST3 sibling has its own
  `perNoteTarget` and model handling and did not share this list or this defect. The
  header declaration and definition changed together, and every target compiling them
  (`clapeffect`, `clapinstrument`, `zene`, `ClapEffectIntegrationTest`,
  `ClapInstrumentViewTest`) was rebuilt and its tests run (`logs/build-5/6`,
  `logs/test-6-clap-family.log` EXIT=0, 8/8).
- **No collateral damage.** No save, load, undo or command path changed: the descriptor
  values written by `saveValues()`, the aliases keyed by `objectName()` and the models
  kept across a rescan are untouched; the model identity, values and ranges after the
  rescan are asserted by the new test and by the existing
  `testParameterRescansRebuildTheModels`. No new limit or check was added, so no other
  caller can be rejected. The only ordering change is "publish before delete", which is
  invisible to the save file.
- **Not an ALREADY-FIXED / FALSE-POSITIVE verdict**, so the fifth ask does not apply;
  the verdict is FIXED with the RED evidence above.
- **Sibling commands** (`app.*`, `arrangement.*`, `control.*`): this change builds no
  command, adds no command id and touches no `ControlCommands*` or
  `ControlReversibilityTable*` file, so no sibling command is affected and no
  reversibility class changed. The list `submit_check.py` prints comes from the branch's
  merge-base against the train, which includes the merged lanes' files, not from this
  fix; the fix's own files are the two `plugins/ClapEffect/ClapParameter` product files,
  the test source, the CLAP test fixture and this report.

## Other findings

- The finding names `perNoteTarget()` as "the audio path"; it is documented as a control
  thread entry point (`include/Instrument.h:200-204`). The defect is real because the
  model objects it walks are render-thread-visible and the vector was republished
  unguarded, but the report text should not be copied as-is.
- `plugins/AuEffect/AuParameter.cpp:51-65` builds its model list once and guards
  nothing; `plugins/CarlaVst2Effect/CarlaVst2Effect.cpp:250-251` deletes models in a
  rebuild loop. Neither was investigated as part of this bug and neither is changed
  here; they may deserve their own finding.
