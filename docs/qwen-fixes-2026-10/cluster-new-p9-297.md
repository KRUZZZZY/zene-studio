Verdict: FIXED

Cluster new-p9-297: "Random Monstro LFO can divide by zero at low sample rates" (Luna patrol, BUG 5).

## Finding and evidence

Random and smooth-random LFO modes take their modulo period from the LFO rate truncated to whole frames. The rate is the period in frames, `ms * 0.001 * samplerate`. Below 10 kHz the 0.1 ms minimum gives a period under one frame; `static_cast<int>` gives 0; the `%` divides by zero (SIGFPE, integer division).

- The rate in frames: [plugins/Monstro/Monstro.cpp:162](plugins/Monstro/Monstro.cpp:162) `m_lfo_rate[0] = ( m_parent->m_lfo1Rate.value() * 0.001f * m_parent->m_samplerate )`.
- The minimum: [plugins/Monstro/Monstro.cpp:903](plugins/Monstro/Monstro.cpp:903) constructs `m_lfo1Rate(1.0f, 0.1f, 10000.f, ...)`. `TempoSyncKnobModel` takes `(val, min, max, step, scale, ...)` ([include/TempoSyncKnobModel.h:62-63](include/TempoSyncKnobModel.h:62)), so 0.1 ms is the lower bound.
- The rate the instrument uses: [plugins/Monstro/Monstro.cpp:1483](plugins/Monstro/Monstro.cpp:1483) `m_samplerate = Engine::audioEngine()->outputSampleRate();`, called from the constructor at [plugins/Monstro/Monstro.cpp:1060](plugins/Monstro/Monstro.cpp:1060).
- Reachability: the configured base rate is clamped to at least 44100 ([src/core/AudioEngine.cpp:481](src/core/AudioEngine.cpp:481), [include/AudioEngine.h:84](include/AudioEngine.h:84)). 8 kHz therefore comes only from a device that reports it ([src/core/audio/AudioDevice.cpp:38](src/core/audio/AudioDevice.cpp:38), [include/AudioEngine.h:340-342](include/AudioEngine.h:340)). The code path is real; I did not verify a real backend that reports a rate below 10 kHz.
- The failing divisions before the fix: [plugins/Monstro/Monstro.cpp:769](plugins/Monstro/Monstro.cpp:769) (WAVE_RANDOM) and [plugins/Monstro/Monstro.cpp:777](plugins/Monstro/Monstro.cpp:777) (WAVE_RANDOM_SMOOTH). They are the only two modulo sites in the file (`rg ' % '`: lines 351 with constant 10, 769, 777).
- RED backtrace, from the log: `Received signal 8 (SIGFPE), code FPE_INTDIV` in `lmms::MonstroSynth::updateModulators ... plugins/Monstro/Monstro.cpp:769`, called from `renderOutput` at `Monstro.cpp:345`.

The finding's line 767 is line 769 in this tree (the statement moved by two lines).

The finding's test pointer, `tests/reference/Monstro/Monstro.cpp` with `ctest -R '^Monstro$'`, is wrong. No ctest is named `Monstro` (`ctest -N` in `build/tests`). That path is the extracted pre-migration reference copy that `PluginPortsMigrationReference` renders for the sample-exact comparison ([tests/CMakeLists.txt:3312-3319](tests/CMakeLists.txt:3312), [tests/CMakeLists.txt:3356](tests/CMakeLists.txt:3356)). I left it unchanged, because it is the reference the comparison is measured against.

## Fix

[plugins/Monstro/Monstro.cpp:701-703](plugins/Monstro/Monstro.cpp:701): `const int lfoPeriod = std::max( 1, static_cast<int>( m_lfo_rate[i] ) );`, computed once per LFO before the waveform switch. It replaces the two raw casts at [:773](plugins/Monstro/Monstro.cpp:773) and [:781](plugins/Monstro/Monstro.cpp:781).

- Behaviour outside the bug: for any rate of one frame or more, `max(1, n) == n`, so the period is identical. Only rates under one frame change, and those divided by zero before. Projects that rendered before render the same; no format, no command.
- Siblings: the smooth mode's float division `tm / m_lfo_rate[i]` ([:784](plugins/Monstro/Monstro.cpp:784)) is not a modulo. With `lfoPeriod == 1`, `tm` is always 0 and the interpolation is well defined. `m_lfo_rate` cannot be 0: the minimum is 0.1 ms and the sample rate is positive.
- Realtime: an integer `max` on the render path. No allocation, lock, wait or log.
- Registration: `tests/upstream-modifications.txt`, one line beside the existing Monstro entries (cluster new-p9-297).

## Regression test

[tests/src/core/NotePlaybackTest.cpp:105](tests/src/core/NotePlaybackTest.cpp:105) `DeviceRatePin` and the slot `monstroRandomLfoAtLowSampleRateRenders` (line 535).

- The seam is `AudioDevice::setSampleRateForTesting` ([include/AudioDevice.h:83](include/AudioDevice.h:83), `LMMS_TESTING` only), which the existing `FixedRateAudioDevice` test already uses. No wall clock, no sleeps, no races: the render is driven by `playNote` on a note handle, as the other slots in the file do.
- The slot pins the dummy device to 8000 Hz, creates a Monstro track with LFO 1 at 0.1 ms, and renders 16 periods for WAVE_RANDOM (9) and for WAVE_RANDOM_SMOOTH (10). It asserts every frame is finite and the output is not silent.
- `DeviceRatePin` restores the original rate and re-emits `sampleRateChanged` on scope exit, including when a `QVERIFY` fails.

Test matrix:
- Normal path (rates of one frame or more): unchanged; covered by `monstroLfoAttackRampsWithinPeriod` and by `PluginPortsMigrationTest::migratedPluginsPreserveBehaviour`, whose Monstro render matches the reference by SHA-256.
- Low-rate random modes: the new slot, both waves.
- Reconfiguration: the RAII pin restores the rate on every exit path.
- Audio thread: no processing is started; `playNote` is called directly, as the other slots do.
- Platform: offscreen Qt and the dummy device; no platform-specific code.
- Limit: with the fix reverted, the first crash is at wave 9. The second site (line 781) is covered by the GREEN run and by the shared cause, not by its own RED.

## Commands and exit codes

`LOGS` is `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-297.r1/logs`. Build commands run from the worktree; ctest from `build/tests`.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Build, first try | `cmake --build build --target monstro NotePlaybackTest -j4 > $LOGS/build-1.log 2>&1` | 2 | build-1.log: ccache could not create its temp file under `zene-bunny-fix/dsh/…/ccache-tmp-r6` ("Permission denied", write-jail). No compile error. |
| Build, ccache temp in allowed area | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target monstro NotePlaybackTest -j4 > $LOGS/build-2.log 2>&1` | 0 | build-2.log |
| RED, first draft (one wave, fix absent) | `cd build/tests && ctest -R '^NotePlaybackTest$' --output-on-failure > $LOGS/test-red-1.log 2>&1` | 8 | test-red-1.log: `Received signal 8 (SIGFPE), code FPE_INTDIV`, Monstro.cpp:769 |
| Fix applied, build | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target monstro NotePlaybackTest -j4 > $LOGS/build-3.log 2>&1` | 0 | build-3.log |
| GREEN, first draft | `ctest -R '^NotePlaybackTest$' --output-on-failure > $LOGS/test-green-1.log 2>&1` | 8 | `nesNoteStartDoesNotAllocate` FAIL: plugin "nes" not found. Same failure is in test-red-1.log lines 11-12, before any Monstro code runs. |
| Build NES plugin | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target nes -j4 > $LOGS/build-4.log 2>&1` | 0 | build-4.log |
| GREEN, first draft after NES build | `ctest -R '^NotePlaybackTest$' --output-on-failure > $LOGS/test-green-2.log 2>&1` | 0 | test-green-2.log |
| Final test, fix applied | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target monstro NotePlaybackTest -j4 > $LOGS/build-8.log 2>&1`, then `ctest -R '^NotePlaybackTest$' -V > $LOGS/test-green-4.log 2>&1` | 0, 0 | test-green-4.log: 30 PASS, 0 FAIL; `monstroRandomLfoAtLowSampleRateRenders` PASS |
| Proof A, RED on the final test | `git diff -- plugins/Monstro/Monstro.cpp > $LOGS/fix.patch`; `git apply -R $LOGS/fix.patch`; build as above (`build-9.log`); `ctest -R '^NotePlaybackTest$' --output-on-failure > $LOGS/test-red-3.log 2>&1` | 0, 0, 0, 8 | test-red-3.log: `Received signal 8 (SIGFPE), code FPE_INTDIV` |
| Proof B, GREEN on the final test | `git apply $LOGS/fix.patch`; build (`build-10.log`); `ctest -R '^NotePlaybackTest$' -V > $LOGS/test-green-5.log 2>&1` | 0, 0, 0 | test-green-5.log: 30 PASS, 0 FAIL |
| Family build | `CCACHE_TEMPDIR=$TMPDIR cmake --build build --target PluginPortsMigrationTest PluginPortsMigrationReference -j4 > $LOGS/build-5.log 2>&1` | 0 | build-5.log |
| Family run | `ctest -R 'PluginPortsMigration' -V > $LOGS/test-ppm-green.log 2>&1` | 8 | test-ppm-green.log: 82 PASS, 1 FAIL |

Family result, `PluginPortsMigrationTest`: `migratedPluginsPreserveBehaviour` PASS, with the Monstro render's SHA-256 equal to the reference. The one FAIL is `nonRealtimeLadspaEffectsAreInvalid`, which needs the SWH Decimator LADSPA fixture ("SWH Decimator test fixture was not loaded", test-ppm-green.log:205). No decimator plugin is built in this tree (`build/plugins/ladspa`). The slot does not touch Monstro. I did not run it on the unfixed tree.

NotePlaybackTest on the final tree: 30 PASS lines, 0 FAIL lines (test-green-4.log, test-green-5.log). The affected test count is 30 for NotePlaybackTest and 83 result lines for PluginPortsMigration (82 PASS, 1 FAIL).

## Gates

- `feature_check.py` could not produce its table. Its first run exited 1 (gates-1.log): it creates a log folder beside the worktree (`zene-qwen-new-p9-297-feature-check`), which is outside the write-jail (PermissionError). The script has no override, and I did not edit it.
- In its place, each gate script from the same list ran from the worktree, logs in `$LOGS/gate2-*.log`:
  - `bash tests/fork-sources-gate.sh`: EXIT 0
  - `bash tests/all-sources-reproduce.sh`: EXIT 0
  - `bash tests/no-upstream-regression-gate.sh`: EXIT 0
  - `tests/scripted/check-namespace`: EXIT 0
  - `bash tests/no-tautology-gate.sh`: EXIT 0
  - `python3 tests/doc-figures-gate.py`: EXIT 0
  - `bash tests/complexity-gate.sh --check`: EXIT 1. Seven REGRESSION lines, none in `Monstro.cpp` or `NotePlaybackTest.cpp`; the new lines are in other files.
  - `bash tests/file-length-gate.sh --check`: EXIT 1. One REGRESSION line in a changed file: `new file over 500 lines: tests/src/core/NotePlaybackTest.cpp (821)`.

The file-length item is carried over, not introduced. At HEAD the same file counts 768 code lines by the gate's rule (I applied the gate's awk to `git show HEAD:...`), so the base listing carries the same key. The file is not in the baseline, so it is flagged by key. My change adds 53 code lines to a file already over the limit. Whether `feature_check.py` classifies this as "not new" depends on the base listing it computes, which I could not compute here. I expect it to pass; that is unverified.

`Monstro.cpp` is 1423 code lines now, 1422 at HEAD, and is not flagged.

## Self-check

- The regression test fails with the product change reverted and passes with it: `git apply -R` + build + `ctest -R '^NotePlaybackTest$'` gives EXIT 8 with `Received signal 8 (SIGFPE), code FPE_INTDIV` (test-red-3.log). With the change re-applied it gives EXIT 0, 30 PASS, 0 FAIL (test-green-5.log).
- Deterministic: no wall-clock, sleep or race. The device rate comes from `setSampleRateForTesting` (`include/AudioDevice.h:83`, `LMMS_TESTING`); the note and the 16 periods are fixed; the output checks are exact (finite, peak greater than 0).
- The fix is at the cause and complete: the two modulo sites (`Monstro.cpp:773`, `:781`) share the one clamped period. `updateModulators` is called from one place, `renderOutput` at `:345`. Every other use of `m_lfo_rate` is a float operation that cannot divide by zero.
- No collateral damage: `max(1, n) == n` for every rate of one frame or more, so every rate that worked before computes the same period. The only changed case previously crashed. The sample-exact Monstro comparison (`PluginPortsMigrationTest`) still matches. No saved data, format or command changed.
- Verdict FIXED, so no ALREADY-FIXED or FALSE-POSITIVE evidence is needed.
