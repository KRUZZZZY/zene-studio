FIXED

new-p9-130: FIXED

## Round 2: what the review asked for and what changed

Round 1 made the flag an atomic handoff and the reviewer accepted that, but sent the
change back for one unmet criterion: a sample-rate **increase** still resized the RMS
buffers inside the audio callback (`RmsHelper::setSize()` frees and allocates when the
window grows: `include/RmsHelper.h:84-90`), so "arrange any resource update safely outside
the processing path" was not satisfied. This round moves that work to the control thread
and keeps the atomic handoff:

- the sample-rate slot now builds the window the new rate needs, on the control thread;
- it publishes the prepared window by pointer, then announces the request (release store);
- the audio path consumes the request, installs the window with one pointer exchange and
  announces the bank it now runs on - it allocates nothing, frees nothing, locks nothing;
- the control thread only ever rebuilds a bank that is neither the one the audio path
  announced it runs on nor the one it published and has not had collected, so the replaced
  window is reused rather than leaked.

## Decision: REAL

The finding describes the code exactly, and its last clause is the part round 1 left
undone. Before this change:

- `plugins/DynamicsProcessor/DynamicsProcessorControls.cpp:64` wrote
  `m_effect->m_needsUpdate = true;` - a plain `bool`, on the control thread.
- `plugins/DynamicsProcessor/DynamicsProcessor.cpp:117-123` read that flag on the audio
  path, cleared it in a second access, **and called `RmsHelper::setSize()` there**: for a
  rate increase that is `delete[] m_buffer; m_buffer = new float[size]` inside
  `processImpl()` (`include/RmsHelper.h:79-91`).
- `plugins/DynamicsProcessor/DynamicsProcessor.h:65` declared `bool m_needsUpdate = false;`
  with no synchronisation of any kind.

Two defects follow. First, the control thread writes the flag while the audio thread reads
it: the engine's emitters currently quiesce the audio thread first - `setAudioDevice()` and
`restoreAudioDevice()` call `stopProcessing()` before `emit sampleRateChanged()`
(`src/core/AudioEngine.cpp:1420-1456`) and `AudioDevice::stopProcessing()` waits for the
open backend callbacks (`src/core/audio/AudioDevice.cpp:137-156`), while `initDevices()`
emits once before processing starts (`AudioEngine.cpp:812`) - so the race is latent rather
than observed in today's paths, but that ordering is an undocumented engine invariant, not
a contract the plugin states or can rely on. Second, the read-then-clear loses a request
that arrives between the two accesses, and the resize it triggers allocates on the audio
thread. Local TSan does not run on this box; this is a design argument, not a detector
report.

## Fix (at the cause)

Control side - everything that allocates:

- `plugins/DynamicsProcessor/DynamicsProcessorControls.cpp:62-67` - the slot calls
  `m_effect->prepareSampleRateChange()`; it no longer touches the flag directly.
- `plugins/DynamicsProcessor/DynamicsProcessor.cpp:97-129` - `prepareSampleRateChange()`
  picks a bank that is neither `m_currentPublished` nor `m_lastPublished`, builds or
  resizes it at the new window (the allocation site), then publishes it with a release
  store (`m_pendingBank`) and only after that announces the request with a release store
  (`m_needsUpdate`).
- `plugins/DynamicsProcessor/DynamicsProcessor.h:50-65` - `RmsBank`, the prepared pair of
  `RmsHelper` windows; `resize()` is the only place a window is (re)allocated.

Audio side - no allocation, no free, no lock:

- `plugins/DynamicsProcessor/DynamicsProcessor.cpp:173-185` - `processImpl()` takes the
  request with one `exchange`, takes the prepared bank with a second `exchange` and hands
  it to `adoptRmsBank()`; the RMS helpers it runs on are then read once per period
  (`RmsHelper * const rms[2] = { &m_currentBank->m_rms[0], &m_currentBank->m_rms[1] };`,
  `:198`).
- `plugins/DynamicsProcessor/DynamicsProcessor.cpp:132-138` - `adoptRmsBank()` sets the
  audio path's bank and announces it with one release store. Nothing is freed: the banks
  are owned by `m_banks` for the effect's life.
- `plugins/DynamicsProcessor/DynamicsProcessor.cpp:74-86` - the constructor builds the
  first bank off the audio thread; `~DynProcEffect()` (`:91-94`) has nothing left to free
  by hand.

Why the handoff is safe without a lock, in the terms of the C++ memory model: the control
thread's release store to `m_pendingBank` and its release store to `m_needsUpdate` are
ordered, and the audio thread's acquire exchange on `m_needsUpdate` makes both visible
before it reads the pointer - the window is fully built before it can be observed. The
pending pointer is taken by `exchange`, so exactly one side owns a published bank. The
audio thread announces its installed bank with a release store that the control thread
reads with acquire, so the control thread never rebuilds the bank the audio path is using;
the bank it published and has not collected is excluded by `m_lastPublished` on the same
thread. Three banks cover the steady state (current, in flight, transient), so the list is
bounded - the regression pins that.

## Test matrix

`tests/src/plugins/DynamicsProcessorSampleRateHandoffTest.cpp` (new target, plugin sources
compiled in; the allocation probe is linked through the tests' own
`lmms_link_allocation_probe`). The probe reads the installed window through
`appliedWindow()`, which resolves whichever member the compiled header declares
(`m_currentBank->m_rms`, or the historical `m_rms`), so the file builds against both and
the red run is a failed assertion, not a build error.

| Path | Test | Result |
| --- | --- | --- |
| The handoff (round 1, still pinned) | `theSampleRateRequestIsHandedOffThroughAnAtomicFlag` - an `std::atomic<bool>` trait probe on the declared member, then the real slot | PASS (it passes with the fix reversed too: the flag is round 1's change, not this round's) |
| Reconfiguration **and the review's criterion** | `aRateIncreaseIsAppliedWithoutAllocatingOrFreeingOnTheAudioPath` - 44.1 kHz -> 96 kHz through the real slot; the slot allocates on the control thread (`controlSide > 0`), the period that applies it allocates 0 and frees 0, the window becomes 139 and both coefficients are at 96 kHz, the request is consumed once | **FAIL before, PASS after** (2 allocations and 2 frees before) |
| Exhaustion / lifetime | `repeatedRateChangesReuseTheWindowsWithoutGrowing` - twelve changes (48k, 96k, 32k, 88.2k, 44.1k, 192k, 22.05k, 96k, 48k, 96k, 44.1k, 88.2k), every period allocation-free, the window correct each time, and the bank count stable and <= 4 | **FAIL before, PASS after** |
| Normal path | `processingWithoutARequestLeavesTheWindowAlone` - four idle periods: no request, no allocation, window and coefficients unchanged | PASS |
| Failure path | none in this code: the slot and the request have no error branch; the only failure surface is save/load of `waveShape`, covered by `dynamicsProcessorRejectsShortWaveShape` (green) | n/a |
| Audio-thread constraints | the install is one pointer exchange and one release store; `static_assert(std::atomic<bool>::is_always_lock_free)` in the test; `RmsHelper::setSize()` is no longer reachable from `processImpl()` at all | PASS (builds and runs) |
| Supported platforms | header and definition changed together; `zene`, the `dynamicsprocessor` module, the new test and `PluginPortsMigrationTest` (the only other target that includes the header, `tests/src/plugins/PluginPortsMigrationTest.cpp:36`) rebuilt | PASS |
| Collateral damage: DSP parity | `PluginPortsMigrationTest::migratedPluginsPreserveBehaviour` - the migrated plugin still renders sample-exactly like the frozen reference: `sha256 migrated=00ea8dc0dca25ef58ece8ddfc616976b9738a32631613d3e394d2b836d25a6b8 reference=00ea8dc0…a6b8`, `max\|delta\|=0` | PASS |
| Collateral damage: saved data | `GraphWaveformLoadTest` (loads `dynamicsprocessor`'s saved `waveShape` through the real load path) plus `dynamicsProcessorRejectsShortWaveShape` | PASS |

## Commands and exit codes (logs in `<runs>/new-p9-130.r2/logs`)

RED - the regression written first, run against the committed (round 1) product code by
reversing this round's product patch:

- `git diff -- plugins/DynamicsProcessor > fix.patch` -> EXIT=0;
  `git apply -R fix.patch` -> EXIT=0, `01-reverse-apply.log`
- `cmake --build build --target cmake_check_build_system` -> EXIT=0, `02-reconfigure.log`
- `cmake --build build --target DynamicsProcessorSampleRateHandoffTest -j4` -> EXIT=0,
  `03-build-red.log`
- `cd build/tests && ctest -R '^DynamicsProcessorSampleRateHandoffTest$' --output-on-failure`
  -> **EXIT=8**, `04-test-red.log`: `Totals: 4 passed, 2 failed`; the two failures are the
  allocation slots -
  `aRateIncreaseIsAppliedWithoutAllocatingOrFreeingOnTheAudioPath() 'applied.allocations == 0' returned FALSE. (the audio path allocated 2 time(s) applying a sample-rate increase)` and
  `repeatedRateChangesReuseTheWindowsWithoutGrowing() ... (step 0 (rate 48000): the audio path allocated 2 and freed 2)`.
  `theSampleRateRequestIsHandedOffThroughAnAtomicFlag` **passed**: the red is the
  allocation, not the atomic type.

GREEN:

- `git apply fix.patch` -> EXIT=0, `05-reapply.log`
- `cmake --build build --target zene dynamicsprocessor DynamicsProcessorSampleRateHandoffTest PluginPortsMigrationTest -j4`
  -> EXIT=0, `06-build-green.log`
- `cd build/tests && ctest -R '^DynamicsProcessorSampleRateHandoffTest$' --output-on-failure`
  -> **EXIT=0**, `07-test-green.log`
- `cd build/tests && ./DynamicsProcessorSampleRateHandoffTest aRateIncreaseIsAppliedWithoutAllocatingOrFreeingOnTheAudioPath repeatedRateChangesReuseTheWindowsWithoutGrowing theSampleRateRequestIsHandedOffThroughAnAtomicFlag processingWithoutARequestLeavesTheWindowAlone`
  -> **EXIT=0**, `08-test-green-slots.log` (`Totals: 6 passed, 0 failed, 0 skipped`) - the
  slots ran, none was skipped

Family / siblings:

- `cd build/tests && ./PluginPortsMigrationTest dynamicsProcessorNeedsUpdateStartsCleared dynamicsProcessorRejectsShortWaveShape dynamicsProcessorHugeLevelReadsLastCurvePoint migratedPluginsPreserveBehaviour comparisonIsSensitive`
  -> EXIT=0, `09-test-migration-dyn.log` (`Totals: 7 passed, 0 failed, 0 skipped`)
- `cd build/tests && ctest -R '^GraphWaveformLoadTest$' --output-on-failure` -> EXIT=0,
  `10-test-graph-family.log`

## 4b. The test catches the bug (final code)

- `cmake --build ... -j4` (final protocol) -> EXIT=0, `12-build-green.log`
- `ctest -R '^DynamicsProcessorSampleRateHandoffTest$'` -> **EXIT=0**, `13-test-green.log`
- `git diff -- plugins/DynamicsProcessor > fix.patch` -> EXIT=0 and
  `git apply -R fix.patch` -> EXIT=0, `14-reverse-apply.log`
- rebuild (`zene dynamicsprocessor DynamicsProcessorSampleRateHandoffTest`) -> EXIT=0,
  `15-build-red-proof.log`
- `ctest -R '^DynamicsProcessorSampleRateHandoffTest$'` -> **EXIT=8**,
  `16-test-red-proof.log` - the same two allocation assertions fail (2 allocations, 2 frees
  on the audio path)
- `git apply fix.patch` -> EXIT=0, `17-reapply.log`
- rebuild -> EXIT=0, `18-build-green-proof.log`
- `ctest -R '^DynamicsProcessorSampleRateHandoffTest$'` -> **EXIT=0**,
  `19-test-green-proof.log`

## Registration

- The test file already exists in `tests/all-sources.txt` (registered in round 1); no new
  product file, so `tests/fork-sources.txt` is unchanged.
- `tests/CMakeLists.txt`: the target block now also calls
  `lmms_link_allocation_probe(DynamicsProcessorSampleRateHandoffTest ...)` so the probe's
  replacement `operator new`/`delete` is linked into this binary.
- The three inherited product files changed again this round are declared in
  `tests/upstream-modifications.txt` with a `cluster new-p9-130 (round 2)` reason each
  (`tests/upstream-modifications.txt:6160-6162`, appended by `11-ledger-append.log`, EXIT=0). `include/RmsHelper.h` is not
  touched this round.

## Gates

`FEATURE_CHECK_LOG_DIR=<logs>/gates python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`
-> **EXIT=0**, `20-feature-check.log`:

```
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

With this report in the tree the same table ran again -> EXIT=0,
`21-feature-check-report.log`. On the committed tree: the regression test
(`ctest -R '^DynamicsProcessorSampleRateHandoffTest$'`) -> EXIT=0, `22-test-committed.log`;
the gates -> EXIT=0, `23-feature-check-committed.log`; the completion check
`python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p9-130`
-> EXIT=0, `24-submit-check.log`, `RESULT: PASS - submit` with no FAIL row.

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it (exit
  codes).** Yes: EXIT=8 in `16-test-red-proof.log` (this round's product patch reversed and
  rebuilt) and EXIT=8 in `04-test-red.log` (the same pre-fix code, before the patch was
  ever applied); EXIT=0 in `13-test-green.log`, `19-test-green-proof.log` and the slot-level
  `08-test-green-slots.log`. The failing assertions are the allocation ones, not the
  atomic-type one (which passes in both states).
- **The test is deterministic: no wall-clock timing, sleeps or races (name the seam).**
  Yes. No thread is started: the slot runs synchronously on the test thread through
  `AudioDevice::setSampleRateForTesting()` + `emit Engine::audioEngine()->sampleRateChanged()`
  (the `tests/src/core/DeviceDspTestParts2.h:121-136` pattern), the effect is rendered with
  direct `processImpl()` calls, and the allocation measurement is the repo's
  `tests/src/core/AllocationProbe.h` seam armed around `processImpl()` alone
  (`resetAllocationCount()`, `tlCountAllocations`, `tlAllocationCount`, `tlFreeCount`). The
  test's own buffer is filled before the probe is armed, so nothing but the audio path is
  counted.
- **The fix is at the cause and complete: every caller and sibling checked.** The three
  product references to the request and the two window owners are all updated: the slot
  (`DynamicsProcessorControls.cpp:62-67`), the preparation and publication
  (`DynamicsProcessor.cpp:97-129`), the audio-side install (`:132-138`), the render read
  (`:173-185`, `:198`) and the constructor/destructor (`:74-94`). `m_rms` no longer exists
  as a member, so no stale accessor remains; the only other translation unit that includes
  the header (`PluginPortsMigrationTest.cpp`) was rebuilt and its
  `dynamicsProcessorNeedsUpdateStartsCleared` (poisoned-byte check) still passes.
  `RmsHelper.h` needed no change: the same `setSize()` call now runs on the control thread.
- **No collateral damage.** No save/load code or format changed, and the DSP render is
  still sample-exact against the frozen pre-migration reference (`max|delta|=0`, same
  sha256 `00ea8dc0…` as the reference). No limit or check was added; the bank list is
  bounded by reuse, and the regression pins `<= 4` banks across twelve changes. Behaviour
  on the normal path is unchanged: with no request the render reads the same helpers it
  always did, and the request branch still recomputes the attack/release coefficients from
  the models.
- **Realtime: nothing new allocates, frees, locks, waits or logs on the audio thread.** The
  new render-thread operations are one `exchange` on `std::atomic<bool>`, one `exchange`
  on `std::atomic<RmsBank*>` and one release store; all three are lock-free on every
  supported target (the bool one is `static_assert`ed in the test). The allocation and the
  `delete[]` it implies moved to `prepareSampleRateChange()` on the control thread. No
  control command was added, so the `ControlCommandsSnapshot` canary does not move; undo is
  untouched (the flag and the window are not editable models).
- **Sibling commands in command files.** None: this change touches no
  `ControlCommands*`, `ControlRegistry*` or reversibility-table file.
- **The reviewer's other two notes.** The `Testing/` directory the review saw is not
  present in this worktree (`ls -d Testing` prints nothing), so there was nothing to leave
  untouched. The run log directory `<runs>/new-p9-130.r2/logs` is writable
  (`test -w` printed `LOGDIR-WRITABLE`) and holds every command's output and exit code
  cited above; no command in this session was run without its log.
- **Not ALREADY-FIXED and not FALSE-POSITIVE: the verdict is REAL.** The pre-fix state is
  this branch's committed round-1 code (plain `bool`, `setSize()` inside `processImpl()`);
  the reversed patch restores exactly that and the rebuilt binary fails the regression.

## Other findings

- `RmsHelper::setSize()` still frees and allocates when the window grows
  (`include/RmsHelper.h:84-90`); that is now correct for this plugin because every call
  happens on the control thread. Other callers would still be wrong on the audio thread,
  and `RmsHelper` itself has no audio-thread story - a separate concern.
- The same shared-flag shape exists in at least one sibling plugin and is not fixed here:
  `plugins/Bitcrush/Bitcrush.cpp:85-91` sets `m_needsUpdate = true` from the
  `sampleRateChanged()` handler that `BitcrushControls` connects to, while `processImpl()`
  reads it and clears it at `plugins/Bitcrush/Bitcrush.cpp:107-145`. Its own cluster, out
  of scope for a one-bug fix; I did not audit the remaining plugins' sample-rate slots.
- `PluginPortsMigrationTest`'s Mallets slots cannot run in this checkout (the configured
  `PART_C_STK_DIR` is absent), so STK prints `FileRead::open: could not open ... marmstk1.raw`
  during `migratedPluginsPreserveBehaviour`. It does not affect the dynamics processor
  comparison, which stays sample-exact.
