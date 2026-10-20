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

Ported `fb31e07f2` by hand and reworked per review. `MalletsSynth` is now
default-constructible and re-armable (`init`/`armModalBar`/`armTubeBell`/
`armBandedWG`/`resetDelay`). A shared, sample-rate-keyed `MalletsVoicePool`
of `PlayHandle::MaxNumber` (1024) preconstructed voices per STK family
(ModalBar, TubeBell, BandedWG) is built off the audio thread; that capacity
covers the engine's supported concurrency across all Mallets instances (at most
`PlayHandle::MaxNumber` note play handles in flight). The render thread claims a
slot with a non-blocking compare-exchange (`MalletsVoicePool::acquire`), arms
the prepared voice in place, and stores the slot in
`NotePlayHandle::m_pluginData`. An exhausted pool (impossible for
engine-supported concurrency) renders silence without allocation or locking.
`deleteNotePluginData()` returns the slot with an atomic store, no
deallocation. The pool is refreshed on `AudioEngine::sampleRateChanged`.

## Regression tests

- `tests/src/plugins/PluginPortsMigrationTest.cpp::migratedPluginsPreserveBehaviour()`
  now watches `malletsstk` as well as `freeboy`/`tripleoscillator`, asserting
  zero allocations/deallocations during render and sample-exact output (sha256
  `590b443f...26339cf`).
- `tests/src/plugins/PluginPortsMigrationTest.cpp::malletsPoolCoversEngineConcurrency()`
  starts 200 simultaneous ModalBar notes across two Mallets instances and
  verifies every note claims a prepared voice with zero render-thread
  allocations and deallocations.
- `tests/rt-safety-scope.txt` declares the new render-path symbols.
- `tests/upstream-modifications.txt` records both modified files.

## Evidence table

| Check | Exact command and result |
| --- | --- |
| RED before fix (migration) | `cmake --build build --target PluginPortsMigrationTest -j4` **EXIT=0**; `./PluginPortsMigrationTest migratedPluginsPreserveBehaviour` from `build/tests` output to `.../test-red.log` **EXIT=1**; `malletsstk render allocations=33, deallocations=32`. |
| GREEN after fix (migration) | same build **EXIT=0**; same test output to `.../test-green2.log` **EXIT=0**; `Totals: 3 passed, 0 failed`. |
| NEW test GREEN | `./PluginPortsMigrationTest malletsPoolCoversEngineConcurrency` output to `.../test-green-final.log` **EXIT=0**; `Totals: 3 passed, 0 failed`. |
| Reversed-fix proof (new test) | `git apply -R .../fix.patch` **EXIT=0**; rebuild `malletsstk`+`PluginPortsMigrationTest` **EXIT=0**; `./PluginPortsMigrationTest malletsPoolCoversEngineConcurrency` output to `.../test-rev.log` **EXIT=1**; `render allocations=5414, deallocations=5413`. |
| Restored-fix proof | `git checkout -- plugins/Stk/Mallets/Mallets.cpp plugins/Stk/Mallets/Mallets.h` then manual re-apply; rebuild **EXIT=0**; same test output to `.../test-green-final.log` **EXIT=0**. |
| Affected family | `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` from `build/tests` output to `.../test-family-final2.log` **EXIT=0**, 1/1 CTest target passed. |
| RT-safety sweep | `ctest -R 'RtSafety' --output-on-failure` from `build/tests` output to `.../test-rt-final.log` **EXIT=0**, RtSafetySweep + RtSafetySelfTest passed. |
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

- **Regression test FAILS reverted / PASSES restored (exit codes):** migration RED `.../test-red.log` **EXIT=1** (`malletsstk render allocations=33, deallocations=32`); new test reversed `.../test-rev.log` **EXIT=1** (`render allocations=5414, deallocations=5413`); restored `.../test-green-final.log` **EXIT=0**.
- **Deterministic (no timing/sleeps/races):** uses `partc::RealtimeAllocationCounts`/`RealtimeAllocationWatch` injection seam; 200 notes constructed up front; no wall-clock timing.
- **Fix at cause and complete:** all `MalletsSynth` callers (`playNoteImpl`, `deleteNotePluginData`) checked; capacity now `PlayHandle::MaxNumber`; sample-rate refresh preserved; no other voice construction.
- **No collateral damage:** sample-exact output unchanged; pool sized to engine-supported concurrency; old projects load unchanged; no save format change.
- **ALREADY-FIXED/FALSE-POSITIVE N/A:** verdict FIXED.
