Verdict: FIXED

new-p16-156: FIXED. `waveTableBandFromFreq()` converted `log2()` of a zero, negative, NaN or infinite frequency to `int`, which is undefined behaviour. `getWtSampleControl()` had the same conversion for a NaN frame. Both are now guarded, and finite inputs are unchanged.

## Decision and evidence

The finding is REAL on this tree.

- Band conversion: `include/Oscillator.h:242` (was `:235`) computes `static_cast<int>(std::ceil(12.0f * std::log2(ratio)))`. For `ratio` = 0, negative, NaN or +inf, `log2` gives -inf, NaN or +inf, and that cannot be converted to `int`.
- Probe on this x86 box (`$TMPDIR` probe program, `g++ -std=c++20 -O0`): freq = 0, -440, NaN and +inf all convert to `int` = -2147483648. The subsequent clamp then returns band 1 for all four, so +inf wrongly gets the lowest band. Other platforms saturate or produce 0 instead, so the result is platform-dependent.
- Frame conversion (sibling, same function): `include/Oscillator.h:189-195`. `absFraction(NaN)` is NaN (`include/lmms_math.h:81-84`), so a NaN frequency makes `m_phase` NaN (`src/core/Oscillator.cpp:557`, `:566`). The NaN sample then reaches `static_cast<f_cnt_t>(control.frame)` at `include/Oscillator.h:195` (`f_cnt_t` is `std::uint64_t`, `include/LmmsTypes.h:43`).
- Callers: `waveTableBandFromFreq` has one caller, `getWtSampleControl` (`include/Oscillator.h:197`). `getWtSampleControl` feeds every wavetable `wtSample` overload: triangle, saw, square, moog-saw and exponential (`src/core/Oscillator.cpp:734`, `:751`, `:768`, `:785`, `:802`), plus the user anti-alias table (`:829`). The sine path returns early for `current_freq >= MAX_FREQ` (`src/core/Oscillator.cpp:715`), but the other shapes have no frequency guard.
- Reachability: the chain from a zero or NaN note frequency or detune to `m_freq` was NOT proven by running the synth. The evidence is that the conversion is undefined for those inputs and that they reach it through the code above. The regression test exercises the function and the frame directly.

## Fix (`include/Oscillator.h`)

- `waveTableBandFromFreq` (`:231-242`): `ratio = freq / 440.0f` is computed once. `!(ratio > 0)` (zero, negative, NaN, or a positive frequency that underflows to 0) returns band 1. `isinf(ratio)` returns the top band, `WAVE_TABLES_PER_WAVEFORM_COUNT-1` = 127. Otherwise the original expression runs with `ratio` in place of `freq / 440.0f`, so finite results are unchanged.
- Choices, which the reviewer may redirect:
  - Zero, negative and NaN get band 1. This matches the current x86 output and the clamp floor.
  - +inf gets the top band (127). This is the limit of the existing clamp: any finite frequency above the range already gets 127, for example 1e30 (tested).
  - A NaN frame gets frame 0, so `f1` = 0 and `f2` = 1. This is a defined value, and it avoids an out-of-range table index.
- `getWtSampleControl` (`:194`): `if (!(control.frame >= 0.0f)) { control.frame = 0.0f; }`. It only changes NaN frames. Finite frames are unchanged.
- Not changed, noted: `userWaveSample` (`include/Oscillator.h:172-179`) converts `frame` the same way on the non-wavetable user-wave path. It is not on the finding's wavetable path, so it is left for a separate cluster. Also pre-existing and not changed: `absFraction` can round to exactly 1.0, which gives `f1 = WAVETABLE_LENGTH`, one past the end.

## Regression test (`tests/src/plugins/TripleOscillatorUserWaveTest.cpp`)

The finding's suggested test files `^TripleOscillator$` and `^Organic$` do not exist in this build tree. `ctest -N` lists no such names. The reference modules are built as `partc_ref_*` and checked by `PluginPortsMigrationTest`. I put the regression in `TripleOscillatorUserWaveTest`, which already includes `Oscillator.h` and is registered in `tests/CMakeLists.txt:3944`.

- `waveTableBandIsDefinedForNonFiniteAndNonPositiveFrequencies` (`:120-140`): finite inputs keep 57, 69 and 81; 1e-30 gives 1; 1e30 gives 127; 0, -440 and NaN give 1; +inf gives 127 (`:138`).
- `wavetableFrameIsDefinedForNaNSample` (`:144-162`): constructs `Oscillator(nullptr, nullptr, ...)`, which only stores references (`src/core/Oscillator.cpp:55-77`). A finite 0.5 gives `f1` = 1223 and `f2` = 1224 (`WAVETABLE_LENGTH` = 2446, from `OscillatorConstants.h:39`). NaN gives frame 0 (`:160`), `f1` 0 and `f2` 1.
- Both are deterministic: no timing, sleeps or threads.

## Commands and results

All paths are under `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-156.r1/logs`, abbreviated to `LOG/`. Build cache: `CCACHE_TEMPDIR` was set to `$TMPDIR/ccache-tmp-new-p16-156`. ccache's default temp dir (`.../dsh/home/ccache-tmp-r6`) is outside the write-jail and failed with `Permission denied` (`LOG/build-1-red.log:19-22`). This is an environment issue, not a code issue.

| Evidence | Command | Exit | Result |
| --- | --- | --- | --- |
| RED, band assertion (fix absent) | `cmake --build build --target TripleOscillatorUserWaveTest -j4 > LOG/build-2-red.log 2>&1; echo $?`, then `cd build/tests && ctest -R '^TripleOscillatorUserWaveTest$' --output-on-failure > LOG/test-2-red.log 2>&1; echo $?` | build 0 / test **8** | `FAIL! ... waveTableBandIsDefined...`, `Actual 1, Expected 127`, `TripleOscillatorUserWaveTest.cpp(138)`. 3 passed, 1 failed. |
| RED, frame assertion (band fix in, frame fix absent) | `cmake --build build --target TripleOscillatorUserWaveTest -j4 > LOG/build-5-band-only.log 2>&1; echo $?`, then ctest as above, `LOG/test-5-band-only.log` | build 0 / test **8** | `FAIL! ... wavetableFrameIsDefinedForNaNSample`, `Actual nan`, `Expected 0`, `TripleOscillatorUserWaveTest.cpp(160)`. |
| Proof, reverse fix | `git diff -- include/Oscillator.h > LOG/fix.patch && git apply -R LOG/fix.patch`, then rebuild target and ctest, `LOG/test-7-proof-nofix.log` | apply 0 / 0; build 0; test **8** | Both slots fail: `(138)` `Actual 1, Expected 127` and `(160)` `nan.frame` is NaN. |
| Full rebuild with fix | `cmake --build build --target zene TripleOscillatorUserWaveTest OrganicParamRaceTest OrganicZeroOscTest Lb302NotePathTest UndoHonestyTest UndoHonestyStateTest PluginPortsMigrationReference PluginPortsMigrationTest partc_ref_tripleoscillator partc_ref_organic partc_ref_nes partc_ref_monstro partc_ref_kicker partc_ref_lb302 -j4 > LOG/build-6-green-full.log 2>&1` | **0** | Includers of `Oscillator.h` rebuilt. No `error` lines in the log. |
| GREEN | `cd build/tests && ctest -R '^TripleOscillatorUserWaveTest$' --output-on-failure > LOG/test-6-green.log 2>&1` | **0** | `Test #607: TripleOscillatorUserWaveTest ... Passed`, 100%, 1/1. |
| Proof, restore fix | `git apply LOG/fix.patch`, rebuild target, ctest, `LOG/test-8-proof-fix.log` | apply 0; build 0; test **0** | Passed again. |
| Oscillator-adjacent families | `ctest -R '^(TripleOscillatorUserWaveTest\|OrganicParamRaceTest\|OrganicZeroOscTest\|Lb302NotePathTest\|UndoHonestyTest\|UndoHonestyStateTest\|PluginPortsMigrationTest\|PluginPortsMigrationReference)$' ...` > `LOG/test-9-family.log` | **8** | 7 tests executed: 6 passed, `PluginPortsMigrationTest` failed. |

Family detail: `PluginPortsMigrationTest` fails in `nonRealtimeLadspaEffectsAreInvalid` (`tests/src/plugins/PluginPortsMigrationTest.cpp:1596`): `"SWH Decimator test fixture was not loaded"`. The Decimator LADSPA module is not built in this tree (`find build -iname "*decimator*"` finds only a CMake directory). The test is about LADSPA and does not touch oscillators. I did NOT build the original code to confirm the failure is pre-existing, so treat it as an unverified baseline failure. The other 85 test cases in that binary pass. `PluginPortsMigrationReference` is a helper binary, not a ctest.

Gates:

- `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` exited **1** before running any check: `logs.mkdir()` on `<worktree>-feature-check` (outside the write-jail) raised `PermissionError` (see `LOG/gates-1-feature-check.log`). The script was not changed.
- I ran the same commands it lists, from the worktree, with logs in `LOG/`:

| Gate | Command | Exit |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | **1** |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | **1** |

The two ratchet failures are train-wide, as the lane notes say. Their REGRESSION lines name other files (`tests/src/core/VideoTrackTest.cpp`, `tests/src/gui/ArrangerTimelineTest.cpp`, and others). None names `include/Oscillator.h` or `TripleOscillatorUserWaveTest.cpp`. I could not run the script's baseline comparison because its baseline cache lives outside the write-jail.

Registration: `tests/upstream-modifications.txt` gains one line for `include/Oscillator.h`, since it is an inherited LMMS file. The test file is already in `tests/fork-sources.txt:2867` and `tests/all-sources.txt:3254`.

## Files committed

- `include/Oscillator.h`
- `tests/src/plugins/TripleOscillatorUserWaveTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p16-156.md`

## Self-check

Verdict is FIXED, so the ALREADY-FIXED / FALSE-POSITIVE item does not apply.

- **Regression fails with the fix reverted and passes with it.** Reverted: `LOG/test-2-red.log` (band assertion, exit 8), `LOG/test-5-band-only.log` (frame assertion, exit 8), `LOG/test-7-proof-nofix.log` (both assertions, exit 8, after `git apply -R LOG/fix.patch` and rebuild). Restored: `LOG/test-8-proof-fix.log` (exit 0, `1/1 Passed`). The reverted run stops at the first failed `QCOMPARE` in each slot, so the exact failing lines are `TripleOscillatorUserWaveTest.cpp:138` and `:160`.
- **Deterministic.** The test calls the static `waveTableBandFromFreq` directly and builds `Oscillator` with null models and caller-owned `float` references (`src/core/Oscillator.cpp:55-77` only stores them). Inputs are literal values (`220`, `440`, `880`, `1e-30`, `1e30`, `0`, `-440`, NaN, `+inf`, sample `0.5`, sample NaN). There are no threads, sleeps, or timing. `Engine::init(true)` runs in the existing `initTestCase` and only provides the sample rate for `getWtSampleControl`.
- **Complete at the cause.** `waveTableBandFromFreq` has one caller, `getWtSampleControl` (`include/Oscillator.h:197`). `getWtSampleControl` feeds every wavetable `wtSample` overload (`src/core/Oscillator.cpp:734,751,768,785,802,829`). Siblings in the same function were checked: the band conversion (`:242`) and the frame conversion (`:195`) are both guarded. Left out on purpose and noted above: `userWaveSample` (`include/Oscillator.h:172-179`), on a non-wavetable path that the finding does not cover.
- **No collateral damage.** Finite inputs are asserted to keep their result: bands 57/69/81, sample 0.5 to frames 1223/1224. For every finite positive `ratio` the code runs the original expression on the same float, so the result is identical. The guards only trigger for a non-positive, NaN or infinite ratio, or a NaN frame. Nothing is serialised, no limit is added that other callers could hit, and no saved data path is touched. The adjacent test binaries pass: `UndoHonestyTest`, `UndoHonestyStateTest`, `Lb302NotePathTest`, `OrganicParamRaceTest`, `OrganicZeroOscTest` (`LOG/test-9-family.log`). `PluginPortsMigrationTest` has one failure caused by a missing LADSPA fixture, described above.
- **Gates.** Six simple gates exit 0. The two ratchets exit 1 for train-wide items, none in my files (`LOG/gate-complexity.log`, `LOG/gate-file-length.log`). `feature_check.py` itself cannot run (write-jail on its log folder, `LOG/gates-1-feature-check.log`), so the submit check's `gates (feature_check.py)` FAIL is an environment result. The submit check's other items PASS.
- **Realtime.** The new code is one float comparison, `std::isinf`, and one `if` on the per-sample path. No allocation, lock, log, or call that the audio thread does not already make.
- **Undo and commands.** Nothing changed. No control command was added, so the ControlCommandsSnapshot canary is not affected.
- **Unverified.** Whether a zero or NaN frequency reaches the wavetable path through a running synth (not run end to end). The `PluginPortsMigrationTest` Decimator failure's baseline (the original code was not built).
