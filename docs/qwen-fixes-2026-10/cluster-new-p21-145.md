FIXED

# Cluster new-p21-145

`EnvelopeAndLfoParameters::fillLevel()` dereferenced the published snapshot
returned by `acquireAudioState()` without checking it for null. Every other
accessor of the same snapshot (`holdAudioState()`, `PAHD_Frames()`,
`releaseFrames()`, `getLfoPredelayFrames()`, `getLfoAttackFrames()`,
`getLfoOscillationFrames()`, `getLfoUserWave()`) already treats a null return
as the "no snapshot published" case, and the destructor deliberately exchanges
the pointer to null so that a late reader observes that case instead of a
dangling pointer. `fillLevel()` was the one caller that skipped the check and
dereferenced null (`*state` and `state->...`). The audio path now fills the
whole period with a defined level (silence), gives the reader claim back, and
returns. This runs only when no snapshot is published; the normal path is
unchanged.

## Verdict evidence

- REAL, before the fix: `src/core/EnvelopeAndLfoParameters.cpp:338` took the
  snapshot and `:340` passed `*state` to `fillLfoLevel()`, then `:342-343`
  read `state->pahdEnv` / `state->rEnv`, with no null check in between.
- The null is produced on purpose: `src/core/EnvelopeAndLfoParameters.cpp:202`
  in the destructor is `delete m_audioState.exchange(nullptr);`.
- The intended contract is visible in the siblings, which all guard the same
  return value: `src/core/EnvelopeAndLfoParameters.cpp:707` (`holdAudioState`),
  `:746`, `:758`, `:770`, `:782`, `:794` (the frame accessors),
  `:807` (`getLfoUserWave`). `fillLevel()` was the single undefined-behaviour
  exception.
- The buffer must still be written: `InstrumentSoundShaping::volumeLevel()`
  declares `float level;` and relies on `fillLevel()` writing one sample
  (`src/core/InstrumentSoundShaping.cpp:99-100`), so the fix fills the period
  instead of returning with the caller's buffer untouched.

## The fix

`src/core/EnvelopeAndLfoParameters.cpp:341-352`:

```cpp
if( state == nullptr )
{
    for( f_cnt_t offset = 0; offset < _frames; ++offset )
    {
        _buf[offset] = 0.0f;
    }
    releaseAudioState();
    return;
}
```

No allocation, lock, wait or log was added; the branch does not touch the LFO
shape cache. `include/EnvelopeAndLfoParameters.h:196-200` declares the test
seam `detachAudioState()`, which takes the published snapshot away exactly as
the destructor does (`src/core/EnvelopeAndLfoParameters.cpp:730-738`), so the
null path is reachable deterministically without a race, a sleep or a real
teardown.

## Test matrix

| Path | Covered by |
| --- | --- |
| Normal | existing slots in `EnvelopeAndLfoParametersTest` (fillLevel with a published snapshot, shape cache, user wave) plus the new slot's final republish step |
| Failure (the bug) | `fillLevelWithoutAPublishedSnapshotFillsSilence`: `detachAudioState()`, then `fillLevel()` must fill silence, not dereference null |
| Exhaustion | same slot: `fillLevel(..., 0)` with no snapshot; claim count stays 0 |
| Reconfiguration | same slot: `updateSampleVars()` republishes, `holdAudioState()` succeeds, reader count returns to 0 |
| Audio-thread constraints | guard is branch + stores only; `feature_check` rt-safety gate PASS |
| Platforms | no platform-specific code; header change rebuilt `zene` and every built test that includes it |

## Exact commands and exit codes

Build and test logs are in
`/home/kruzzzzy/.config/kads/zene/state/runs/new-p21-145.r1/logs`.

| Phase | Exact command (run in the worktree unless noted) | Exit |
| --- | --- | ---: |
| RED build | `cmake --build build --target EnvelopeAndLfoParametersTest -j4 > .../logs/build-1.log 2>&1; echo EXIT=$?` | 0 |
| RED | `cd build/tests && ctest -R '^EnvelopeAndLfoParametersTest$' --output-on-failure > .../logs/test-1-red.log 2>&1; echo EXIT=$?` | 8 |
| GREEN build | `cmake --build build --target EnvelopeAndLfoParametersTest -j4 > .../logs/build-2.log 2>&1; echo EXIT=$?` | 0 |
| GREEN | `ctest -R '^EnvelopeAndLfoParametersTest$' --output-on-failure > .../logs/test-2-green.log 2>&1; echo EXIT=$?` | 0 |
| App rebuild (header) | `cmake --build build --target zene -j4 > .../logs/build-3-zene.log 2>&1; echo EXIT=$?` | 0 |
| Family build | `cmake --build build --target EnvelopeGraphTest LfoGraphTest ModulationRateTest ModulationLayerTest -j4 > .../logs/build-4-family.log 2>&1; echo EXIT=$?` | 0 |
| Family test | `ctest -R '^(EnvelopeAndLfoParametersTest\|EnvelopeGraphTest\|LfoGraphTest\|ModulationRateTest\|ModulationLayerTest)$' --output-on-failure > .../logs/test-4-family.log 2>&1; echo EXIT=$?` | 0 |
| Save product patch | `git diff -- src/core/EnvelopeAndLfoParameters.cpp include/EnvelopeAndLfoParameters.h > .../logs/fix.patch` | 0 |
| Reverse patch | `git apply -R .../logs/fix.patch; echo EXIT=$?` | 0 |
| Reverse build | `cmake --build build --target EnvelopeAndLfoParametersTest -j4 > .../logs/build-5-red-after-reverse.log 2>&1; echo EXIT=$?` | 0 |
| Reverse RED | `ctest -R '^EnvelopeAndLfoParametersTest$' --output-on-failure > .../logs/test-5-red-after-reverse.log 2>&1; echo EXIT=$?` | 8 |
| Reapply patch | `git apply .../logs/fix.patch; echo EXIT=$?` | 0 |
| Reapply build | `cmake --build build --target EnvelopeAndLfoParametersTest -j4 > .../logs/build-6-green-reapply.log 2>&1; echo EXIT=$?` | 0 |
| Reapply GREEN | `ctest -R '^EnvelopeAndLfoParametersTest$' --output-on-failure > .../logs/test-6-green-reapply.log 2>&1; echo EXIT=$?` | 0 |
| Final app rebuild | `cmake --build build --target zene -j4 > .../logs/build-7-zene.log 2>&1; echo EXIT=$?` | 0 |
| Gates | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > .../logs/feature-check-1.log 2>&1; echo EXIT=$?` | 0 |

RED output (`test-1-red.log:14-15`): `Received signal 11 (SIGSEGV), code
SEGV_MAPERR ... accessing address 0x000000000000006c` in
`fillLevelWithoutAPublishedSnapshotFillsSilence`. The reverse-patch run
(`test-5-red-after-reverse.log:14`) shows the same SIGSEGV. GREEN runs report
`100% tests passed`.

Note: `fix.patch` contains only the `fillLevel()` guard because the test seam
was committed separately first (`test-seam(new-p21-145)`), which is what makes
the reverse-patch RED a behavioural failure (SIGSEGV) rather than a build
failure.

## Gates

`feature_check.py` printed `RESULT: PASS` with exit 0; all eight gates PASS
(`fork-sources`, `all-sources-reproduce`, `no-upstream-regression`,
`check-namespace`, `no-tautology`, `doc-figures`, `complexity`, `file-length`).
Registration for this change is in `tests/upstream-modifications.txt`
(`src/core/EnvelopeAndLfoParameters.cpp` and
`include/EnvelopeAndLfoParameters.h`, reason naming this cluster and the
regression slot).

## Self-check

- **? / FAIL items:** none were reported by the completion check; the table
  above is pasted from `feature-check-1.log`.
- **Test is deterministic:** the null state is injected through
  `detachAudioState()`; no thread, sleep, timing or wall clock is used.
- **No collateral damage:** the guard executes only when
  `acquireAudioState()` returns null. With a published snapshot the code path
  is byte-for-byte the previous one; save/load formats are untouched.
- **Realtime:** the new branch does not allocate, free, lock, wait or log.
- **Registration:** both changed inherited files are listed with a reason.
- **Undo/commands:** no control command, undo step or command snapshot was
  touched.

## Other findings

- `~EnvelopeAndLfoParameters()` (`src/core/EnvelopeAndLfoParameters.cpp:202-207`)
  exchanges the snapshot to null and then deletes the current and retired
  snapshots without waiting for `m_audioStateReaders` to drain. A reader that
  claimed a snapshot just before the destructor can therefore hold a freed
  snapshot. That is a separate teardown-lifetime issue outside this finding;
  it is not changed here.
- `InstrumentSoundShaping::volumeLevel()` leaves `float level;` uninitialised
  if `fillLevel()` ever fails to write. The fix keeps `fillLevel()` writing
  every sample, so the existing contract holds; the uninitialised declaration
  was not changed.
