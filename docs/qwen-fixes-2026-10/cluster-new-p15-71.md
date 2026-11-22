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

- **The regression test FAILS with the product change reverted and PASSES with it
  (exit codes).** Reverted: `ctest -R GigPlayer24BitFullScale` EXIT=8 with
  `ratio > 0.9f` false (24-bit 0.435972 vs 16-bit 0.871917). Restored: same
  command EXIT=0. Logs `logs/test-5-reverted-red.log`,
  `logs/test-6-refixed-green.log`.
- **The test is deterministic.** It uses no wall clock, sleeps or threads. The
  fixtures are generated deterministically from the tracked
  `tests/data/gigproof/sine440.gig` (16-bit and 24-bit full-scale PCM written
  into the same-sized file), the render is a fixed 64 periods through the real
  plugin entry point. The measured ratio is produced by injected sample values,
  not by timing.
- **The fix is at the cause and complete.** `GigInstrument::loadSample()` is the
  only 24-bit conversion in the product (`rg -n "0x100000000" plugins/GigPlayer`
  now finds nothing). Both channels were changed (`:1372`, `:1385`), and the
  16-bit sibling (`:1395`, `:1404`) already normalised by `0x8000`. The frozen
  pre-migration reference keeps the old constant on purpose: it is the baseline
  the harness compares against and the harness only renders the tracked 16-bit
  fixture, with a documented 2x scale factor at
  `tests/src/plugins/PluginPortsMigrationTest.cpp:2584-2600`.
- **No collateral damage.** Project and preset XML, the `src`/`patch`/`bank`
  attributes, patch selection and save/load are untouched; only the arithmetic
  scale applied to decoded 24-bit PCM changed, and 16-bit audio is untouched. No
  limit or check was added, so no other caller can be rejected. The focused
  ctest case and all 15 GigPlayer cases pass (`logs/test-4-gigfamily.log`).
- **ALREADY-FIXED / FALSE-POSITIVE code evidence.** Not applicable: the verdict
  is FIXED. The buggy constant was visible at
  `plugins/GigPlayer/GigPlayer.cpp:1372` before this change, and the earlier
  revert is commit `c844f8f1cf`.
- **Sibling commands.** This fix changes no command file
  (`ControlCommands*`, `ControlRegistry*`, `ControlReversibilityTable*` are
  untouched). The `app.*` and `arrangement.*` ids listed by the pre-submit
  check come from other commits on the shared branch, not this cluster. GIG
  sample decoding has no agent command and this fix adds none, so no sibling
  command behaviour changes.

## Other findings

- The full `PluginPortsMigrationTest` suite cannot be green in this checkout
  because `thirdparty/stk-rawwaves` and the SWH Decimator LADSPA fixture are not
  installed. This is an environment gap, not a regression from this change.
- `plugins/GigPlayer/GigPlayer.cpp:1366-1368` shifts a `uint8_t` promoted to
  `int` by 24, so the top bit is shifted into the sign bit of `int` (technically
  undefined/implementation-defined before the assignment to `int32_t`). This is
  pre-existing and outside this cluster; noted only, not changed.
