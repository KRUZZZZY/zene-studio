FIXED

new-p15-73: FIXED

# cluster new-p15-73: a sample-rate change reached the shifter one event-loop turn late

Finding: `plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp:75` (historical).
Verdict: REAL, fixed on this branch.

## Decision: REAL, and what the code shows

The finding's mechanism is the connection type, not the DSP:

- `plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp:71-72` (before the fix) connected
  `AudioEngine::sampleRateChanged` with **`Qt::QueuedConnection`**. The preparation it runs —
  `sampleRateNeedsUpdate()` (`:294`) — is therefore posted to the effect's thread and delivered by
  the next event-loop turn, not by the emit.
- The engine emits while the backend is stopped and then lets audio run again *in the same call*:
  `src/core/AudioEngine.cpp:1420-1429` (`setAudioDevice()`: `stopProcessing()`, swap, emit at
  `:1427`, `if (startNow) { startProcessing(); }` at `:1429`) and `:1446-1456`
  (`restoreAudioDevice()`: `stopProcessing()` at `:1450`, `emit sampleRateChanged()` at `:1454`,
  `startProcessing()` at `:1456`). `AudioDevice::stopProcessing()` waits for open callbacks
  (`src/core/audio/AudioDevice.cpp:137-155`), so nothing is racing the emit — but nothing delivers
  the queued call before the restart either.
- The engine states the ordering contract in its own code: `src/core/ProjectRenderer.cpp:265-267`
  — *"Have to do audio engine stuff with GUI-thread affinity in order to make slots connected to
  sampleRateChanged()-signals being called immediately."*
- Until the prepared object is applied, `processImpl()` runs on the old state. `:116`
  (`applyPreparedBuffers()`) is the only place that installs it, and the values the finding names
  are read from `m_sampleRate`: `:132` (`minLatency = value() * m_sampleRate`), `:141`
  (`m_glideCoef`), `:145` (`sizeSamples = m_sampleRate / size`), and `m_ringBufLength`, which
  `applyPreparedBuffers()` (`:322-355`) keeps in step with the rate.
- Every other listener I checked connects without an explicit queued delivery, i.e. it runs in the emitting thread: `plugins/Dispersion/Dispersion.cpp:70-71`,
  `plugins/FrequencyShifter/FrequencyShifterEffect.cpp:60-61`, `plugins/NeuralAmp/NeuralAmpEffect.cpp:75-76`,
  `plugins/ConvolutionReverb/ConvolutionReverb.cpp:177`, `plugins/Stk/Mallets/Mallets.cpp:284`,
  `plugins/FreeBoy/FreeBoy.cpp:329`, `plugins/CarlaVst2Effect/CarlaVst2Effect.cpp:129`,
  `plugins/Lv2Effect/Lv2FxControls.cpp:40`, `plugins/Lv2Instrument/Lv2Instrument.cpp:112`,
  `src/core/AudioPortsModel.cpp:146`, `src/core/VstSyncController.cpp:57`. The granular effect was
  the only queued one, so it was the only one that could miss the restart window.

Measured on the unfixed tree (this session, `logs/test-2-red.log` and `logs/test-4-reverted-red.log`):
after `setAudioDevice(48000)` the first rendered block still reports
`granular->m_sampleRate == 44100`, expected `48000`, and its ring buffer is still
220500 frames (5 s at 44.1 kHz) instead of 240000.

## Fix (at the cause)

`plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp:71-81`: the connection no longer asks
for a queued delivery, so the slot runs in the emitting thread (AutoConnection: direct on the same
thread, still queued if a signal ever comes from another thread). The preparation therefore
completes inside `AudioEngine::setAudioDevice()` / `restoreAudioDevice()` **before**
`startProcessing()`, and `applyPreparedBuffers()` installs it at the top of the first new-rate block.

- Nothing on the audio path changed: `processImpl()` is untouched, `sampleRateNeedsUpdate()` and
  `applyPreparedBuffers()` are untouched, the allocation stays on the control thread with the
  backend stopped (that is the Q050-005 shape, `b3a4d9d7c3`), and no lock, wait or log was added.
- The three producers are unchanged and all emit off the audio thread (`AudioEngine.cpp:812`,
  `:1427`, `:1454`). `sampleRateNeedsUpdate()` reads `outputSampleRate()`, and in all three the
  device is already swapped when the signal fires.
- Siblings/callers checked: `sampleRateNeedsUpdate()`'s three callers (constructor `:70`, this
  signal `:80-81`, `GranularPitchShifterControls::updateRange()` at
  `plugins/GranularPitchShifter/GranularPitchShifterControls.cpp:104`); the controls, the prepared
  handoff, the retired-buffer reclamation and the save/load path are unchanged, so old projects
  load and sound as before.
- Registration: both changed product files are inherited, so
  `tests/upstream-modifications.txt` gained one row each (cluster new-p15-73).

## Test matrix

| Path | Covered by |
| --- | --- |
| Normal: device swap, next block at the new rate | `sampleRateHandoffCompletesBeforeTheNextBlock` (new), first block after `setAudioDevice(..., false)` |
| Failure before the fix (old rate in force) | same slot, `QCOMPARE(m_sampleRate, 48000)` — RED evidence below |
| Reconfiguration: two announces before the next block | same slot, second half: the pending state is replaced, the block runs at the newest rate |
| Exhaustion: retired slots | not reachable through the product paths — every `sampleRateNeedsUpdate()` reclaims first (`:296`), so a prepare always finds both slots free; the guard at `:325-333` is unchanged and untested here |
| Audio-thread constraints | existing `sampleRateHandoffDoesNotAllocateAndKeepsPitchShift` (allocation and free counters = 0, callback under 100 ms) still passes in the same binary |
| Saved data / controls | existing `retiredBuffersAreReclaimedWithoutAnotherUpdate` and `savedOutOfRangeValueIsBoundedByTheControlModel` still pass |
| Supported platforms | `initTestCase` skips on Windows (a test host cannot load plugin modules there); the test uses no platform API, and the seam is an inline `LMMS_TESTING` friend, so Qt 5/MSVC builds see no difference |

Seam: `plugins/GranularPitchShifter/GranularPitchShifterEffect.h:38-40` and `:207-211` add an
`LMMS_TESTING`-only friend so the test can read the rate and the ring buffer the next block will
use. It is compiled out of product builds and adds no data member.

## RED / GREEN (all commands from the worktree, logs in the run's log folder)

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| baseline build | `cmake --build build --target GranularPitchShifterRealtimeTest -j4` | 0 | `build-1-baseline.log` |
| baseline test (before any change) | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^GranularPitchShifterRealtimeTest$' --output-on-failure` | 0 | `test-1-baseline.log` |
| RED build (test + seam, no fix) | `cmake --build build --target GranularPitchShifterRealtimeTest -j4` | 0 | `build-2-red.log` |
| RED test | same ctest command | 8 | `test-2-red.log` |
| GREEN build | `cmake --build build --target zene GranularPitchShifterRealtimeTest -j4` | 0 | `build-3-green.log` |
| GREEN test | same ctest command | 0 | `test-3-green.log` |
| GREEN, all slots visible | `cd build/tests && QT_QPA_PLATFORM=offscreen ./GranularPitchShifterRealtimeTest` | 0 | `test-3-green-verbose.log` (6 passed) |
| proof patch | `git diff -- plugins/GranularPitchShifter/GranularPitchShifterEffect.cpp > ~/.config/kads/zene/state/runs/new-p15-73.r1/logs/fix.patch` | 0 | `fix.patch` |
| revert the fix | `git apply -R ~/.config/kads/zene/state/runs/new-p15-73.r1/logs/fix.patch` | 0 | — |
| rebuild reverted | `cmake --build build --target GranularPitchShifterRealtimeTest -j4` (rebuilds `../libgranularpitchshifter.so`, log line 18-21) | 0 | `build-4-reverted.log` |
| RED proof | same ctest command | 8 | `test-4-reverted-red.log` |
| restore the fix | `git apply ~/.config/kads/zene/state/runs/new-p15-73.r1/logs/fix.patch` | 0 | — |
| rebuild restored | `cmake --build build --target GranularPitchShifterRealtimeTest -j4` | 0 | `build-5-restored.log` |
| GREEN proof | same ctest command | 0 | `test-5-restored-green.log` |

`fix.patch` holds the behavioural hunk (`GranularPitchShifterEffect.cpp`) only. The header hunk is
the `LMMS_TESTING` seam the test needs in order to compile; reverting it would turn the RED run into
a compile error instead of a test failure, and it is inert in product builds.

RED output (`test-2-red.log`, and again in `test-4-reverted-red.log`):

```
FAIL!  : GranularPitchShifterRealtimeTest::sampleRateHandoffCompletesBeforeTheNextBlock() Compared floats are not the same (fuzzy compare)
   Actual   (granular->m_sampleRate)         : 44100 (0x1.5888p+15)
   Expected (static_cast<float>(ChangedRate)): 48000 (0x1.77p+15)
   Loc: [../../tests/src/core/GranularPitchShifterRealtimeTest.cpp(291)]
Totals: 5 passed, 1 failed, 0 skipped, 0 blacklisted, 508ms
```

## Family and siblings

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| family build | `cmake --build build --target BasicFiltersSampleRateTest PluginPortsMigrationTest FreeBoySampleRateHandoff LommSampleRateTest AudioPluginTest -j4` | 2 (`No rule to make target 'FreeBoySampleRateHandoff'` — it is a ctest that runs the slot `PluginPortsMigrationTest::freeBoySampleRatePoolHandoffIsRealtimeSafe`, not a target; that slot PASSES in every run below) | `build-6-family.log` |
| family build 2 | `cmake --build build --target LommSampleRateTest AudioPluginTest -j4` | 0 | `build-7-family2.log` |
| family test | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^(BasicFiltersSampleRateTest\|PluginPortsMigrationTest\|LommSampleRateTest\|AudioPluginTest\|GranularPitchShifterRealtimeTest)$' --output-on-failure` | 8 | `test-6-family.log` |
| A/B, fix reverted | `git apply -R .../fix.patch` (0); `cmake --build build --target granularpitchshifter -j4` (0); `ctest -R '^PluginPortsMigrationTest$' --output-on-failure` | 0 / 0 / 8 | `build-8-ab-reverted.log`, `test-7-family-ab.log` |
| final build | `cmake --build build --target zene granularpitchshifter GranularPitchShifterRealtimeTest -j4` | 0 | `build-9-final.log` |
| final family test | same 5-test ctest command | 8 | `test-8-family-final.log` |

`PluginPortsMigrationTest` reports `95 passed, 4 failed, 2 skipped` in **both** states, with the same
four slots and the same reasons: `malletsVoiceLimitIsEnforcedPerInstrument`,
`malletsVoiceLimitSurvivesDeviceChange` and `slice8PluginsAreLiveAndExact` (missing
`thirdparty/stk-rawwaves/*.raw` — `ls thirdparty/` in this worktree prints only `mts-esp`) and
`nonRealtimeLadspaEffectsAreInvalid` (the SWH Decimator LADSPA fixture is not installed). They are
environment fixtures, not this change; the A/B run above is the evidence, and
`docs/qwen-fixes-2026-10/cluster-new-p15-71.md:148-162` recorded the identical four on this tree.
The other four binaries pass: `BasicFiltersSampleRateTest`, `LommSampleRateTest`, `AudioPluginTest`,
`GranularPitchShifterRealtimeTest`.

## Re-verification on the committed tree (this session)

Re-ran the 4b pair, the family and the gates from a clean tree at `fc27954f5a` (only `fix.patch` from
this run's log folder was applied and reversed), so the verdict also rests on output produced in this
session rather than on the earlier run's logs:

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| revert the fix | `git apply -R ~/.config/kads/zene/state/runs/new-p15-73.r1/logs/fix.patch` | 0 | — |
| rebuild reverted | `cmake --build build --target GranularPitchShifterRealtimeTest -j4` | 0 | `build-11-verify-red.log` |
| RED proof | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R '^GranularPitchShifterRealtimeTest$' --output-on-failure` | 8 | `test-10-verify-red.log` |
| restore the fix | `git apply ~/.config/kads/zene/state/runs/new-p15-73.r1/logs/fix.patch` | 0 | — |
| rebuild restored | `cmake --build build --target zene GranularPitchShifterRealtimeTest -j4` | 0 | `build-12-verify-green.log` |
| GREEN proof | same ctest command | 0 | `test-11-verify-green.log` |
| family | `cd build/tests && QT_QPA_PLATFORM=offscreen ctest -R 'GranularPitchShifter|AudioPlugin|SampleRate|Lomm' --output-on-failure` | 0 | `test-12-verify-family.log` (6 of 6 passed) |

RED output this session (`test-10-verify-red.log`):

```
FAIL!  : GranularPitchShifterRealtimeTest::sampleRateHandoffCompletesBeforeTheNextBlock() Compared floats are not the same (fuzzy compare)
   Actual   (granular->m_sampleRate)         : 44100 (0x1.5888p+15)
   Expected (static_cast<float>(ChangedRate)): 48000 (0x1.77p+15)
   Loc: [../../tests/src/core/GranularPitchShifterRealtimeTest.cpp(291)]
Totals: 5 passed, 1 failed, 0 skipped, 0 blacklisted, 502ms
```

The reverse apply succeeding on the committed tree shows `fix.patch` is the product change in
`aa51194b1f`, not a hand-made variant.

## Gates

```
# run three times: before the commit (logs/gates-1.log, 3 changed files), after the ledger and the
# report were added (logs/gates-2.log, 5 changed files) and on the committed tree (logs/gates-3-final.log)
python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>   EXIT=0

feature-check kads-new-p15-73 (base 3436a808b, 5 changed files; logs in <worktree>/build/feature-check)
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

Re-run this session on the committed tree, unpiped:

```
cd <worktree> && FEATURE_CHECK_LOG_DIR=<logs>/gate-verify \
  python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>
```
EXIT 0, the same eight PASS rows (`logs/gates-4-verify.log`, per-gate logs in `logs/gate-verify/`):
`fork-sources`, `all-sources-reproduce`, `no-upstream-regression`, `check-namespace`,
`no-tautology`, `doc-figures`, `complexity` (no new item in this branch's files), `file-length`
(no new item).

## Self-check

`submit_check.py` exit 0 (`logs/submit-check-4-verify.log`: `RESULT: PASS - submit`, five rows PASS,
no FAIL), five ASK items. Answers, each with the evidence this session saw:

1. **The regression FAILS with the product change reverted and PASSES with it.** The 4b pair is
   above: `git apply -R .../fix.patch` EXIT 0, rebuild EXIT 0 → ctest EXIT **8**
   (`logs/test-4-reverted-red.log`: `Actual (granular->m_sampleRate): 44100`, `Expected: 48000`,
   line 291); `git apply .../fix.patch` EXIT 0, rebuild EXIT 0 → ctest EXIT **0**
   (`logs/test-5-restored-green.log`). Independently, the test was RED before the fix existed at
   all (`logs/test-2-red.log`, EXIT 8) and the baseline binary was green (`logs/test-1-baseline.log`,
   EXIT 0), so the failure is the change under test and not the harness. Re-run again this session on
   the committed tree: reverse apply 0, rebuild 0 -> ctest EXIT **8** (`test-10-verify-red.log`);
   restore 0, rebuild 0 -> ctest EXIT **0** (`test-11-verify-green.log`); the family run of six
   binaries EXIT **0** (`test-12-verify-family.log`).
2. **The test is deterministic: no wall-clock timing, no sleeps, no threads, no event loop.** The
   new slot starts no thread and calls neither `processEvents()` nor any timer; it renders blocks by
   calling `renderOneBlock()` directly. The rate is *injected*: `FixedRateAudioDevice` sets it with
   `AudioDevice::setSampleRateForTesting()` and the engine announces it through
   `AudioEngine::setAudioDevice()` — the same seam `tests/src/plugins/LommSampleRateTest.cpp` and
   `DispersionNonFiniteTest` use. The observation is the state the next block reads (`m_sampleRate`,
   `m_ringBuf.size()`), read through the `LMMS_TESTING`-only friend
   (`GranularPitchShifterEffect.h:207-211`), not through timing. The only guard in the slot is the
   `QCOMPARE(outputSampleRate(), ...)` pre-condition that the injection landed.
3. **The fix is at the cause and complete.** The cause was the connection type
   (`GranularPitchShifterEffect.cpp:71-72` before the fix), and that one line was changed. Checked:
   (a) the three callers of `sampleRateNeedsUpdate()` — constructor `:70`, this signal `:80-81`, and
   `GranularPitchShifterControls::updateRange()` (`GranularPitchShifterControls.cpp:104`); (b) all
   three producers of `sampleRateChanged` (`AudioEngine.cpp:812`, `:1427`, `:1454`) fire off the
   audio thread with the backend stopped (`AudioDevice.cpp:137-155`); (c) every sibling listener in
   the tree already runs in the emitting thread (the eleven sites listed above), so nothing else
   needs the same change, and no listener needed its own ordering fix; a ranked repo-wide search for
   `Qt::QueuedConnection` returned no `plugins/` hit. Local TSan does not run on this box, so
   the thread-safety statement is the design argument above, not a detector report.
4. **No collateral damage.** The DSP (`processImpl`), `sampleRateNeedsUpdate()`,
   `applyPreparedBuffers()`, the retired-buffer reclamation and the control models are byte-for-byte
   unchanged; only *when* the already-existing preparation runs changed, so a saved project loads
   and sounds exactly as before (no serialization, no parameter, no default touched). No limit or
   check was added anywhere. Within the same binary, the three pre-existing slots still pass
   (`sampleRateHandoffDoesNotAllocateAndKeepsPitchShift`: allocations 0, frees 0;
   `retiredBuffersAreReclaimedWithoutAnotherUpdate`; `savedOutOfRangeValueIsBoundedByTheControlModel`),
   and the family binary `PluginPortsMigrationTest` reports the same 4 environment failures with and
   without the fix (`test-6-family.log` vs `test-7-family-ab.log`).
5. **Not ALREADY-FIXED and not FALSE-POSITIVE — verdict FIXED.** The queued connection was present
   in this tree (`GranularPitchShifterEffect.cpp:71-72` at `3436a808b`) and the RED run showed the
   old rate in force after the swap (`logs/test-2-red.log`, re-confirmed in
   `logs/test-10-verify-red.log`), so the defect was live, not already
   fixed and not a phantom. The contract it violated is the engine's own
   (`src/core/AudioEngine.cpp:1420-1429`, `:1446-1456`, and the comment at
   `src/core/ProjectRenderer.cpp:265-267`).

## Other findings

- `PluginPortsMigrationTest` cannot be green in this checkout: `thirdparty/stk-rawwaves` is absent
  (only `mts-esp` is in `thirdparty/`) and the SWH Decimator LADSPA fixture is not installed. Same
  four slots with and without this change; not touched here.
- Engine-side: a plugin whose `sampleRateChanged` slot is slow now delays `startProcessing()` inside
  `setAudioDevice()` / `restoreAudioDevice()`, which is the ordering the engine already documents
  (`ProjectRenderer.cpp:265-267`) and what every other listener already does. The granular slot
  allocates and zero-fills up to 120 s × 96 kHz of ring buffer, so a device swap now pays that cost
  before audio restarts instead of after. That is the intended trade (no audio is processed with the
  stale state); noted, not changed.
