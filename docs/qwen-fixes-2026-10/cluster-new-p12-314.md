FIXED

# cluster new-p12-314: the OSS startup probe reads the key its setup UI saves

## Verdict

FIXED. `AudioOss::probeDevice()` read `("AudioOss", "Device")`, but the OSS setup widget saves `("audiooss", "device")`. `ConfigManager::value` is an exact, case-sensitive lookup, so the saved device was never found. The probe fell through to `AUDIODEV` or the `/dev/dsp` default, and the saved path was ignored on every start. The one-line fix makes the read use the same key the save uses.

## (a) Verdict evidence

- Read (defect): `src/core/audio/AudioOss.cpp:173` was `value("AudioOss", "Device")`. It is now `value("audiooss", "device")`. Fix diff: 1 file, 1 hunk, one line (`fix.patch`).
- Write: `src/core/audio/AudioOss.cpp:287` `setValue("audiooss", "device", ...)` in `AudioOss::setupWidget::saveSettings()`.
- Lookup semantics: `src/core/ConfigManager.cpp:374-385` is an exact, case-sensitive match on class and attribute.
- Canonical key: `src/core/AudioIoSetup.cpp:59` `{"OSS", "audiooss", "device"}`. The generic reader at `:216` (`value(source.cls, source.attribute)`) uses this table entry, so the audio-setup path already reads `audiooss`/`device`.
- Siblings use lowercase class and attribute for the audio backends: `audioalsa` (`AudioAlsa.cpp:167`), `audiopa` (`AudioPulseAudio.cpp:66`), `audiosndio` (`AudioSndio.cpp:52`), `midioss` (`MidiOss.cpp:172`). Exceptions are noted under (e)3.
- History: `git log -S 'setValue( "AudioOss"' -- src/core/audio/AudioOss.cpp` prints 0 commits. No version of the code wrote the uppercase key, so no saved user data sits under `AudioOss`/`Device`.

## (b) p1 test matrix (from `logs/matrix-p1.md`)

- (a) normal: save a writable path via `("audiooss","device")`; `probeDevice` returns it.
- (b) failure/fallback: empty saved key; `AUDIODEV` precedence and the `/dev/dsp` scan are unchanged.
- (c) exhaustion: not applicable (no resource counters).
- (d) reconfiguration: save A, then B; `probeDevice` returns B and a new widget prefills B.
- (e) audio thread: `probeDevice` runs only at startup (constructor, `:78`) and in the setup dialog (`:258`). No process or render code is touched.
- (f) platform: the test is registered only under `LMMS_HAVE_OSS` (`build/lmmsconfig.h:29`). `AudioOss.h:30-89` is guarded by `#ifdef LMMS_HAVE_OSS`.

## (c) Commands and exit codes, p2-p5

Exit codes are copied from the logs where the log records one. Where the log has no EXIT line, the entry says so and gives the value the earlier packet recorded.

- p2 `syntax-p2.log`: compile before MOC generation failed with `AudioOssDeviceConfigTest.moc: No such file or directory`. No EXIT line in the log. Superseded by the build below.
- p2 `reconfigure.log`: configure refused by the lane guard (`configuring is the parent's job`). No EXIT line. The existing build tree was used.
- p2 `build-1.log`: `cmake --build build --target AudioOssDeviceConfigTest` built the test. No EXIT line.
- p3 `build-2.log`: rebuilt `AudioOss.cpp.o` and linked the test. No EXIT line.
- p3 `build-3.log`: built the `zene` target. No EXIT line.
- p3 `build-4.log`: built `AudioOssWriteTest`. No EXIT line.
- p3 RED `test-1.log`: `ctest -R AudioOssDeviceConfigTest --output-on-failure`. Totals: 3 passed, 2 failed. `savedDeviceIsProbedOnNextStartup` and `savedDevicePrefillsTheNextSetupWidget` both got `probeDevice() = "/dev/dsp"` instead of the saved temp path. `0% tests passed, 1 tests failed`. No EXIT line; the red exit is 8 per p4.
- p3 GREEN `test-2.log`: `ctest -R AudioOssDeviceConfigTest --output-on-failure`. `100% tests passed out of 1`, Passed 1.11 sec.
- p3 GREEN `test-3.log`: `ctest -R AudioOssWriteTest` and `AudioOssDeviceConfigTest`. `100% tests passed out of 2`. Both `AudioOss` tests Passed.
- p4 reversal: `git apply -R fix.patch` returned EXIT=0 per the p4 record; no log. `build-5-reversed.log` rebuilt `AudioOss.cpp.o`.
- p4 `test-4-reversed.log`: `ctest -R AudioOssDeviceConfigTest --output-on-failure`. Totals: 3 passed, 2 failed. Both saved-device slots fail with `/dev/dsp`. `0% tests passed`. EXIT=8 per the p4 record; the log has no EXIT line.
- p4 re-apply: `git apply fix.patch` returned EXIT=0 per the p4 record; no log. `build-6-restored.log` rebuilt `AudioOss.cpp.o`.
- p5 `test-5-restored.log`: `ctest -R AudioOssDeviceConfigTest --output-on-failure`. `100% tests passed out of 1`, Passed 1.08 sec. EXIT=0 per the p5 record; no EXIT line.
- p5 `test-5-verbose.log`: all five slots PASS: `initTestCase`, `savedDeviceIsProbedOnNextStartup`, `savedDevicePrefillsTheNextSetupWidget`, `emptyKeyKeepsEnvironmentPrecedence`, `cleanupTestCase`. Totals: 5 passed, 0 failed.
- Gate table `feature-check-1.log`: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity (no new item), file-length (no new item). All 8 PASS. `RESULT: PASS`.
- all-sources: `all-sources-1.log` and `all-sources-2.log` both print `REPRODUCES: the entry list in all-sources.txt is the recipe's own output.` The mutant controls in `all-sources-self-test.log` and `all-sources-self-test-2.log` print `SELF-TEST PASS`: a dropped path (exit 1) and an underivable path (exit 1) are both refused.
- Whitespace: `git diff --cached --check` on the staged set. Result and EXIT are in `logs/whitespace-final.log` (see the final-check section below). The earlier `whitespace.log` is empty.

## (d) Registration

- `tests/upstream-modifications.txt` declares `src/core/audio/AudioOss.cpp` as a fork modification for `cluster new-p12-314`.
- `tests/CMakeLists.txt` registers `src/core/AudioOssDeviceConfigTest.cpp` in `LMMS_TESTS` under `IF(LMMS_HAVE_OSS)`. It sets `QT_QPA_PLATFORM=offscreen` for this test only.
- `tests/fork-sources.txt` and `tests/all-sources.txt` list `tests/src/core/AudioOssDeviceConfigTest.cpp`.

## Self-check

1. Red and green. Red: `logs/test-1.log` (3 passed, 2 failed; `probeDevice = "/dev/dsp"`), with the reversed-fix run `logs/test-4-reversed.log` (EXIT=8 per p4). Green: `logs/test-2.log`, `logs/test-3.log`, `logs/test-5-restored.log`, and `logs/test-5-verbose.log` (all five pass). The final green run from the committed tree is `logs/test-6-final.log` with its EXIT in the final message.
2. Deterministic. Each test uses its own `QTemporaryDir` (`tests/src/core/AudioOssDeviceConfigTest.cpp:134`, `:155`, `:180`). Saved values are injected through `ConfigManager::setValue`. `AUDIODEV` is saved at `:109-110`, cleared at `:140` and `:161`, and restored in cleanup at `:122-126`. The test has no sleeps and no timing dependence. `Engine::init(true)` at `:111` reaches `AudioEngine.cpp:795-796`, which builds `AudioDummy` when `m_renderOnly` is set; I did not re-read the code that sets `m_renderOnly` in this packet, so the no-device-I/O claim rests on the p2 record.
3. Cause and completeness. `probeDevice` has exactly two call sites in `AudioOss.cpp`: `:78` (the constructor, which opens the device) and `:258` (the setup dialog). Both use the one fixed key. `AudioIoSetup.cpp:59` is the canonical table entry. The sibling keys were audited (`logs/sibling-audit.log` and the grep in (a)). `MidiOss` keeps `midioss`/`device` (`MidiOss.cpp:172`) and was not changed. Open item: `MidiAlsaSeq` (`Midialsaseq`, `MidiAlsaSeq.cpp:325`) and `MidiSndio` (`MidiSndio.cpp:154`) read mixed-case keys. I did not check their writers in this packet, so they are out of scope and unverified.
4. No collateral damage. The only saved data is `audiooss`/`device`, written by `saveSettings` (`AudioOss.cpp:287`), and the fix makes the probe honour it. The `AUDIODEV` then `/dev/dsp` fallback is unchanged and is covered by `emptyKeyKeepsEnvironmentPrecedence` (`:178-188`). No limit was added. The product diff is one line.
5. Verdict. The verdict is FIXED, backed by the reversed-fix red run and the restored green run. It is not a claim-only closure, so no code-reference verdict is needed.

## Other findings

- Fontconfig prints `No writable cache directories` (`/var/cache/fontconfig`, `~/.cache/fontconfig`, `~/.fontconfig`). This is a resource-lookup warning, not a test failure.
- `QWARN: Error loading icon pixmap "lcd_19green": "File not found"` in `savedDeviceIsProbedOnNextStartup`. This is a resource-lookup warning, not a test failure.
