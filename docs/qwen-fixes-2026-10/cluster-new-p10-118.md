FIXED

# Cluster new-p10-118 - Advancing LFOs takes a mutex

The bug is real on this branch, and it had a sibling the finding did not name: the
per-period LFO *reset* took the same mutex and walked the same list, also on the audio
thread. Both now advance a phase counter instead.

## The defect (code evidence, read this session)

- `src/core/EnvelopeAndLfoParameters.cpp:57-65` before the fix (`:52` in the finding;
  the line moved). `LfoInstances::trigger()` opened with
  `QMutexLocker m( &m_lfoListMutex )` and then walked `m_lfos`, adding
  `Engine::audioEngine()->framesPerPeriod()` to every LFO's frame. Two hazards, both
  stated by the finding: a mutex on the audio thread, and work that grows with the
  number of LFO instances.
- Called on the audio thread: `src/core/AudioEngine.cpp:1210`
  `EnvelopeAndLfoParameters::instances()->trigger();`, at the end of the render, after
  `emit nextAudioBuffer` (STAGE 3).
- Sibling path, same defect: `LfoInstances::reset()` (was
  `src/core/EnvelopeAndLfoParameters.cpp:70-78`) locked `m_lfoListMutex` and walked
  `m_lfos` setting every frame to 0, and `Song::processNextBuffer` calls it per period
  on the audio thread while the play position is 0: `src/core/Song.cpp:526-529`
  (`Song::processNextBuffer` is STAGE 0). Fixing only `trigger()` would have left the
  audio thread taking the very same mutex.
- The tree's own ledger recorded the debt at HEAD:
  `tests/rt-safety-allowlist.txt:24`
  `src/core/EnvelopeAndLfoParameters.cpp  EnvelopeAndLfoParameters::LfoInstances::trigger  lock-guard  1`
  ("inherited upstream LMMS ... the fix-up pass's to remove").
- Measured, not assumed. With the pre-fix product code restored and HEAD's ledger and
  scope files, the sweep reports `by rule : allocation 0, locking 2, growth 2`
  (`logs/sweep-3-red-lockcount.log`, EXIT=0). With the fix it reports
  `locking 1` (`logs/sweep-1.log`, EXIT=0); the remaining one is
  `AudioEngine::renderNextPeriod`, a different job.

## The fix

`include/EnvelopeAndLfoParameters.h:266-274` and
`src/core/EnvelopeAndLfoParameters.cpp:47-78, 265-277`:

- Two class-static relaxed counters replace the per-instance frame write:
  `s_frameCounter` (frames rendered so far) and `s_frameCounterAtReset` (the value the
  last LFO reset happened at).
- `LfoInstances::trigger()` (`:59-66`) is now one `fetch_add` of the period's frames.
  No lock, no list access, O(1) whatever the instance count.
- `LfoInstances::reset()` (`:71-78`) is now one `load` and one `store`. No lock, no list
  access, no growth.
- `EnvelopeAndLfoParameters::lfoFrame()` (`:265-277`) is the LFO's phase: the distance
  from its own origin, which is `max(m_frameCounterOrigin, s_frameCounterAtReset)`,
  where `m_frameCounterOrigin` is `s_frameCounter` at the LFO's construction
  (`:123`). Three relaxed loads and a branch.
- The readers of the phase use it: `updateLfoShapeData()` (`:284`) and the shape-cache
  key test in `fillLfoLevel()` (`:326-328`), which previously compared
  `m_lfoShapeDataFrame` against `m_lfoFrame`.

Why the semantics are unchanged for every LFO the engine can see:

- A registered LFO's frame still grows by `framesPerPeriod` per period, because the
  counter grows by exactly that and the origin is fixed. `f_cnt_t` is `uint64_t`
  (`include/LmmsTypes.h:43`), so the subtraction is the same arithmetic as the old
  per-instance accumulation.
- A LFO created mid-song still starts at phase 0: its origin is the counter at its
  construction.
- `reset()` still puts every older LFO back to phase 0: their origins are all below the
  new reset mark, so all of them measure from it.
- The mutex stays for `add()`/`remove()`, which are control-thread operations; it is
  no longer reachable from the audio thread.
- Nothing is persisted about the phase, so saved projects and old data are unaffected.

Concurrency reasoning (local TSan does not run on this box, so this is stated, not
measured): the audio thread is the only writer of both counters, and every read is a
relaxed load of a monotonically increasing value; a reader can see a counter older by
a period but never a torn or inconsistent one. The only cross-thread write is
`m_frameCounterOrigin` at construction (control thread), stored before the object is
reachable from the audio thread, and read only after that. A control-thread construction
that races a song-start reset at worst reads an origin one period old, which the `max`
against the reset mark absorbs.

## Test matrix

| path | what proves it |
| --- | --- |
| normal path: a registered LFO advances one period per `trigger()` | `EnvelopeAndLfoParametersTest::theLfoPhaseAdvancesWithoutARegistryWalk` (the `before`/`after` render pair) and the pre-existing LFO tests in the same binary |
| failure path of the finding: the advance must not be a registry walk | same test, with the instance taken out of the registry before the trigger: it fails on the pre-fix code, passes after |
| reset path (the sibling, on the audio thread) | `EnvelopeAndLfoParametersTest::theLfoResetPutsEveryLfoBackToTheSamePhase`; `RtSafetySweep` measures `LfoInstances::reset`, newly declared in `tests/rt-safety-scope.txt:60` |
| audio-thread constraints: no lock, no allocation, no growth | `RtSafetySweep` (ctest, and the standalone run in `logs/sweep-1.log`): `locking 1, allocation 0, growth 2`, the two growth hits belong to `Song::processNextBuffer` and are on the ledger; the LFO allowlist line is deleted, which is the ratchet going down |
| rate/render behaviour of the LFO fill | `ModulationRateTest`, `InstrumentTrackRenderPathTest` (allocation probe over an instrument's render path), `LfoGraphTest`, `EnvelopeGraphTest`, `SamplerEnvelopeTest`, `SfzFlexEnvelopeZeroStageTest`, `ClipGainEnvelopeTest`, `LfoControllerUserWaveTest`, `DelayLfoAutomationTest` |
| reconfiguration: speed/wave/amount changes still land | `EnvelopeAndLfoParametersTest` wave/shape-cache cases, `ModulationLayerTest`, `LfoControllerUserWaveTest` |
| platform build | no platform-specific code; the header change rebuilt `zene` and every test target that includes it (`logs/build-4-green.log`, `logs/build-6-green-restored.log`, both `BUILD_EXIT=0`) |

## Commands and exit codes

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p10-118.r1/logs`.

Baseline before any change (test binaries built first, `build-1-tests.log` EXIT=0):

```
cd build/tests && ctest -R 'EnvelopeAndLfoParametersTest|RtSafetySweep|RtSafetySelfTest|ModulationLayerTest' \
  --output-on-failure > LOGS/test-2-baseline.log 2>&1; echo EXIT=$?
EXIT=0
```

RED - the new test, product code unchanged (`test-3-red.log`):

```
cmake --build build --target EnvelopeAndLfoParametersTest -j4 > LOGS/build-3-red.log 2>&1; echo EXIT=$?
EXIT=0
cd build/tests && ctest -R 'EnvelopeAndLfoParametersTest' --output-on-failure > LOGS/test-3-red.log 2>&1; echo EXIT=$?
EXIT=8
FAIL!  : EnvelopeAndLfoParametersTest::theLfoPhaseAdvancesWithoutARegistryWalk() 'step > 1.0e-6f'
         returned FALSE. (the LFO phase did not advance by one period: the advance is a walk over
         the LFO registry, not a period counter the audio thread reads)
Totals: 10 passed, 1 failed, 0 skipped, 0 blacklisted, 495ms
```

GREEN - with the fix (`build-4-green.log` and `test-4-green.log`):

```
cmake --build build --target zene EnvelopeAndLfoParametersTest ModulationLayerTest -j4 \
  > LOGS/build-4-green.log 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0
cd build/tests && ctest -R 'EnvelopeAndLfoParametersTest|RtSafetySweep|RtSafetySelfTest|ModulationLayerTest' \
  --output-on-failure > LOGS/test-4-green.log 2>&1; echo TEST_EXIT=$?
TEST_EXIT=0
1/4 ModulationLayerTest Passed  2/4 EnvelopeAndLfoParametersTest Passed
3/4 RtSafetySweep Passed        4/4 RtSafetySelfTest Passed
```

The two new cases by name (`test-5-green-named.log`):

```
cd build/tests && ./EnvelopeAndLfoParametersTest theLfoPhaseAdvancesWithoutARegistryWalk \
  theLfoResetPutsEveryLfoBackToTheSamePhase > LOGS/test-5-green-named.log 2>&1; echo EXIT=$?
EXIT=0
PASS   : EnvelopeAndLfoParametersTest::theLfoPhaseAdvancesWithoutARegistryWalk()
PASS   : EnvelopeAndLfoParametersTest::theLfoResetPutsEveryLfoBackToTheSamePhase()
```

Step 4b - revert only the product files, rebuild, run (must fail), restore:

```
git diff -- include/EnvelopeAndLfoParameters.h src/core/EnvelopeAndLfoParameters.cpp > LOGS/fix.patch; echo EXIT=$?
EXIT=0
git apply -R LOGS/fix.patch; echo EXIT=$?
EXIT=0
cmake --build build --target EnvelopeAndLfoParametersTest -j4 > LOGS/build-5-red-reverted.log 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0
cd build/tests && ctest -R 'EnvelopeAndLfoParametersTest' --output-on-failure > LOGS/test-6-red-reverted.log 2>&1; echo TEST_EXIT=$?
TEST_EXIT=8
FAIL!  : EnvelopeAndLfoParametersTest::theLfoPhaseAdvancesWithoutARegistryWalk() 'step > 1.0e-6f' returned FALSE.
Totals: 10 passed, 1 failed, 0 skipped, 0 blacklisted, 484ms
```

The same reverted tree with HEAD's ledger shows the lock the finding names
(`sweep-3-red-lockcount.log`):

```
python3 tests/rt-safety-sweep.py --tree . --scope LOGS/head-scope.txt \
  --allowlist LOGS/head-allowlist.txt --check > LOGS/sweep-3-red-lockcount.log 2>&1; echo EXIT=$?
EXIT=0
  measured  : 4958 region line(s); 4 hit(s) in 3 key(s)
  by rule   : allocation 0, locking 2, growth 2, blocking-io 0
```

Restore, rebuild, rerun (must pass):

```
git apply LOGS/fix.patch; echo EXIT=$?
EXIT=0
cmake --build build --target zene EnvelopeAndLfoParametersTest ModulationLayerTest -j4 \
  > LOGS/build-6-green-restored.log 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0
cd build/tests && ctest -R 'EnvelopeAndLfoParametersTest|RtSafetySweep|RtSafetySelfTest|ModulationLayerTest' \
  --output-on-failure > LOGS/test-8-green-restored.log 2>&1; echo TEST_EXIT=$?
TEST_EXIT=0
```

The sweep after the fix (`sweep-1.log`) reports `locking 1` where the reverted tree
reports `locking 2`; `sweep-2-list-scope.log` shows the two newly declared paths
(`LfoInstances::reset`, `lfoFrame`) resolve to `ok`.

Wider runs:

```
# ctest -R 'Lfo|Envelope|Modulation|SoundShaping' --output-on-failure > LOGS/test-9-family.log
TEST_EXIT=8   (7 of the 13 binaries were not built yet: "Not Run", not failures)
cmake --build build --target LfoControllerUserWaveTest ModulationLayerValueTest ModulationAudioViewTest \
  ModulationLayerProjectRoundTripTest ClipGainEnvelopeTest SamplerEnvelopeTest SfzFlexEnvelopeZeroStageTest \
  -j4 > LOGS/build-8-family.log 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0
cd build/tests && ctest -R 'Lfo|Envelope|Modulation|SoundShaping' --output-on-failure > LOGS/test-10-family.log 2>&1; echo TEST_EXIT=$?
TEST_EXIT=0   100% tests passed out of 13
cmake --build build --target InstrumentTrackRenderPathTest -j4 > LOGS/build-9-renderpath.log 2>&1; echo BUILD_EXIT=$?
BUILD_EXIT=0
cd build/tests && ctest -R 'InstrumentTrackRenderPathTest' --output-on-failure > LOGS/test-11-renderpath.log 2>&1; echo TEST_EXIT=$?
TEST_EXIT=0
```

Gates (`gates-1.log`):

```
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/gates-1.log 2>&1; echo EXIT=$?
EXIT=0
  PASS  fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace / no-tautology / doc-figures
  PASS  complexity  no new item in your files
  PASS  file-length no new item in your files
RESULT: PASS
```

Registration: `include/EnvelopeAndLfoParameters.h` and
`src/core/EnvelopeAndLfoParameters.cpp` are inherited files, so both are recorded in
`tests/upstream-modifications.txt` (two appended lines), in the same commit. No new
product file, so `tests/fork-sources.txt` is untouched.

## Self-check

- **The regression test fails with the product change reverted and passes with it.**
  Fail: `test-6-red-reverted.log`, EXIT=8, `theLfoPhaseAdvancesWithoutARegistryWalk()
  'step > 1.0e-6f' returned FALSE`. Pass: `test-5-green-named.log`, EXIT=0, the same
  case `PASS`. The reverse patch is `logs/fix.patch`, taken with `git diff` over the two
  product files only.
- **The test is deterministic: no wall-clock timing, sleeps or races.** It uses no
  thread and no sleep. It injects nothing but the render inputs: identical
  `fillLevel()` calls before and after one explicit
  `EnvelopeAndLfoParameters::instances()->trigger()`, with the instance taken out of
  the registry in between, and the assertion is a phase step `> 1e-6` on the rendered
  floats (plus `QVERIFY(first.getLfoOscillationFrames() > 16 * frames)` so a step
  cannot be confused with a wrap). A background `AudioDummy` render can advance the
  counter too, and that cannot flip the result: the pre-fix code never advances an
  unregistered instance, and the post-fix code advances it by at least one period.
- **The fix is at the cause and complete: every caller and sibling checked.**
  `trigger()`'s only caller is `AudioEngine.cpp:1210`; `reset()`'s only caller is
  `Song.cpp:528` (which also runs on the audio thread, and is fixed in the same change);
  `add()`/`remove()`'s only callers are the constructor and destructor
  (`EnvelopeAndLfoParameters.cpp:139, 209`) and they keep the mutex. The two phase
  readers (`updateLfoShapeData`, `fillLfoLevel`'s cache key) were both converted to
  `lfoFrame()`; `rg m_lfoFrame` over `src include tests` returns nothing after the
  change. Both new/changed audio-thread paths are declared in
  `tests/rt-safety-scope.txt` so a later edit cannot silently reintroduce a lock there.
- **No collateral damage.** Nothing about the phase is saved, so projects load and
  render as before; the LFO family and an instrument render-path allocation test ran
  green (13/13 and 1/1 above). No limit or check was added anywhere, so no other caller
  can be rejected. The mutex is still there for the control-thread list edits, and the
  semantics for registered, newly created and reset LFOs are the ones the old
  accumulation gave (argued above; the reset case is pinned by
  `theLfoResetPutsEveryLfoBackToTheSamePhase`).
- **No undo, command or control-registry change:** no command file, no
  `ControlCommandsSnapshot` regeneration, no new product file.
- **No sibling command is affected.** This fix touches no command file, no
  `ControlRegistry`/`ControlCommands` source and no reversibility table, so the
  `control.*`/`app.*`/`arrangement.*` ids that `submit_check.py` lists as siblings come
  from earlier commits on this branch, not from this change: `git show --stat db0df4af0f`
  touches only the two `EnvelopeAndLfoParameters` files, one test file, the two
  rt-safety list files, `tests/upstream-modifications.txt` and this report. The
  `ControlCommandsSnapshot` canary is therefore untouched, and the fix adds no undo
  step and no user-visible command.
- **Verdict basis:** the finding's line numbers had moved but the code it describes was
  present at HEAD, and the ledger carried it as debt; both are cited above with the
  measurements that show the lock before and after.

## Other findings (not fixed here)

- `src/core/AudioEngine.cpp:1210` and `src/core/Song.cpp:528` call
  `EnvelopeAndLfoParameters::instances()->trigger()/reset()` without a null check, while
  `instances()` is null until the first `EnvelopeAndLfoParameters` exists and is set
  back to null when the last one is destroyed. That is a separate pre-existing hazard;
  it is not this finding and is not touched here. (As a side effect of this fix both
  bodies no longer dereference `this`, but the call itself remains undefined behaviour.)
- `LfoInstances::isEmpty()` reads `m_lfos` without the mutex from the destructor
  (`src/core/EnvelopeAndLfoParameters.cpp:211-216`), which is a pre-existing
  control-thread race, not related to the audio thread.
- `src/core/AudioEngine.cpp`'s render lock (`m_changeMutex` in `renderNextPeriod`) is the
  remaining `lock-guard` on the sweep's ledger; it is another cluster's job, as that
  line's own reason states.
