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

Each entry names its log. EXIT is copied from that log. "no EXIT line" means the log holds no exit code, so none is recorded for that command. The earlier draft's claims of exit codes "per the p4 record" or "per the p5 record" are withdrawn: no log backs them.

- p2 `syntax-p2.log`: compile before MOC generation failed (`AudioOssDeviceConfigTest.moc: No such file or directory`). EXIT: no EXIT line. Superseded by the builds below.
- p2 `reconfigure.log`: configure refused by the lane guard (`configuring is the parent's job`). EXIT: no EXIT line.
- p2 `build-1.log`: `cmake --build build --target AudioOssDeviceConfigTest`. EXIT: no EXIT line.
- p3 `build-2.log`: rebuild and link of the test. EXIT: no EXIT line.
- p3 `build-3.log`: build of the `zene` target. EXIT: no EXIT line.
- p3 `build-4.log`: build of `AudioOssWriteTest`. EXIT: no EXIT line.
- p2c RED before the product change, `test-1.log`: `ctest -R AudioOssDeviceConfigTest --output-on-failure`; the log lists `727 - AudioOssDeviceConfigTest (Failed)`. EXIT: no EXIT line.
- p3 GREEN `test-2.log`: `ctest -R AudioOssDeviceConfigTest --output-on-failure`. EXIT: no EXIT line.
- p3 GREEN `test-3.log`: `ctest -R AudioOss --output-on-failure`. EXIT: no EXIT line.
- p4 reversal `apply-reverse.log`: `git apply -R fix.patch`. EXIT=0.
- p4 reversed rebuild `build-5-reversed.log`: relink of `AudioOssDeviceConfigTest`. EXIT: no EXIT line.
- p4 RED with the fix reverted, `test-8-red.log`: `ctest -R AudioOssDeviceConfigTest --output-on-failure`; the log lists `727 - AudioOssDeviceConfigTest (Failed)` and two `/dev/dsp` lines. EXIT=8. `test-4-reversed.log` shows the same failure lines but has no EXIT line, so it is not used as an exit-code source.
- p4 re-apply `apply-restore.log`: `git apply fix.patch`. EXIT=0.
- p4 restored rebuild `build-6-restored.log`: relink of `AudioOssDeviceConfigTest`. EXIT: no EXIT line.
- p5 GREEN with the fix applied, quiet-run log `duet/12-worker/tmp/006-p5-green.log`: `ctest -R AudioOssDeviceConfigTest --output-on-failure`; `100% tests passed out of 1`. EXIT=0 (quiet-run's exit). This run was made in p5 for the exit code only; the diff is unchanged.
- Other ctest logs with no EXIT line: `test-5-restored.log`, `test-5-verbose.log`, `test-6-final.log`, `test-7-final.log`, `test-9-green.log`, `test-10-family.log`.
- Gates: `feature-check-3.log` `RESULT: PASS`, EXIT=0. `feature-check-1.log` `RESULT: PASS`, no EXIT line.
- Whitespace: no result is recorded. `whitespace-final.log` and `whitespace.log` are both empty (0 lines) and have no EXIT line.

## (d) Registration

- `tests/upstream-modifications.txt` declares `src/core/audio/AudioOss.cpp` as a fork modification for `cluster new-p12-314`.
- `tests/CMakeLists.txt` registers `src/core/AudioOssDeviceConfigTest.cpp` in `LMMS_TESTS` under `IF(LMMS_HAVE_OSS)`. It sets `QT_QPA_PLATFORM=offscreen` for this test only.
- `tests/fork-sources.txt` and `tests/all-sources.txt` list `tests/src/core/AudioOssDeviceConfigTest.cpp`.

## Self-check

1. Red and green exits. Red: `logs/test-8-red.log`, `ctest -R AudioOssDeviceConfigTest --output-on-failure` with the fix reverted (the state set by `apply-reverse.log`, EXIT=0), `727 - AudioOssDeviceConfigTest (Failed)`, two `/dev/dsp` lines, EXIT=8. Green: quiet-run log `duet/12-worker/tmp/006-p5-green.log`, `100% tests passed out of 1`, EXIT=0, with the fix applied (state set by `apply-restore.log`, EXIT=0). The green logs with no EXIT line are listed in (c) and are not used as exit sources.
2. Deterministic. Each test uses its own `QTemporaryDir` (`tests/src/core/AudioOssDeviceConfigTest.cpp:134`, `:155`, `:180`). Saved values are injected through `ConfigManager::setValue`. `AUDIODEV` is saved at `:109-110`, cleared at `:140` and `:161`, and restored in cleanup at `:122-126`. The test has no sleeps and no timing dependence. `src/core/AudioEngine.cpp:485` sets `m_renderOnly` from the constructor argument, and `:795-799` builds `AudioDummy` and `MidiDummy` when `m_renderOnly` is set. So `Engine::init(true)` (`:111`) opens no real device. Line numbers are from the earlier draft and were not re-read in p5.
3. Cause and completeness. `probeDevice` has exactly two call sites in `AudioOss.cpp`: `:78` (the constructor, which opens the device) and `:258` (the setup dialog). Both use the one fixed key. `AudioIoSetup.cpp:59` is the canonical table entry. The sibling keys were audited (`logs/sibling-audit.log` and the grep in (a)). `MidiOss` keeps `midioss`/`device` (`MidiOss.cpp:172`) and was not changed. Open item: `MidiAlsaSeq` (`Midialsaseq`, `MidiAlsaSeq.cpp:325`) and `MidiSndio` (`MidiSndio.cpp:154`) read mixed-case keys. I did not check their writers in this packet, so they are out of scope and unverified.
4. No collateral damage. The only saved data is `audiooss`/`device`, written by `saveSettings` (`AudioOss.cpp:287`), and the fix makes the probe honour it. The `AUDIODEV` then `/dev/dsp` fallback is unchanged and is covered by `emptyKeyKeepsEnvironmentPrecedence` (`:178-188`). No limit was added. The product diff is one line. The fix touches no audio-thread function: the changed line is in `probeDevice()`, which runs only from the constructor (`:78`) and the setup dialog (`:258`), never from the process or render path (see (e)).
5. Verdict evidence. The verdict is FIXED. It is backed by the red exit (EXIT=8, `test-8-red.log`) and the green exit (EXIT=0, `006-p5-green.log`). `test-5-verbose.log` has no EXIT line; its five-slot PASS result comes from the earlier draft and was not re-read in p5. The verdict does not depend on the claim-only path, so no code-reference verdict is needed.
6. Mutation controls.
   - Gate self-test, passed: `all-sources-self-test.log` and `all-sources-self-test-2.log` print `SELF-TEST PASS`. A dropped path (exit 1) and an underivable path (exit 1) are both refused.
   - fork-sources pair, not counted as a control: `mutation-fork-sources.log` (EXIT=0, fork-sources PASS) and `mutation-fork-sources-repaired.log` (EXIT=1, fork-sources FAIL). The logs do not say which file each run checked (`fork-sources.mutant` is 475972 bytes; `fork-sources.backup` is 476016 bytes). The file names suggest the reverse of what the results show, so the lead must check this pair before relying on it.
   - `mutation-report.log`: EXIT=1, a pre-submit run made while the tree was dirty. Its verdict-word FAIL is cleared by line 1 (FIXED). Its working-tree FAIL clears when this commit lands. It was not rerun.

## Other findings

- Fontconfig prints `No writable cache directories` (`/var/cache/fontconfig`, `~/.cache/fontconfig`, `~/.fontconfig`). This is a resource-lookup warning, not a test failure.
- `QWARN: Error loading icon pixmap "lcd_19green": "File not found"` in `savedDeviceIsProbedOnNextStartup`. This is a resource-lookup warning, not a test failure.
