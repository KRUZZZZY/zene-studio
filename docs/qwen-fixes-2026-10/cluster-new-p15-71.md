FIXED

# GIG 24-bit samples play at half the expected level (cluster new-p15-71)

## Verdict

REAL. `GigInstrument::loadSample()` packs each signed 24-bit sample byte into the
top 24 bits of an `int32_t`:

- `plugins/GigPlayer/GigPlayer.cpp:1365-1368` — `valueLeft = (byte0 << 8) | (byte1 << 16) | (byte2 << 24)`
- `plugins/GigPlayer/GigPlayer.cpp:1372` — divided by `0x100000000`
- `plugins/GigPlayer/GigPlayer.cpp:1380-1385` — the right channel does the same
- `plugins/GigPlayer/GigPlayer.cpp:1395,1404` — the 16-bit path divides by `0x8000`

Because the 24-bit value is left-justified in 32 bits, dividing by `0x100000000`
(2^32) maps full scale (±2^31) to ±0.5. Dividing by `0x80000000` (2^31) maps it to
±1.0, matching the 16-bit path. The same issue had been observed and then
deliberately reverted as out of scope in `c844f8f1cf` ("GIG: remove out-of-scope
normalization change (cluster new-p9-149-port)"); this cluster is the owner of
the fix.

## Change

- `plugins/GigPlayer/GigPlayer.cpp:1372,1385` (now with an explanatory comment on
  the left channel): normalise by `0x80000000` instead of `0x100000000`.
- No other 24-bit conversion exists in the GIG player. The only sibling is the
  frozen pre-migration reference in `tests/reference/GigPlayer/GigPlayer.cpp`,
  which must keep the old behaviour: the harness only renders the tracked 16-bit
  fixture and already compares that path with a documented 2x scale factor
  (`tests/src/plugins/PluginPortsMigrationTest.cpp:2584-2600`). Changing the
  reference would destroy its purpose.
- `tests/upstream-modifications.txt`: declares the inherited product change and
  names the regression.
- `tests/src/plugins/PluginPortsMigrationTest.cpp`: restores the regression
  `gigplayer24BitSamplePlaysAtFullScale` and its fixture builder, plus a focused
  ctest entry `GigPlayer24BitFullScale` in `tests/CMakeLists.txt`.

No header changed; the audio path is unchanged apart from a float scale constant.
Nothing was added to the audio thread: no allocation, lock, wait or log. Undo and
control commands are untouched.

## Test matrix

| Path | Coverage |
|------|----------|
| Normal | A full-scale 16-bit fixture and an otherwise identical full-scale 24-bit fixture must render at the same peak (ratio in 0.9..1.1). |
| Failure/negative | The 16-bit fixture must be non-silent (`peak16 > 0.05`); the 24-bit fixture is the changed path. |
| Exhaustion / reconfiguration | Not applicable to a per-sample conversion. The GIG voice/layer exhaustion and loop tests were rerun in the same executable and still pass. |
| Audio thread | Conversion runs inside `loadSample()` from the render path; the change is arithmetic only. `gigplayerAudioPathDoesNotAllocate` passes. |
| Supported platforms | `swap32IfBE()` is untouched; the arithmetic constant is platform independent. |

The 24-bit fixture is generated at runtime from the tracked `tests/data/gigproof/sine440.gig`
by patching the sample's `fmt` chunk and data, so no new binary fixture is added.

## RED (before the fix, test added, product code unchanged)

Command (from `build/tests`):

```
ctest -R GigPlayer24BitFullScale --output-on-failure
EXIT=8
```

```
FAIL!  : PluginPortsMigrationTest::gigplayer24BitSamplePlaysAtFullScale() 'ratio > 0.9f' returned FALSE. (a full-scale 24-bit sample played at 0.435972, which is half the level of the full-scale 16-bit sample (0.871917))
Totals: 2 passed, 1 failed, 0 skipped, 0 blacklisted, 500ms
```

Log: `logs/test-1-red.log`.

## GREEN (with the fix)

Build:

```
cmake --build build --target zene PluginPortsMigrationTest -j4
EXIT=0
```

Log: `logs/build-2-green.log`.

Test:

```
ctest -R GigPlayer24BitFullScale --output-on-failure
EXIT=0
100% tests passed out of 1
```

Log: `logs/test-2-green.log`.

## 4b. Proof that the test catches the bug

```
git diff -- plugins/GigPlayer/GigPlayer.cpp > logs/fix.patch
EXIT=0
git apply -R logs/fix.patch
EXIT=0
cmake --build build --target PluginPortsMigrationTest -j4
EXIT=0
ctest -R GigPlayer24BitFullScale --output-on-failure
EXIT=8
```

```
FAIL!  : PluginPortsMigrationTest::gigplayer24BitSamplePlaysAtFullScale() 'ratio > 0.9f' returned FALSE. (a full-scale 24-bit sample played at 0.435972, which is half the level of the full-scale 16-bit sample (0.871917))
```

Logs: `logs/build-3-reverted.log`, `logs/test-5-reverted-red.log`.

```
git apply logs/fix.patch
EXIT=0
cmake --build build --target PluginPortsMigrationTest -j4
EXIT=0
ctest -R GigPlayer24BitFullScale --output-on-failure
EXIT=0
```

Logs: `logs/build-4-refixed.log`, `logs/test-6-refixed-green.log`.

## Family run

The GIG cases in the existing test binary were run together:

```
QT_QPA_PLATFORM=offscreen ./PluginPortsMigrationTest \
  gigPlayerAcceptsMidiNoteZero \
  gigplayerBidirectionalLoopReflectsAtExclusiveEnd \
  gigplayerSingleFrameLoopRepeatsFrame \
  gigplayerNoProgressFallbackKeepsEarlierSamples \
  gigplayerAudioPathDoesNotAllocate \
  gigplayer24BitSamplePlaysAtFullScale \
  gigplayerCallbackDoesNotWaitForAFileLoad \
  gigplayerKeepsEveryOverlappingNote \
  gigplayerPlaysMoreThan512OverlappingNotes \
  gigplayerKeepsEveryLayerOfANote \
  gigplayerSurvivesGlobalVoicePoolExhaustion \
  gigPlayerKeepsReleaseTriggerAcrossRegions \
  gigOneFrameReleaseSampleIsAudible \
  gigPatchDialogPreviewsSelectedProgram \
  gigKeyboardDimensionUsesPressedKey
EXIT=0
Totals: 17 passed, 0 failed, 0 skipped, 0 blacklisted, 1196ms
```

Log: `logs/test-4-gigfamily.log`.

The whole `PluginPortsMigrationTest` binary (`ctest -R '^PluginPortsMigrationTest$'`)
also ran. It reports 91 passed, 4 failed, 2 skipped
(`logs/test-3-family.log`). All four failures are unrelated environment fixtures,
not GIG code:

- `malletsVoiceLimitIsEnforcedPerInstrument` and
  `malletsVoiceLimitSurvivesDeviceChange` — `thirdparty/stk-rawwaves/sinewave.raw`
  is absent from this checkout, so Mallets renders silence.
- `slice8PluginsAreLiveAndExact` — same missing rawwaves ("malletsstk rendered silence").
- `nonRealtimeLadspaEffectsAreInvalid` — the SWH Decimator LADSPA fixture is not
  installed.

Every GIGPlayer case in that same binary passes (run above).

## Gates

```
python3 ~/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>
EXIT=0
```

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

Log: `logs/feature-check-1.log`.

## Self-check

- Does the test fail without the product change? Yes, exit 8 with ratio 0.435972 /
  0.871917 (~0.5); see RED and 4b above.
- Does it pass with the change? Yes, exit 0, and the case appears as `PASS` in the
  17-case GigPlayer run (`logs/test-4-gigfamily.log`).
- Are callers and siblings consistent? `loadSample()` is the only conversion site.
  The frozen reference is intentionally left at the old behaviour because it is
  the pre-migration baseline and the harness only renders the 16-bit fixture.
- Any work left unverified? No: this box built and ran the test; the unrelated
  family failures are named with their missing fixtures.

## Other findings

- The full `PluginPortsMigrationTest` suite cannot be green in this checkout
  because `thirdparty/stk-rawwaves` and the SWH Decimator LADSPA fixture are not
  installed. This is an environment gap, not a regression from this change.
- `plugins/GigPlayer/GigPlayer.cpp:1366-1368` shifts a `uint8_t` promoted to
  `int` by 24, so the top bit is shifted into the sign bit of `int` (technically
  undefined/implementation-defined before the assignment to `int32_t`). This is
  pre-existing and outside this cluster; noted only, not changed.
