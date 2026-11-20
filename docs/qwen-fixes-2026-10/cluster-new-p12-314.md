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

## (c) Commands and exit codes, this session's p2-p4 sequence

Each entry names the command and its log in `logs/`. EXIT is the last `EXIT=` line of that log. Only these logs are cited.

- p2 patch generation: `git show a518d354de -- src/core/audio/AudioOss.cpp` wrote `fix.patch` (772 bytes, non-empty). Log `fix-patch-exit.log`: EXIT=0.
- p2 apply-reverse: `git apply -R fix.patch`. Log `apply-reverse.log`: EXIT=0.
- p2 reversed build: log `build-7-reversed.log`: EXIT=0.
- p2 RED, fix reverted: `ctest -R AudioOssDeviceConfigTest --output-on-failure`. Log `test-8-red.log`: `0% tests passed, 1 tests failed out of 1`, `727 - AudioOssDeviceConfigTest (Failed)`, EXIT=8.
- p3 apply-restore: `git apply fix.patch`. Log `apply-restore.log`: EXIT=0.
- p3 final build: `cmake --build build --target zene AudioOssDeviceConfigTest`. Log `build-9-final.log`: EXIT=0.
- p3 focused GREEN: `ctest -R AudioOssDeviceConfigTest --output-on-failure`. Log `test-11-green.log`: `100% tests passed out of 1`, EXIT=0.
- p3 AudioOss family GREEN: `ctest -R AudioOss --output-on-failure`. Log `test-12-family.log`: `100% tests passed out of 2`, EXIT=0.
- p4 feature gate: log `feature-check-3.log`: `RESULT: PASS`, EXIT=0.
- p4 submit check: log `submit-check-3b.log`: EXIT=0.
- p4 mutation controls: see Self-check item 6 (logs `mutation-fork-sources.log`, `mutation-fork-sources-repaired.log`, `mutation-report.log`).
- Whitespace: `git diff --check`, log `whitespace-2.log`: EXIT=0, no output.

## (d) Registration

- `tests/upstream-modifications.txt` declares `src/core/audio/AudioOss.cpp` as a fork modification for `cluster new-p12-314`.
- `tests/CMakeLists.txt` registers `src/core/AudioOssDeviceConfigTest.cpp` in `LMMS_TESTS` under `IF(LMMS_HAVE_OSS)`. It sets `QT_QPA_PLATFORM=offscreen` for this test only.
- `tests/fork-sources.txt` and `tests/all-sources.txt` list `tests/src/core/AudioOssDeviceConfigTest.cpp`.

## Self-check

1. Red and green exits. Red: `logs/test-8-red.log`, `ctest -R AudioOssDeviceConfigTest --output-on-failure` with the fix reverted (the state set by `apply-reverse.log`, EXIT=0), `727 - AudioOssDeviceConfigTest (Failed)`, EXIT=8. Green: `logs/test-11-green.log`, `100% tests passed out of 1`, EXIT=0, with the fix applied (state set by `apply-restore.log`, EXIT=0). The family run `logs/test-12-family.log` gives `100% tests passed out of 2`, EXIT=0.
2. Deterministic. Each test uses its own `QTemporaryDir` (`tests/src/core/AudioOssDeviceConfigTest.cpp:134`, `:155`, `:180`). Saved values are injected through `ConfigManager::setValue`. `AUDIODEV` is saved at `:109-110`, cleared at `:140` and `:161`, and restored in cleanup at `:122-126`. The test has no sleeps and no timing dependence. `src/core/AudioEngine.cpp:485` sets `m_renderOnly` from the constructor argument, and `:795-799` builds `AudioDummy` and `MidiDummy` when `m_renderOnly` is set. So `Engine::init(true)` (`:111`) opens no real device. These line numbers were re-checked against the tree in this correction.
3. Cause and completeness. `probeDevice` has exactly two call sites in `AudioOss.cpp`: `:78` (the constructor, which opens the device) and `:258` (the setup dialog). Both use the one fixed key. `AudioIoSetup.cpp:59` is the canonical table entry. The sibling keys were audited (`logs/sibling-audit.log` and the grep in (a)). `MidiOss` keeps `midioss`/`device` (`MidiOss.cpp:172`) and was not changed. Open item: `MidiAlsaSeq` (`Midialsaseq`, `MidiAlsaSeq.cpp:325`) and `MidiSndio` (`MidiSndio.cpp:154`) read mixed-case keys. I did not check their writers in this packet, so they are out of scope and unverified.
4. No collateral damage. The only saved data is `audiooss`/`device`, written by `saveSettings` (`AudioOss.cpp:287`), and the fix makes the probe honour it. The `AUDIODEV` then `/dev/dsp` fallback is unchanged and is covered by `emptyKeyKeepsEnvironmentPrecedence` (`:178-188`). No limit was added. The product diff is one line. The fix touches no audio-thread function: the changed line is in `probeDevice()`, which runs only from the constructor (`:78`) and the setup dialog (`:258`), never from the process or render path (see (e)).
5. Verdict evidence. The verdict is FIXED. It is backed by the red exit (EXIT=8, `test-8-red.log`) and the green exit (EXIT=0, `test-11-green.log`). The verdict does not depend on the claim-only path, so no code-reference verdict is needed.
6. Mutation controls.
   - Gate self-test: `all-sources-self-test.log` and `all-sources-self-test-2.log` both print `SELF-TEST PASS`. A dropped path (exit 1) and an underivable path (exit 1) are both refused.
   - Literal one-manifest removal (`AudioOssDeviceConfigTest` removed from `tests/fork-sources.txt` only): `mutation-fork-sources.log`, EXIT=0, `fork-sources` PASS. This passes by design. `AudioOssDeviceConfigTest.cpp` is also registered in `tests/all-sources.txt:2833`, and the fork-sources gate accepts a file that any manifest lists. The mutant diff confirms the only change: `fork-sources.backup` (476016 bytes) and `fork-sources.mutant` (475972 bytes) differ by exactly the line `tests/src/core/AudioOssDeviceConfigTest.cpp` at `tests/fork-sources.txt:3977`.
   - Repaired mutation (removed from both `tests/fork-sources.txt` and `tests/all-sources.txt`): `mutation-fork-sources-repaired.log`, EXIT=1, `fork-sources` FAIL and `all-sources-reproduce` FAIL. The mutant diff confirms the only change in `all.mutant` (132950 bytes) against `all-sources.backup` (132994 bytes) is the line at `tests/all-sources.txt:2833`. Both manifests were restored after the run.
   - Report-verdict mutation: `mutation-report.log`, EXIT=1: `FAIL report ... the verdict word must be in its first lines`. The same run also shows `FAIL working tree clean`, because the tree was dirty when it ran. Line 1 is now FIXED, so the verdict-word item passes. The working-tree item clears when this commit lands. This run was not repeated. The report was restored after it.

## Other findings

- Fontconfig prints `No writable cache directories` (`/var/cache/fontconfig`, `~/.cache/fontconfig`, `~/.fontconfig`). This is a resource-lookup warning, not a test failure.
- `QWARN: Error loading icon pixmap "lcd_19green": "File not found"` in `savedDeviceIsProbedOnNextStartup`. This is a resource-lookup warning, not a test failure.
