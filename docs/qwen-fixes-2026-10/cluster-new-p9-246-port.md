# Cluster new-p9-246-port: Mallets allocates and locks on the audio thread

Verdict: **FIXED**

## Finding

`MalletsInstrument::playNoteImpl()` (plugins/Stk/Mallets/Mallets.cpp) built a
`MalletsSynth` with `new` on the render thread on the first period of every
note, inside a `static QMutex` critical section, and `deleteNotePluginData()`
deleted it again on the render owner. The constructor allocated the STK voice
and its delay buffer and called `Stk::setSampleRate`/`Stk::setRawwavePath`, so
the callback allocated, blocked on a lock and mutated shared global STK state.

## Fix

Ported `fb31e07f2` by hand. `MalletsSynth` is now default-constructible and
re-armable (`init`/`armModalBar`/`armTubeBell`/`armBandedWG`/`resetDelay`).
A shared, sample-rate-keyed `MalletsVoicePool` of 128 preconstructed voices per
STK family (ModalBar, TubeBell, BandedWG) is built off the audio thread; the
render thread claims a slot with a non-blocking compare-exchange
(`MalletsVoicePool::acquire`), arms the prepared voice in place, and stores the
slot in `NotePlayHandle::m_pluginData`. When all prepared voices are in use the
note renders silence via `noMalletsVoiceMarker`. `deleteNotePluginData()`
returns the slot with an atomic store, no deallocation. The pool is refreshed on
`AudioEngine::sampleRateChanged`.

## Regression test

`tests/src/plugins/PluginPortsMigrationTest.cpp` now watches `malletsstk` as
well as `freeboy`/`tripleoscillator` in `migratedPluginsPreserveBehaviour()`,
asserting zero allocations/deallocations during render. `tests/rt-safety-scope.txt`
declares the new render-path symbols. `tests/upstream-modifications.txt` records
both modified files.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before fix | `cmake --build build --target PluginPortsMigrationTest -j4 > .../build-red.log 2>&1` **EXIT=0**; `./PluginPortsMigrationTest migratedPluginsPreserveBehaviour` from `build/tests` output to `.../test-red.log` **EXIT=1**; `malletsstk render allocations=33, deallocations=32`. |
| GREEN after fix | `cmake --build build --target PluginPortsMigrationTest -j4 > .../build-green2.log 2>&1` **EXIT=0**; `./PluginPortsMigrationTest migratedPluginsPreserveBehaviour` from `build/tests` output to `.../test-green2.log` **EXIT=0**; `Totals: 3 passed, 0 failed` (44 gtest cases in ctest). |
| Reversed-fix proof | `git apply -R .../fix.patch` **EXIT=0**; rebuild **EXIT=0**; `./PluginPortsMigrationTest migratedPluginsPreserveBehaviour` output to `.../test-red.log` **EXIT=1**, `malletsstk render allocations=33, deallocations=32`. |
| Restored-fix proof | `git apply .../fix.patch` **EXIT=0**; rebuild **EXIT=0**; same test output to `.../test-green2.log` **EXIT=0**. |
| Affected family | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` from `build/tests` output to `.../test-family2.log` **EXIT=0**, 1/1 CTest target passed. |
| RT-safety sweep | `ctest -R 'RtSafety' --output-on-failure` from `build/tests` output to `.../test-rtsafety.log` **EXIT=0**, RtSafetySweep + RtSafetySelfTest passed. |
| Complexity gate | `python3 .../feature_check.py .` **EXIT=0**; `complexity` no new item in your files. |
| File-length gate | same run; `file-length` no new item in your files. |

All logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-246-port.r1/logs`.

## Files in this commit

- `plugins/Stk/Mallets/Mallets.cpp`
- `plugins/Stk/Mallets/Mallets.h`
- `tests/src/plugins/PluginPortsMigrationTest.cpp`
- `tests/rt-safety-scope.txt`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p9-246-port.md`

## Self-check

- **Regression test FAILS reverted / PASSES restored (exit codes):** RED `.../test-red.log` **EXIT=1** (`malletsstk render allocations=33, deallocations=32`); GREEN `.../test-green2.log` **EXIT=0**; reversed-fix proof re-run **EXIT=1**; restored-fix proof **EXIT=0**.
- **Deterministic (no timing/sleeps/races):** uses the `partc::RealtimeAllocationCounts` injection seam in `PluginPortsHarness` (counts C++ allocations around every instrument period and cleanup); no wall-clock timing.
- **Fix at cause and complete:** checked every caller of `MalletsSynth` (only `playNoteImpl`/`deleteNotePluginData`); `MalletsInstrument` now holds `m_voicePool`; sample-rate change rebuilds pool via `sampleRateChanged`; no other path constructs voices. Siblings (freeboy, tripleoscillator) unchanged.
- **No collateral damage:** render output sample-exact (sha256 `590b443f...26339cf` unchanged on both sides); pool capacity 128 covers note counts; sentinel renders silence when exhausted; old projects load unchanged (no save format change).
- **ALREADY-FIXED/FALSE-POSITIVE N/A:** verdict FIXED.
