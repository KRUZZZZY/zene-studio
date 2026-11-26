FIXED

new-p9-130: FIXED

## Decision: REAL

The finding describes the code exactly. `DynProcControls::sampleRateChanged()` is the
slot of `AudioEngine::sampleRateChanged()`:

- before the fix, `plugins/DynamicsProcessor/DynamicsProcessorControls.cpp:64` wrote
  `m_effect->m_needsUpdate = true;` - a plain `bool`, on the control thread.
- before the fix, `plugins/DynamicsProcessor/DynamicsProcessor.cpp:117-123` read that flag
  on the audio path and cleared it in a second, separate access
  (`if( m_needsUpdate ) { ... m_needsUpdate = false; }`).
- before the fix, `plugins/DynamicsProcessor/DynamicsProcessor.h:65` declared
  `bool m_needsUpdate = false;` with no synchronisation of any kind. The plugin owns no
  barrier, no atomic and no engine-stated contract.

Two defects follow from that shape.

1. The control thread writes the flag while the audio thread reads it: a data race. The
   engine's emitters currently quiesce the audio thread first - `setAudioDevice()` and
   `restoreAudioDevice()` call `stopProcessing()` before `emit sampleRateChanged()`
   (`src/core/AudioEngine.cpp:1420-1456`) and `AudioDevice::stopProcessing()` waits for the
   open backend callbacks to end (`src/core/audio/AudioDevice.cpp:137-156`), while
   `initDevices()` emits once before processing starts (`AudioEngine.cpp:812`). So the race
   is latent rather than observed in today's paths, but the ordering is an undocumented
   engine invariant, not a contract the plugin states or can rely on, and the plugin has
   none of its own. The same shape (a `sampleRateChanged` slot publishing state the audio
   path reads) was accepted as REAL for the Mallets voice pool
   (`docs/qwen-fixes-2026-10/cluster-new-p21-233.md`) and for FreeBoy, with the same
   argument.
2. The read-then-clear is a lost update. If a second sample-rate change reaches the slot
   after the audio thread has read `true` and before it stores `false`, the request
   disappears: the RMS window and the detector coefficients stay at the old rate for the
   rest of the effect's life. One exchange cannot lose it.

Local TSan does not run on this box, so this is a design argument about the handoff, not a
detector report.

Scope note: this finding is the flag. The allocation `RmsHelper::setSize()` performs inside
the same block when the request is consumed is pre-existing and reachable today
(`include/RmsHelper.h:79-113` reallocates when the window grows); it is listed under
"Other findings" and is not changed here.

## Fix (at the cause)

- `plugins/DynamicsProcessor/DynamicsProcessor.h:33` - `#include <atomic>`.
- `plugins/DynamicsProcessor/DynamicsProcessor.h:67-70` -
  `std::atomic<bool> m_needsUpdate{false};` (value-initialised, so the state the
  constructor leaves is still "no request" - cluster new-p9-129's fix is preserved; see
  `tests/upstream-modifications.txt:760`).
- `plugins/DynamicsProcessor/DynamicsProcessorControls.cpp:62-68` - the slot publishes the
  request with one `store( true, std::memory_order_release )`. The release publishes the
  device rate the engine set before it emitted the signal.
- `plugins/DynamicsProcessor/DynamicsProcessor.cpp:117-126` - the render path consumes it
  with one `exchange( false, std::memory_order_acq_rel )`; the update block itself (RMS
  window at the new rate, `calcAttack()`, `calcRelease()`) is unchanged, and the `else`
  branch for the attack/release model edits is untouched.

Every reference to the member was checked; there are exactly three product references
(an fff grep for `m_needsUpdate` over the worktree: declaration, slot, render path) plus one
test accessor, `DynProcEffectTestAccess::needsUpdateByte` in
`tests/src/plugins/PluginPortsMigrationTest.cpp:970-981`, which reads the member's first
byte and still sees 0 with the atomic (`dynamicsProcessorNeedsUpdateStartsCleared`, green
below). Nothing else in the tree touches the flag or constructs `DynProcEffect` beyond the
plugin entry point and the tests.

## Test matrix

| Path | Test | Result |
| --- | --- | --- |
| The handoff (the cause) | `theSampleRateRequestIsHandedOffThroughAnAtomicFlag` in the new `tests/src/plugins/DynamicsProcessorSampleRateHandoffTest.cpp` - a runtime trait probe on the declared member: a plain `bool` fails it, `std::atomic<bool>` passes | **FAIL before, PASS after** |
| Normal path (no request) | `processingWithoutARequestLeavesTheWindowAlone` - four periods leave the RMS window and both coefficients untouched | PASS |
| Reconfiguration | `aSampleRateChangeIsAppliedOnceAndConsumed` - the real slot publishes, one period consumes once and re-derives the window (69 -> 139 at 96 kHz) and both coefficients at the new rate; a second period changes nothing | PASS |
| Exhaustion / repeated request | `repeatedRequestsCoalesceIntoOneUpdate` - two rate changes with no period between coalesce, the last rate wins, one period applies it | PASS |
| Failure path | none in this code: the slot and the request have no error branch; the only failure surface is save/load of `waveShape`, covered by `dynamicsProcessorRejectsShortWaveShape` (green) | n/a |
| Audio-thread constraints | the consume is one lock-free exchange (`static_assert(std::atomic<bool>::is_always_lock_free)` at the top of the test file); no allocation, lock, wait or log is added to `processImpl()` | PASS (builds and runs) |
| Supported platforms | header and definition changed together; `zene`, the `dynamicsprocessor` module, the new test and `PluginPortsMigrationTest` (the only other target that includes the header, `tests/src/plugins/PluginPortsMigrationTest.cpp:36`) all rebuilt | PASS |
| Collateral damage: DSP parity | `PluginPortsMigrationTest::migratedPluginsPreserveBehaviour` - the migrated plugin renders sample-exactly like the frozen reference: `sha256 migrated=00ea8dc0dca25ef58ece8ddfc616976b9738a32631613d3e394d2b836d25a6b8 reference=00ea8dc0…a6b8 | max\|delta\|=0`, 7 passed, 0 skipped | PASS |
| Collateral damage: saved data | `GraphWaveformLoadTest` (loads `dynamicsprocessor`'s saved `waveShape` through the real load path) plus `dynamicsProcessorRejectsShortWaveShape` | PASS |

The regression trigger reads the *declared* handoff through an overload-free trait
(`std::is_same_v<T, std::atomic<bool>>` on the member's deduced type), not through a
product test seam, so the file compiles against the unfixed header and reports the defect
as a failed assertion rather than a build error. The behavioural slots around it prove the
handoff still works.

## Commands and exit codes (all logs in the run log folder)

RED - test first, no product change:

- `cmake --build build --target DynamicsProcessorSampleRateHandoffTest -j4` -> EXIT=2,
  `01-build-red.log` (`No rule to make target` - the new target needs the build system
  regenerated first)
- `cmake --build build --target cmake_check_build_system` -> EXIT=0, `02-reconfigure.log`
- `cmake --build build --target DynamicsProcessorSampleRateHandoffTest -j4` -> EXIT=0,
  `03-build-red.log`
- `cd build/tests && ctest -R '^DynamicsProcessorSampleRateHandoffTest$' --output-on-failure`
  -> **EXIT=8**, `04-test-red.log`:
  `FAIL!  : ...::theSampleRateRequestIsHandedOffThroughAnAtomicFlag() 'isAtomicHandoff(effect.m_needsUpdate)' returned FALSE`
  (`Totals: 5 passed, 1 failed`)

GREEN - with the fix:

- `cmake --build build --target zene dynamicsprocessor DynamicsProcessorSampleRateHandoffTest PluginPortsMigrationTest -j4`
  -> EXIT=0, `05-build-green.log`
- `cd build/tests && ctest -R '^DynamicsProcessorSampleRateHandoffTest$' --output-on-failure`
  -> **EXIT=0**, `06-test-green.log`

Family / siblings:

- `cmake --build build --target GraphWaveformLoadTest -j4` -> EXIT=0, `08-build-graph-family.log`
  (the first attempt, `07-test-graph-family.log`, was `Not Run` EXIT=8 because that binary
  had never been built in this tree - not a product failure)
- `cd build/tests && ctest -R '^GraphWaveformLoadTest$' --output-on-failure` -> EXIT=0,
  `09-test-graph-family.log`
- `cd build/tests && ./PluginPortsMigrationTest dynamicsProcessorNeedsUpdateStartsCleared dynamicsProcessorRejectsShortWaveShape dynamicsProcessorHugeLevelReadsLastCurvePoint migratedPluginsPreserveBehaviour comparisonIsSensitive`
  -> EXIT=0, `10-test-migration-dyn.log` (`Totals: 7 passed, 0 failed, 0 skipped`)

## 4b. The test catches the bug

- `git diff -- plugins/DynamicsProcessor > <logs>/fix.patch` -> EXIT=0 and
  `git apply -R <logs>/fix.patch` -> EXIT=0, `11-reverse-apply.log`
- `cmake --build build --target zene dynamicsprocessor DynamicsProcessorSampleRateHandoffTest -j4`
  -> EXIT=0, `12-build-red-proof.log`
- `cd build/tests && ctest -R '^DynamicsProcessorSampleRateHandoffTest$' --output-on-failure`
  -> **EXIT=8**, `13-test-red-proof.log` - the same assertion fails, at
  `tests/src/plugins/DynamicsProcessorSampleRateHandoffTest.cpp(210)`
- `git apply <logs>/fix.patch` -> EXIT=0, `14-reapply.log`
- `cmake --build build --target zene dynamicsprocessor DynamicsProcessorSampleRateHandoffTest -j4`
  -> EXIT=0, `15-build-green-proof.log`
- `cd build/tests && ctest -R '^DynamicsProcessorSampleRateHandoffTest$' --output-on-failure`
  -> **EXIT=0**, `16-test-green-proof.log`

## Registration

- New test source: `tests/src/plugins/DynamicsProcessorSampleRateHandoffTest.cpp`, listed
  in `tests/all-sources.txt` (the manifest's own recipe re-derived it; the diff is that one
  line) - `bash tests/all-sources-reproduce.sh` -> EXIT=0, `18-all-sources-reproduce.log`
  (`REPRODUCES: the entry list in all-sources.txt is the recipe's own output.`)
- `tests/CMakeLists.txt`: the new `DynamicsProcessorSampleRateHandoffTest` target and its
  `add_test` registration, next to the LommSampleRateTest block it copies.
- Inherited product files changed: `plugins/DynamicsProcessor/DynamicsProcessor.cpp`,
  `DynamicsProcessor.h` and `DynamicsProcessorControls.cpp` are declared in
  `tests/upstream-modifications.txt` with a `cluster new-p9-130` reason each.

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

After committing (the commit this report ships in, named by `logs/proof.json`), on the
committed tree: the regression test (`ctest -R '^DynamicsProcessorSampleRateHandoffTest$'`)
-> EXIT=0, `27-test-final.log`; the same gate table -> EXIT=0,
`28-feature-check-final.log`; the completion check
`python3 ~/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p9-130`
-> EXIT=0, `29-submit-check-final.log`, `RESULT: PASS - submit` with no FAIL row.

## Self-check

- **A decision with evidence.** REAL, with the three pre-fix file:line references above,
  the engine ordering that makes it latent rather than live today, and the precedent for
  the same shape. Not an "already fixed" claim: the flag was a plain bool on this branch
  before this change (`git diff` in `11-reverse-apply.log`'s patch shows the `-	bool`).
- **The regression test fails without the product change and passes with it.** Yes:
  EXIT=8 in `04-test-red.log` (test added, product untouched) and again EXIT=8 in
  `13-test-red-proof.log` (product change reversed, rebuilt), EXIT=0 in
  `06-test-green.log` and `16-test-green-proof.log`.
- **The test is deterministic: no wall-clock timing, sleeps or races.** Yes. No thread is
  started: the slot is driven synchronously on the test thread by
  `AudioDevice::setSampleRateForTesting()` + `emit Engine::audioEngine()->sampleRateChanged()`
  (`tests/src/core/DeviceDspTestParts2.h:121-136` is the same pattern), and the effect is
  rendered with direct `processImpl()` calls. The trigger is the declared type, probed with
  a trait that compiles against both declarations.
- **The trigger is honest, not a build-error trick.** The RED and GREEN runs use the same
  test source, built against the same headers apart from the three product files; the
  failure is a `QVERIFY2` at runtime, not a compile error. The failure message cites the
  *pre-fix* line numbers (header line 65, slot line 64, render path 117-123) because it
  describes the buggy state; those lines moved with the fix.
- **The fix is at the cause and complete: every caller and sibling checked.** The three
  product references are the whole set (`m_needsUpdate` grep), the one test accessor was
  rebuilt and re-run green, and `PluginPortsMigrationTest` - the only other translation
  unit that includes the header - was rebuilt with the fix.
- **No collateral damage.** No save/load code or format changed (`saveSettings`/
  `loadSettings` untouched), and the DSP render is sample-exact against the frozen
  pre-migration reference (`max|delta|=0`). No limit or check was added, so no caller can
  be newly rejected. The attack/release `isValueChanged()` branch keeps its behaviour
  (the request branch recomputes the coefficients from the models, exactly as before).
- **Realtime: nothing new allocates, frees, locks, waits or logs on the audio thread.** The
  only new render-thread operation is one `exchange` on a `std::atomic<bool>` (lock-free on
  every supported target; `static_assert` in the test). The store and the request handling
  stay on the control thread. No control command was added, so the
  `ControlCommandsSnapshot` canary does not move. Undo is untouched: the flag is not an
  editable model.
- **Registration and gates.** Above: registration lines added, `feature_check.py` EXIT=0
  with the full PASS table.
- **Not ALREADY-FIXED and not FALSE-POSITIVE: the verdict is REAL.** The flag was a plain
  `bool` on this branch before this change - the reversed patch (step 4b) puts back exactly
  `-	bool m_needsUpdate = false;` and the write `-	m_effect->m_needsUpdate = true;`, and the
  rebuilt binary fails the regression. It is not false-positive either: the plugin's own code
  has no synchronisation at all, and the read-then-clear can lose a request. The engine's
  quiescence before every current emitter (cited above) is why nobody has observed the race,
  not why the flag is safe - the same argument the Mallets and FreeBoy fixes rest on.
- **Sibling commands in command files.** None: this change touches no
  `ControlCommands*`, `ControlRegistry*` or reversibility-table file.

## Other findings

- `RmsHelper::setSize()` is called from `processImpl()` when the request is consumed, and
  it allocates when the window grows (`include/RmsHelper.h:79-113`). That is a pre-existing
  realtime violation, reachable today (a device change during playback sets the request),
  not introduced here and not part of this finding's subject. Fixing it means publishing
  the rebuilt helper pair from the control thread with a retirement path for the old one -
  a separate change.
- The same shared-flag shape exists in at least one sibling plugin, and is not fixed here:
  `plugins/Bitcrush/Bitcrush.cpp:85-91` sets `m_needsUpdate = true` from the
  `sampleRateChanged()` handler that `BitcrushControls` connects to
  (`plugins/Bitcrush/BitcrushControls.cpp`, the same `Engine::audioEngine()` connection),
  while `processImpl()` reads it and clears it at `plugins/Bitcrush/Bitcrush.cpp:107-145`.
  That is a separate finding (its own cluster), out of scope for a one-bug fix; I did not
  audit the remaining plugins' sample-rate slots, so I make no claim about them.
- `PluginPortsMigrationTest`'s Mallets slots cannot run in this checkout: the configured
  `PART_C_STK_DIR` is absent, so STK prints `FileRead::open: could not open ... marmstk1.raw`
  during `migratedPluginsPreserveBehaviour`. It does not affect the dynamics processor
  comparison, which is sample-exact (above).
