# Cluster new-p23-188: OSS 16-bit conversion on +1.0 samples

**Verdict: ALREADY-FIXED**

The finding's mechanism is arithmetically false: `1.0f * 32767.0f` is `32767.0`, which fits `int16_t`, so a sample at exactly +1.0 cannot wrap to -32768. The real defect the finding is about (samples above +1.0 scaling past the int16 range) was fixed on this branch by `e7854be17` (cluster new-p10-492), which added the clamp in `toPcmSample`. This cluster adds a regression test that pins the full-scale contract at the boundary the finding names. No product code changed.

## File evidence

- [OssSampleConversion.h:27-31](../../src/core/audio/OssSampleConversion.h:27) - `toPcmSample` maps NaN to 0 (`:29`) and clips with `AudioEngine::clip` before the multiply and int16 cast (`:30`).
- [AudioEngine.h:362-372](../../include/AudioEngine.h:362) - `clip()` returns `1.0f` for `s > 1.0f` and `-1.0f` for `s < -1.0f`, so the scaled value is always within ±32767.
- [AudioEngine.h:81](../../include/AudioEngine.h:81) - `OUTPUT_SAMPLE_MULTIPLIER = 32767.0f` (not 32768).
- Pre-fix code, from `git show e7854be17 -- src/core/audio/AudioOss.cpp`: `pcmBuf[i] = static_cast<int16_t>(buf[i] * OUTPUT_SAMPLE_MULTIPLIER);` - no clamp. That is the over-range defect. `e7854be17` is an ancestor of this branch (`git merge-base --is-ancestor e7854be17 HEAD` exit 0).
- [AudioOss.cpp:238](../../src/core/audio/AudioOss.cpp:238) - the one OSS caller, `audio::oss::toPcmSample(buf[i])`.
- Sibling conversions all clip before scaling, so they share the contract: [AudioDevice.cpp:184](../../src/core/audio/AudioDevice.cpp:184), [AudioDevice.cpp:199](../../src/core/audio/AudioDevice.cpp:199), [AudioFileWave.cpp:76](../../src/core/audio/AudioFileWave.cpp:76), [AudioFileWave.cpp:282](../../src/core/audio/AudioFileWave.cpp:282), [AudioSndio.cpp:151](../../src/core/audio/AudioSndio.cpp:151), [ExportNoiseShaper.cpp:55](../../src/core/ExportNoiseShaper.cpp:55).
- Tests: [DeviceDspTest.cpp:405](../../tests/src/core/DeviceDspTest.cpp:405) `ossConversionClipsOverrangeSamples` (from `e7854be17`: +1.25, -1.25, in-range 0.5, NaN). New in this cluster: [DeviceDspTest.cpp:414](../../tests/src/core/DeviceDspTest.cpp:414) `ossConversionFullScaleStaysInRange` (+1.0, -1.0, and the nearest float beyond each: `nextafter(1.0f, 2.0f)`, `nextafter(-1.0f, -2.0f)`).
- Standalone arithmetic check (`boundary-1.log` in the run folder; scratch program in `$TMPDIR`, removed afterwards): `1.0f*32767 = 32767.0 -> 32767`, `-1.0f*32767 = -32767.0 -> -32767`, `INT16_MAX=32767 INT16_MIN=-32768`.

## Regression evidence

Logs are in the run folder `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p23-188.r1/logs/`. Short name below: `LOGS/`. The `ccache` temp directory was not writable from this session, so every build sets `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p23-188` (see the first row).

| Step | Exact command | Exit / result |
|---|---|---|
| Build attempt (ccache temp dir not writable) | `cmake --build build --target DeviceDspTest -j4 > LOGS/build-1-green-current.log 2>&1; echo EXIT=$?` | 2 (ccache `Permission denied`; no code problem) |
| Build, GREEN baseline | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p23-188 cmake --build build --target DeviceDspTest -j4 > LOGS/build-2-green-current.log 2>&1; echo EXIT=$?` | 0 |
| Test, unmodified HEAD `cf3618e61` | `cd build/tests && ctest -R '^DeviceDspTest$' --output-on-failure > LOGS/test-1-green-current.log 2>&1; echo EXIT=$?` | 8; OSS test PASS; the only failure is `theFrequencyShifterMuteRouteDoesNotPassDryInput` ("the frequency shifter module is available") - pre-existing, see below |
| RED, clamp removed (pre-fix line) | edit `OssSampleConversion.h:30` to `static_cast<std::int16_t>(sample * OUTPUT_SAMPLE_MULTIPLIER)`, then `CCACHE_TEMPDIR=... cmake --build build --target DeviceDspTest -j4 > LOGS/build-3-red-clamp-removed.log 2>&1; echo EXIT=$?` and `ctest -R '^DeviceDspTest$' --output-on-failure > LOGS/test-2-red-clamp-removed.log 2>&1; echo EXIT=$?` | build 0; test 8; `ossConversionClipsOverrangeSamples` FAIL at `DeviceDspTest.cpp:407`, actual `toPcmSample(1.25f)` = **-24578** (wrap), expected 32767 |
| Restore header | `cp LOGS/OssSampleConversion.h.orig src/core/audio/OssSampleConversion.h; sha256sum -c LOGS/orig.sha256` | OK, exit 0; `git status` clean apart from the test file |
| GREEN after restore | `CCACHE_TEMPDIR=... cmake --build build --target DeviceDspTest -j4 > LOGS/build-4-green-restored.log 2>&1; echo EXIT=$?` and `ctest -R '^DeviceDspTest$' --output-on-failure > LOGS/test-3-green-restored.log 2>&1; echo EXIT=$?` | build 0; test 8 (same pre-existing frequency-shifter failure); `ossConversionClipsOverrangeSamples` PASS |
| Test added: `ossConversionFullScaleStaysInRange` | edit `DeviceDspTest.cpp:413-421`; build `LOGS/build-5-green-newtest.log` and test `LOGS/test-4-green-newtest.log` | build 0; test 8 (pre-existing failure only); new test PASS |
| RED for the new test: the finding's own mechanism (clamp, then scale by 32768) | edit `OssSampleConversion.h:30` to `AudioEngine::clip(sample) * 32768.0f`; build `LOGS/build-6-red-scale32768.log`; test `LOGS/test-5-red-scale32768.log` | build 0; test 8. `ossConversionFullScaleStaysInRange` FAIL at `DeviceDspTest.cpp:416`, actual `toPcmSample(1.0f)` = **-32768** (the finding's wrap) |
| Restore header | `cp LOGS/OssSampleConversion.h.orig src/core/audio/OssSampleConversion.h; sha256sum -c LOGS/orig.sha256` | OK, exit 0 |
| Final build, frequency-shifter plugin | `CCACHE_TEMPDIR=... cmake --build build --target frequencyshifter -j4 > LOGS/build-8-plugin-frequencyshifter.log 2>&1; echo EXIT=$?` | 0 |
| Final test | `cd build/tests && ctest -R '^DeviceDspTest$' -V > LOGS/test-8-green-after-plugin-verbose.log 2>&1; echo EXIT=$?` | **0**; `Totals: 29 passed, 0 failed, 13 skipped` (the 13 skips are "this build has no X module" for plugins the targeted build does not compile); both OSS tests PASS |

**Affected family:** `ctest -R '^DeviceDspTest$'` runs one CTest entry containing 42 QTest functions (29 passed, 0 failed, 13 skipped) in the final state.

**Pre-existing failure (not caused by this cluster):** on the unmodified HEAD, before any edit, `theFrequencyShifterMuteRouteDoesNotPassDryInput` failed because the `FrequencyShifter` plugin library is not built by a targeted `DeviceDspTest` build (`build/plugins/FrequencyShifter` holds only moc metadata). Building the plugin target (`build-8`, exit 0) makes the family exit 0. I did not change that test.

## Gates

| Gate | Exact command | Exit |
|---|---|---|
| Feature gate, first try | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > LOGS/feature-check-1.log 2>&1; echo EXIT=$?` | 1 - the script creates its log folder beside the worktree (`<worktree>-feature-check`), outside the write area; no gate ran |
| Feature gate, final tree | a copy of `feature_check.py` in `$TMPDIR` with only the output folder and `ROOT` re-pointed (gate logic unchanged, cache `gate-baselines/cf3618e61…` reused): `python3 -I $TMPDIR/fc-new-p23-188.py <worktree> > LOGS/feature-check-3-final.log 2>&1; echo EXIT=$?` | 0 - `RESULT: PASS`; 1 changed file; fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures PASS; complexity and file-length "no new item in your files" |

`tests/src/core/DeviceDspTest.cpp` is listed in `tests/fork-sources.txt` (fork-new), so no `tests/upstream-modifications.txt` entry is needed.

## Files committed

- `tests/src/core/DeviceDspTest.cpp` - new test `ossConversionFullScaleStaysInRange`
- `docs/qwen-fixes-2026-10/cluster-new-p23-188.md` - this report

No product file changed. The header was edited twice for the RED runs and restored byte-for-byte both times (hash check above).

## Self-check

- **The regression test FAILS with the product change reverted and PASSES with it.** There is no product change in this cluster; the fix is `e7854be17`. Reverting its clamp (`OssSampleConversion.h:30`) makes `ossConversionClipsOverrangeSamples` FAIL at `DeviceDspTest.cpp:407` (actual -24578; `test-2-red-clamp-removed.log`, exit 8). The new test does not catch the clamp removal, because 1.0 × 32767 fits without it. It does catch the finding's own mechanism: with a 32768 scale after the clip it FAILS at `DeviceDspTest.cpp:416` with `toPcmSample(1.0f)` = -32768 (`test-5-red-scale32768.log`, exit 8). With the code as committed it PASSES (`test-8-green-after-plugin-verbose.log`, exit 0 for the family).
- **The test is deterministic:** no clock, sleep, thread or random input. It uses fixed float literals and `std::nextafter` at ±1.0.
- **The fix is at the cause and complete:** the clamp lives in the one shared converter (`OssSampleConversion.h:27-31`). The OSS caller (`AudioOss.cpp:238`) and every sibling float-to-int16 site clip before scaling (listed in File evidence), so there is no other site with the defect. No new code was added to the product.
- **No collateral damage:** no product code, saved data, limit or check changed. `toPcmSample` behaves exactly as at `e7854be17`; the new test only asserts existing behaviour. Realtime: unchanged, `toPcmSample` is still an inline `noexcept` function with no allocation, lock or logging.
- **Completion check (`submit_check.py`, `LOGS/submit-check-2.log`, exit 1):** every row passes except "gates (feature_check.py)". That row runs the unmodified `feature_check.py`, which stops at `logs.mkdir` for `<worktree>-feature-check` with `PermissionError` (`LOGS/feature-check-1.log`), because that folder is outside this session's write area. The gate logic itself passes: `LOGS/feature-check-3-final.log`, exit 0, `RESULT: PASS`, run through the copy described in Gates. The reviewer should rerun the unmodified script from a session that can write the sibling folder.
- **ALREADY-FIXED evidence (the code or contract that shows it):** `OssSampleConversion.h:30` clamps through `AudioEngine::clip` (`AudioEngine.h:362-372`) before the multiply by `OUTPUT_SAMPLE_MULTIPLIER` = 32767 (`AudioEngine.h:81`), so the scaled value is bounded to ±32767. A sample of exactly +1.0 yields 32767 (`boundary-1.log`). The finding's "+1.0 becomes 32768" holds only if the multiplier were 32768, which the code does not use.
