**Verdict: FIXED**

# Cluster new-p20-43: DrumSynth noise-band step from a negative frequency

The finding is real. A negative `[NoiseBand] F` (or `NoiseBand2` F) makes `BF` negative, `sqrt(BF)` is NaN, and the step quotient becomes NaN. That NaN went straight into `static_cast<int>` with no check. The step is computed whether or not the band is on, so loading such a `.ds` file hits the undefined conversion on every render.

A prior fix (`new-p10-140`, [upstream-modifications.txt:2398](../../tests/upstream-modifications.txt#L2398)) clamps the step with `std::max(..., 1)` after the cast. That clamp runs after the undefined conversion, so it does not close the bug.

## Evidence

- Original cast sites (the finding's line 552 has moved): the step was `1 + static_cast<int>(...)` followed by `std::max(..., 1L)`. The two sites are now [DrumSynth.cpp:597](../../src/core/DrumSynth.cpp#L597) (first band) and [DrumSynth.cpp:610](../../src/core/DrumSynth.cpp#L610) (second band). Both call the new `noiseBandStepCount`.
- The conversion now lives in [DrumSynth.cpp:387](../../src/core/DrumSynth.cpp#L387) (`DrumSynth::noiseBandStepCount`). A NaN quotient returns 1. Other quotients are clamped into `[0, INT_MAX-1]` before the cast, so the cast is always defined. For every value the old code converted defined (quotients from 0 up to `INT_MAX-1`, and any negative quotient) the result is unchanged: `1 + trunc(q)`, floored at 1.
- The declaration is at [DrumSynth.h:44](../../include/DrumSynth.h#L44), public static, so the regression test can call it.
- Regression: [DrumSynthTest.cpp:440](../../tests/src/core/DrumSynthTest.cpp#L440) (`noiseBandStepCountStaysDefinedForOutOfRangeQuotients`) checks 41.7→42, -3.0→1, NaN→1, +inf→INT_MAX, 1e300→INT_MAX.
- Regression: [DrumSynthTest.cpp:452](../../tests/src/core/DrumSynthTest.cpp#L452) (`negativeNoiseBandFrequencyRendersLikeTheBandOff`) renders a file with a disabled band at `F=-100` and checks it matches the same file without the band.
- Registration: [upstream-modifications.txt](../../tests/upstream-modifications.txt) gains entries for `src/core/DrumSynth.cpp` and `include/DrumSynth.h`. `DrumSynthTest.cpp` is already in `tests/fork-sources.txt`.

## Why the RED needed a seam

On this x86 build, `static_cast<int>(NaN)` and `static_cast<int>(+inf)` produce `INT_MIN`, and the minimum clamp then turns that into 1. So the bug is invisible in rendered output unless a sanitiser or a value-level check catches it. The regression therefore calls the conversion directly and asserts the defined result for the out-of-range inputs. The RED stage used the original arithmetic inside the seam, so the assertion failed on the real undefined behaviour.

## Choice made

Non-representable quotients (+inf, or anything at or above `INT_MAX-1`) now give `INT_MAX`, meaning "never retrigger". On this x86 build they gave 1 before, and on saturating-cast platforms they gave `INT_MAX`, so one platform's behaviour changes. I picked saturation as the defined meaning of a huge step count. The owner can redirect this to 1 if preferred.

## Commands and recorded results

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p20-43.r1/logs`. Build and test commands ran with `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p20-43`, because the shared ccache config points its temp dir outside the write-jail. Ctest ran from `build/tests`.

| Phase | Command | EXIT | Result |
|---|---|---:|---|
| First build attempt (no ccache override) | `cmake --build build --target zene DrumSynthTest -j4 > .../build-red-1.log 2>&1` | 2 | ccache temp dir not writable under the jail; no code error. |
| RED build | same, with `CCACHE_TEMPDIR` set, log `build-red-2.log` | 0 | Built with the seam and original arithmetic. |
| RED test | `ctest -R '^DrumSynthTest$' --output-on-failure > .../test-red-1.log 2>&1` | 8 | `noiseBandStepCount(+inf)` returned 1, expected 2147483647 (`DrumSynthTest.cpp:448`). |
| GREEN build | `cmake --build build --target zene DrumSynthTest -j4 > .../build-green-1.log 2>&1` | 0 | |
| GREEN test | `ctest -R '^DrumSynthTest$' --output-on-failure > .../test-green-1.log 2>&1` | 0 | 1/1 passed. |
| Save fix | `git diff -- src/core/DrumSynth.cpp include/DrumSynth.h > .../fix.patch`; `cp src/core/DrumSynth.cpp .../DrumSynth.guarded.cpp` | 0 / 0 | |
| Reverse proof build | after reverting the guard body to the original arithmetic, `cmake --build build --target zene DrumSynthTest -j4 > .../build-reverse-1.log 2>&1` | 0 | |
| Reverse proof test | `ctest -R '^DrumSynthTest$' --output-on-failure > .../test-reverse-1.log 2>&1` | 8 | Same assertion fails: `+inf` returns 1. |
| Restore | `cp .../DrumSynth.guarded.cpp src/core/DrumSynth.cpp`; `git diff -- src/core/DrumSynth.cpp include/DrumSynth.h \| cmp - .../fix.patch` | 0 / 0 | The restored diff is byte-identical to the saved patch. |
| Restore build | `cmake --build build --target zene DrumSynthTest -j4 > .../build-restore-1.log 2>&1` | 0 | |
| Restore test | `ctest -R '^DrumSynthTest$' -V > .../test-restore-1.log 2>&1` | 0 | `Totals: 18 passed, 0 failed`. |
| Family build | `cmake --build build --target DrumSynthLevelTest SampleBufferResampleTest ImportDetectionTest -j4 > .../build-family-1.log 2>&1` | 0 | Targets that include `DrumSynth.h`. |
| Family test | `ctest -R 'DrumSynth\|SampleBufferResample\|ImportDetection' --output-on-failure > .../test-family-1.log 2>&1` | 0 | 4/4 passed: DrumSynthLevelTest, ImportDetectionTest, SampleBufferResampleTest, DrumSynthTest. |

The literal protocol step (`git apply -R` of the whole product patch) was not used. At HEAD the seam does not exist, so reverting the full patch would stop the test compiling and would not show the assertion. The reverse proof above reverts only the guard body and gives an assertion failure.

### Gates

`feature_check.py` could not run here. It creates `<worktree>-feature-check/` next to the worktree, which is outside the write-jail, and fails with `PermissionError` (`.../logs/feature-check-1.log`, EXIT 1). I ran each gate command it runs directly from the worktree instead. This is a manual equivalent, not a `feature_check.py` PASS.

| Gate | Command | EXIT |
|---|---|---:|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

The complexity and file-length ratchets are red on the train, as stated in the brief. Their REGRESSION lines (`gate-7.log`, `gate-8.log`) name `WasmSandbox.cpp`, `XyPadPairTest.cpp`, `DawProjectReadTrac…`, `ControlCommandsMixer*`, `ControlCommandsSessionR…`, `Vst3Host.cpp`, `TakeLane.cpp`. None names a file this branch changes (`DrumSynth.cpp`, `DrumSynth.h`, `DrumSynthTest.cpp`, `upstream-modifications.txt`). I did not have the base listings to compare keys exactly, so this is "no item in a changed file", not a full baseline diff.

## Files in the commit

- `include/DrumSynth.h`
- `src/core/DrumSynth.cpp`
- `tests/src/core/DrumSynthTest.cpp`
- `tests/upstream-modifications.txt`
- `docs/qwen-fixes-2026-10/cluster-new-p20-43.md`

## Self-check

- **Test fails with the change reverted and passes with it.** RED `test-red-1.log` EXIT 8 with the original arithmetic; GREEN `test-green-1.log` EXIT 0. Reverse proof `test-reverse-1.log` EXIT 8 after the guard body was reverted, then restored (`cmp` EXIT 0) and `test-restore-1.log` EXIT 0 with 18/18 slots passing.
- **Deterministic.** The seam test calls a pure function with fixed doubles (NaN, +inf, 1e300, 41.7, -3.0). The render test writes fixed bytes and compares two renders. No timing, sleeps or threads.
- **Fix at the cause, all callers checked.** Two callers ([DrumSynth.cpp:597](../../src/core/DrumSynth.cpp#L597), [610](../../src/core/DrumSynth.cpp#L610)), both changed. Header includers (`DrumSynthLevelTest`, `ImportDetectionTest`, `SampleBufferResampleTest`, `DrumSynthTest`, `SampleDecoder.cpp`) were rebuilt; the family passed 4/4. Sibling not changed: [DrumSynth.cpp:985](../../src/core/DrumSynth.cpp#L985) `static_cast<int>(DF[j] / DAtten)`. `DAtten = exp2(2*bits)` with bits gated to 0..7 is at least 1, but I did not bound the magnitude of `DF`, so it is left for a separate check. Residual, read from code and not run: with the band **on** and negative F, `BQ` and `BPhi` become NaN and the output sanitiser zeroes those samples. That is audible but not undefined behaviour, so I left it alone; the owner should decide whether negative noise-band frequencies should fall back to the default, as non-finite F1/F2 already do.
- **No collateral damage.** In-range quotients (from 0 up to `INT_MAX-1`, and any negative quotient) give the same step as before, `1 + trunc(q)` floored at 1. NaN still gives 1. Only non-representable quotients change, and only in the way the choice above describes. No file format or saved-data path changed. The helper runs at parse time, not on the audio thread: no allocation, lock or log.
- **Verdict FIXED**, so the ALREADY-FIXED / FALSE-POSITIVE evidence item does not apply.
