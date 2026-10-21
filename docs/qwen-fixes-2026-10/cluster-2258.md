# Cluster 2258: sample-rate buffers, retired reclamation, saved range

2258: ALREADY-FIXED
new-p14-101: FIXED
new-p16-72: FALSE-POSITIVE

## Decisions and evidence

### 2258: ALREADY-FIXED (sample-rate changes no longer resize buffers during processing)

The finding's `m_sampleRateNeedsUpdate` flag and the `changeSampleRate()` call from the audio callback
no longer exist on this branch.  `processImpl()` only calls `applyPreparedBuffers()`
(`plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp:107`), which takes a prepared object with
an atomic exchange (`:326`), swaps the already-sized ring buffer into place (`:329`), copies the scalar
state, clears the pre-reserved grain vector (`:339`), and retires the prepared object (`:344`).  Nothing
there resizes, allocates, frees or clears the ring buffer.  The resize and zero-fill happen in
`sampleRateNeedsUpdate()` (`:285`, resize at `:295`), which is reached from the constructor (`:70`), from
the queued `AudioEngine::sampleRateChanged` connection (`:71-72`) and from
`GranularPitchShifterControls::updateRange()` (`plugins/GranularPitchShifter/GranularPitchShifterControls.cpp:104`).
That is the Q050-005 fix already on this branch (commit `b3a4d9d7c`).

RED evidence for this contract (scratch, not committed): I restored the pre-Q050-005 product files with
`git show b3a4d9d7c^:plugins/.../GranularPitchShifterEffect.{cpp,h}`, rebuilt, and ran the test.  It failed
at the allocation assertion with `Actual (state.callbackAllocations): 1` vs `Expected: 0` (exit 1).  After
restoring the current files and rebuilding, the same test passed (exit 0).  The regression now also records
`callbackFrees` and asserts zero frees on the render thread.

### new-p14-101: FIXED (retired ring buffer is reclaimed without waiting for another update)

The audio thread retires the old ring buffer by storing the prepared object in a reusable slot and setting
`m_retiredPending` (`plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp:344-345`).  A control-thread
`QTimer` is created in the constructor (`:73-80`), named `granularRetiredBufferReclaimTimer`, and runs as a
1 s `Qt::VeryCoarseTimer`; its timeout calls `reclaimRetiredBuffersIfPending()` (`:349-354`), which clears the
flag and calls `reclaimRetiredBuffers()`.  The only delete of a retired object is in that function
(`:356-361`).  The timer is on the control thread, so the free never runs inside `processImpl()`; the
sample-rate update path still reclaims as well, so the change only adds a second, eventual reclaimer.

The regression is `GranularPitchShifterRealtimeTest::retiredBuffersAreReclaimedWithoutAnotherUpdate`.  It
loads the 120 s range, renders a block so the previous 5 s ring is retired, finds the named timer, stops it,
and emits its `timeout` directly (no wall-clock waiting), counting `operator delete` through the allocation
probe.  It requires one reclaim on the first tick and zero on the second.  This fails without the product
change and passes with it (see the RED/GREEN table).  The old zero-interval spinning timer is replaced by the
1 s coarse timer; the test asserts the interval is positive.

### new-p16-72: FALSE-POSITIVE (a saved range cannot index past RangeSeconds)

`m_rangeModel` is a `ComboBoxModel`; its constructor adds exactly five items
(`plugins/GranularPitchShifter/GranularPitchShifterControls.cpp:58-62`).  `ComboBoxModel::addItem()` calls
`setRange(0, m_items.size() - 1)` (`src/core/ComboBoxModel.cpp:35`), so its range is 0..4.  Loading the range
from a project goes through `AutomatableModel::loadSettings()` -> `setValue()`
(`src/core/AutomatableModel.cpp:342`), and `setValueInternal()` stores `m_value = fittedValue(value)`
(`:415`); `fittedValue()` clamps to `[m_minValue, m_maxValue]` (`:502-516`).  A malformed `value="99"`
therefore arrives as 4, and a negative value as 0, before `RangeSeconds[range]` is indexed at
`GranularPitchShifterEffect.cpp:290`.  The round-1 clamp on top of that was dead code (and its test only
did `QVERIFY(true)`), so it is removed.  The evidence test
`savedOutOfRangeValueIsBoundedByTheControlModel` loads 99, asserts the control model reports value 4 and
max 4, then renders a block; it passes with and without a clamp, exactly as a false positive should.  No
product change is made for this item.

## RED/GREEN proof (new files are logs in /tmp/2258.r2-logs; see environment note)

| Step | Command | Exit |
|------|---------|------|
| Build final targets | `CCACHE_TEMPDIR=/tmp/ccache-tmp-2258 cmake --build build --target zene granularpitchshifter GranularPitchShifterRealtimeTest` | 0 |
| Full job product patch | `git diff b61e5f965 -- plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp plugins/GranularPitchShifter/GranularPitchShifterEffect.h > /tmp/2258.r2-logs/fix.patch` | 0 |
| Revert it | `git apply -R /tmp/2258.r2-logs/fix.patch` | 0 |
| Rebuild without fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-2258 cmake --build build --target granularpitchshifter GranularPitchShifterRealtimeTest` | 0 |
| Run RED | `cd build/tests && QT_QPA_PLATFORM=offscreen ./GranularPitchShifterRealtimeTest` | 1 |
| Restore fix | `git apply /tmp/2258.r2-logs/fix.patch` | 0 |
| Rebuild with fix | same build command | 0 |
| Run GREEN | same test command | 0 (5 passed, 0 failed) |

Round-2-only reverse patch (literal `git diff` of the working product files) was also checked:
`git diff -- plugins/...cpp plugins/...h > fix-round2.patch`, `git apply -R` exit 0, build exit 0,
test exit 1 at `retiredBuffersAreReclaimedWithoutAnotherUpdate` because the named timer is absent;
`git apply` restore exit 0, build exit 0, test exit 0.

The 2258 scratch RED used `git show b3a4d9d7c^:...` for both product files, build exit 0, test exit 1 with
`callbackAllocations` 1 vs 0; restoring the current files, build exit 0, test exit 0.

## Affected-family tests

`cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^(GranularPitchShifterRealtimeTest|GranularPitchShifterEffect|GranularPitchShifterControls)$' --output-on-failure`
matched and ran 1 test (the only granular test registered in this build tree) and passed 1/1, exit 0
(`test-5-family-green.log`, `test-12-final.log`).  The QtTest binary itself ran 5 functions, 5 passed, 0 failed
and was repeated three times with the same result (`test-10-repeat.log`).

## Gates

`PYTHONPATH=/tmp/2258-site python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exit 0:

```
PASS  fork-sources / all-sources-reproduce / no-upstream-regression / check-namespace /
      no-tautology / doc-figures / complexity / file-length
RESULT: PASS
```

The direct invocation exits 1 before printing the table because this session cannot write the script's
default log directory (see below); the gate logic itself is unchanged and the redirected run above is the
authoritative PASS.  `PYTHONPATH=/tmp/2258-site` only redirects `Path.open`/`Path.mkdir` for the unwritable
`<worktree parent>/*-feature-check` and run-log paths to `/tmp`; it does not alter gate inputs or results.

## Environment note: log folder

The requested LOG FOLDER `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/2258.r2/logs` is read-only to
this session: it is owned by uid 1000 with mode 0755, but `touch .../logs/probe` returns
`EXIT=1`/`Permission denied`, and the `write` tool returns EACCES as well.  Every build/test/gate log for this
round is therefore a new numbered file in `/tmp/2258.r2-logs` (same names and exit codes recorded here).
This is an environment restriction, not a skipped log.

## Files committed

- `plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp`
- `tests/src/core/GranularPitchShifterRealtimeTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-2258.md`

Product/test commit: `1d29c8b3b`.  This report is committed separately.

## Self-check

- Each new test FAILS with the product change reverted: `retiredBuffersAreReclaimedWithoutAnotherUpdate`
  exits 1 with the full job product patch reversed (no timer) and with the round-2-only patch reversed
  (named timer absent), and exits 0 restored.  The 2258 handoff test exits 1 with Q050-005 reversed
  (`callbackAllocations` 1 vs 0) and 0 restored.  new-p16-72 has no product change; its evidence test passes
  both ways by design.
- Determinism: the reclamation test injects the timer's `timeout` signal directly (no sleeps, no wall-clock
  wait) and counts frees through the allocation probe; the 2258 test uses fixed 128-frame blocks, a fixed-rate
  test device and injected sample rates.  The only deadlines are completion bounds for the render thread, and
  the only clock assertion is the existing 100 ms callback bound, not a correctness input.
- Cause/complete: `sampleRateNeedsUpdate()` remains the sole producer of prepared state (constructor,
  queued sample-rate change, range change); `applyPreparedBuffers()` is the sole audio-thread consumer and
  only swaps; `reclaimRetiredBuffers()` is the sole deleter and now has three control-thread callers
  (update, timer, destructor).  The only `RangeSeconds` index is `sampleRateNeedsUpdate():290`, whose value
  is bounded by the ComboBoxModel's 0..4 range.
- No collateral damage: valid ranges 0..4 produce the same five ring lengths; the removed clamp only ever
  sat on values the model had already clamped; no save format, undo, command or control-command change; the
  audio thread gains only an atomic store in `applyPreparedBuffers()`, and the timer/free run on the control
  thread.  Old projects with a malformed range load as before (clamped by the model) and render.
- ALREADY-FIXED (2258): `GranularPitchShifterEffect.cpp:107` calls only `applyPreparedBuffers()`; `:285-295`
  builds/resizes off the audio thread; `:313-345` swaps/retires without resizing.  FALSE-POSITIVE
  (new-p16-72): `GranularPitchShifterControls.cpp:58-62` + `ComboBoxModel.cpp:35` bound the model to 0..4,
  and `AutomatableModel.cpp:342,415,502-516` clamp the saved value.
