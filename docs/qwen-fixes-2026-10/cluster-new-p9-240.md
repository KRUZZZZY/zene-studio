FIXED

# Cluster new-p9-240 — LOMM sample-rate changes can resize buffers while audio uses them

## Verdict

REAL, fixed. The finding's concurrency premise (the resize races `processImpl`) does **not**
hold on this tree, but the same handler has the defect the resize exposes: it rebuilds the six
lookahead rings to the new length and leaves `m_lookWrite`, the rings' write cursor, at its
old-ring value. After a rate drop the new rings are shorter than the old cursor, and the next
period reads and writes past their end. The handler also kept the old-rate samples in the
rings while resetting every other piece of DSP state, so the rings replayed old-rate audio
into the new-rate periods.

## Evidence: the race is not real

- `AudioDevice::stopProcessing()` clears the running flag, stops the backend, and then
  `waitForOpenCallbacks()` waits until every backend callback scope has ended:
  `src/core/audio/AudioDevice.cpp:135-154`. A backend callback holds a `CallbackScope` for its
  whole body (`include/AudioDevice.h:170-186`).
- All three producers of `AudioEngine::sampleRateChanged` are outside a callback:
  - `src/core/AudioEngine.cpp:808` in `initDevices()`, called once from `Engine::init`
    (`src/core/Engine.cpp:86`) while the engine is being built;
  - `src/core/AudioEngine.cpp:1408-1415` in `setAudioDevice()`, after `stopProcessing()` and
    before `startProcessing()`;
  - `src/core/AudioEngine.cpp:1436-1442` in `restoreAudioDevice()`, also around
    `stopProcessing()`/`startProcessing()`.
- Therefore no `processImpl()` can be running while `LOMMEffect::changeSampleRate()` runs, and
  the handler's allocation (the vector rebuild) is not an audio-thread allocation. This part of
  the finding is rejected with the code above.

## Evidence: the resize is still unsafe (the real defect)

- Before the fix, `plugins/LOMM/LOMM.cpp:88-96` did:
  `m_lookBufLength = ... + 2;` then `m_inLookBuf[j][i].resize(m_lookBufLength);` and
  `m_scLookBuf[j][i].resize(m_lookBufLength, LOMM_MIN_FLOOR);`.
  `m_lookWrite` was not touched (it is only set at construction, `LOMM.cpp:63`, and by the
  per-frame wrap at `LOMM.cpp:427`).
- `processImpl()` subscripts every ring with that cursor:
  `m_scLookBuf[j][i][m_lookWrite]` and the lookahead read at `plugins/LOMM/LOMM.cpp:299-300`,
  `m_inLookBuf[j][i][m_lookWrite]` at `plugins/LOMM/LOMM.cpp:393-394`.
- A 48 kHz ring is `ceil(20 ms * 48000) + 2 = 962` entries; 44.1 kHz is 884. After one period
  at 48 kHz the cursor is a valid old-ring value (the regression ends at 946), then the rate
  drops: 946 is past the end of an 884-entry vector, so the next period's read and write are
  out of bounds.
- The handler already resets the whole DSP state on a rate change —
  `m_yL`, `m_rms`, `m_gainResult`, `m_displayIn`, `m_displayOut`, `m_prevOut` and the crest
  followers at `plugins/LOMM/LOMM.cpp:108-111`. The two lookahead rings and their cursor are
  the omission, not an intentional carry-over.

## Fix (cause, complete)

`plugins/LOMM/LOMM.cpp:88-107`: when the rings are rebuilt, the cursor is re-anchored to 0 and
the rings are assigned their reset contents (`0.f` for the input delay line,
`LOMM_MIN_FLOOR` for the sidechain line). A fresh effect at the new rate has exactly that
state, so the construction path is unchanged (`changeSampleRate()` is called from the
constructor, `LOMM.cpp:71`). Nothing runs on the audio thread: the slot runs on the control
thread with processing stopped, as shown above. `processImpl()` is unchanged.

Siblings checked: `m_lookBufLength` and `m_lookWrite` are written only in `changeSampleRate()`
(constructor + slot) and `processImpl()`'s wrap; the six rings are written only there and in
`processImpl()`. Both directions are covered by the regression (drop and rise). No save/load
format, no model, no command, no undo state is touched.

## Regression test

`tests/src/plugins/LommSampleRateTest.cpp` (new target `LommSampleRateTest`, registered in
`tests/CMakeLists.txt`; the LOMM sources are compiled into the test binary, the
`DispersionNonFiniteTest` pattern, so the real handler is driven through
`AudioEngine::sampleRateChanged()`).

| Matrix | Slot | What it proves |
|---|---|---|
| normal path | `theCursorStaysInsideTheGrownRingsWhenTheRateRises` | a period renders, a rate rise grows the rings, sizes and cursor stay consistent |
| failure path | `theCursorStaysInsideTheResizedRingsWhenTheRateDrops` | a 48k→44.1k change after a full ring leaves the cursor inside the 884-entry rings; fails on the unfixed handler (cursor 946) |
| reconfiguration/output | `theCursorStaysInsideTheResizedRingsWhenTheRateDrops` (second period) | the next period after the change renders finite samples |
| stale state | `theRingsCarryNoOldRateAudioAcrossTheChange` | rings hold no samples captured at the old rate after the change; fails on the unfixed handler (12 stale entries) |
| exhaustion/wrap | `theCursorStaysInsideTheResizedRingsWhenTheRateDrops` (processes `m_lookBufLength + 16` frames) | the cursor wraps the full ring before the change |
| audio-thread constraint | `processingDoesNotResizeTheRings` | `processImpl()` leaves every ring's capacity and data pointer unchanged (no audio-thread allocation) |

Determinism: the rate is injected with `AudioDevice::setSampleRateForTesting()` and the signal
is emitted synchronously; the test renders fixed frame counts. There is no clock, sleep,
thread, plugin module or allocator interposition.

## Commands and results

Logs: `/home/kruzzzzy/.config/kads/zene/state/runs/new-p9-240.r1/logs`.

| # | Exact command | EXIT | Evidence |
|---|---|---|---|
| 1 | `cmake --build build --target zene -j4 > logs/build-2-regen.log 2>&1` (regenerates the makefiles after the new CMake target) | 0 | `Configuring done`, `Generating done` in the log |
| 2 | `cmake --build build --target LommSampleRateTest -j4 > logs/build-3-red.log 2>&1` | 0 | test binary built |
| 3 | `cd build/tests && ctest -R '^LommSampleRateTest$' --output-on-failure > logs/test-2-red.log 2>&1` | 8 | `FAIL! ... the lookahead ring cursor was left outside the resized buffers`; `FAIL! ... staleRingEntries(effect)` 12 vs 0; `Totals: 4 passed, 2 failed` |
| 4 | `cmake --build build --target lomm LommSampleRateTest -j4 > logs/build-5-green.log 2>&1` | 0 | module and test rebuilt with the fix |
| 5 | `cd build/tests && ctest -R '^LommSampleRateTest$' --output-on-failure > logs/test-3-green.log 2>&1` | 0 | `100% tests passed out of 1` |
| 6 | `cd build/tests && ctest -R '^(LommSampleRateTest|DispersionNonFiniteTest|MultitapEchoBoundsTest)$' --output-on-failure > logs/test-5-family-green.log 2>&1` | 0 | all three plugin-source-compiled tests pass |
| 7 | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > logs/test-6-migration.log 2>&1` | 8 | `migratedPluginsPreserveBehaviour()` PASS; `lomm` migrated/reference sha256 equal (`test-6-migration.log:961`); the 4 failures are pre-existing and unrelated (see Other findings) |

### Step 4b: prove the test catches the bug

| # | Exact command | EXIT |
|---|---|---|
| 1 | `git diff -- plugins/LOMM/LOMM.cpp > logs/fix.patch` | 0 |
| 2 | `git apply -R logs/fix.patch` | 0 |
| 3 | `cmake --build build --target LommSampleRateTest -j4 > logs/build-8-red-proof.log 2>&1` | 0 |
| 4 | `cd build/tests && ctest -R '^LommSampleRateTest$' --output-on-failure > logs/test-7-red-proof.log 2>&1` | **8** — `Totals: 4 passed, 2 failed`, the two slots above fail |
| 5 | `git apply logs/fix.patch` | 0 |
| 6 | `cmake --build build --target lomm LommSampleRateTest -j4 > logs/build-9-green-proof.log 2>&1` | 0 |
| 7 | `cd build/tests && ctest -R '^LommSampleRateTest$' -V > logs/test-8-green-proof.log 2>&1` | **0** — `Totals: 6 passed, 0 failed`; `Test #777 ... Passed` |

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py
/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/kads-new-p9-240`
(logs/feature-check-2.log; table copied from the log, unpiped exit code **0**):

```
feature-check kads-new-p9-240 (base fa4a5f2e7, 352 changed files; logs in .../build/feature-check)
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

Registration: `tests/src/plugins/LommSampleRateTest.cpp` is in `tests/fork-sources.txt` and in
`tests/all-sources.txt` (byte order); the report is named in `tests/fork-sources.txt`; the
changed inherited files are declared in `tests/upstream-modifications.txt`
(`plugins/LOMM/LOMM.cpp`, `tests/CMakeLists.txt`). Both manifests end with a newline.

## Self-check

1. **The regression test fails with the product change reverted and passes with it.** Yes:
   with the patch reversed, `logs/test-7-red-proof.log` exits 8 with the two failures above;
   restored, `logs/test-8-green-proof.log` exits 0 with 6 passed. The exact commands are in
   "Step 4b".
2. **The test is deterministic.** No wall clock, sleep, thread or race. The seam is the
   engine's own test hook: `AudioDevice::setSampleRateForTesting(rate)` followed by a
   synchronous `emit engine->sampleRateChanged()`, exactly what a device change does; the frame
   counts and input are fixed. The failing assertion is a state invariant, not a timing
   observation.
3. **The fix is at the cause and complete.** `changeSampleRate()` is the only writer of
   `m_lookBufLength` and the only resize of the rings; it now also writes the cursor and the
   ring contents, which are the two pieces of state it had missed while resetting all the
   others. Both rate directions are exercised, and the construction path is unchanged (same
   contents after `assign` as after `resize`). `processImpl()` is untouched. Search of
   `plugins/LOMM/` for `m_lookWrite`, `m_lookBufLength`, `m_inLookBuf`, `m_scLookBuf` found
   only the constructor, this handler and `processImpl()`.
4. **No collateral damage.** The PM test that compares every migrated module against the
   pre-migration reference still matches for `lomm` (sha256 identical, `test-6-migration.log:961`);
   the construction-time ring contents are byte-identical (zero/floor in both cases). No saved
   data, model, command, undo state or limit is changed, so old projects load and render as
   before.
5. **Verdict evidence.** Not ALREADY-FIXED/FALSE-POSITIVE: the concurrency part is rejected by
   `src/core/audio/AudioDevice.cpp:135-154` and the three emit sites, and the remaining real
   defect and its fix are shown at `plugins/LOMM/LOMM.cpp:88-107` (after fix) with the failing
   assertions in `logs/test-7-red-proof.log`.

## Other findings

- `PluginPortsMigrationTest` has 4 failures on this tree that are unrelated to LOMM and were
  already present for other lanes: `malletsVoiceLimitIsEnforcedPerInstrument`,
  `malletsVoiceLimitSurvivesDeviceChange`, `slice8PluginsAreLiveAndExact` (the Mallets
  instrument assets are not in this build) and `nonRealtimeLadspaEffectsAreInvalid` (the SWH
  Decimator test fixture was not loaded). They were not touched here.
- A first `ctest -R` family run reported `DispersionNonFiniteTest` and
  `MultitapEchoBoundsTest` as "Not Run" because those executables were not built in this tree;
  building them (`logs/build-6-family.log`, exit 0) made the family run pass
  (`logs/test-5-family-green.log`, exit 0).
