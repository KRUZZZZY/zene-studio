FIXED

# cluster new-p9-503 (2026-10): Vibed string model can build a zero-length oversampling buffer

Finding: `plugins/Vibed/VibratingString.cpp:39` (historical line; the integer division is now at lines 43-48 in `stepsPerSample`, called from the initializer at line 54).

## Verdict: REAL, fixed

The defect is real on this branch (`639e1d23e`) and reachable with default-range settings.

### Evidence (file:line on the working tree)

- `plugins/Vibed/VibratingString.cpp:43-48` (new helper): `2 * setting / static_cast<int>(ratio)` with `ratio = max(sampleRate / baseSampleRate(), 1)`, and the result clamped to at least 1.
- `plugins/Vibed/VibratingString.cpp:54`: the member initializer now calls the helper instead of the bare integer division.
- The length model is `FloatModel(1, 1, 16, 1, ...)` at `plugins/Vibed/Vibed.cpp:119`, so the setting is in 1..16.
- `plugins/Vibed/Vibed.cpp:229-241`: the setting reaches the constructor from `playNoteImpl`, which is the audio thread.
- Before the fix, the count was `2*setting / (outputRate/baseRate)`. For setting 1 and an output rate of 4x the base (e.g. 192 kHz output on the 44.1 kHz base), the count is `2/4 = 0`. Then `m_outsamp` is a zero-length buffer, and `nextSample()` (`plugins/Vibed/VibratingString.h:55-76`) returns `m_outsamp[m_choice]` with `m_choice = 0`: an out-of-bounds read.
- The finding's "note creation may fail in `fastRand(0)`" does not happen: `fastRand(int)` with upper 0 returns 0 (`include/lmms_math.h:111-115`). The real failure is the out-of-bounds read in `nextSample()`.
- Second defect on the same expression: an output rate below the base rate makes `sampleRate / baseSampleRate()` zero, so the integer division is by zero (SIGFPE, reproduced below). The helper treats such a rate as the base rate.
- Behaviour kept: every ratio that already gave a count of at least 1 gives the same count (checked by `unchangedWhereTheRateFits`: ratio 1 setting 1 -> 2, ratio 2 setting 1 -> 1, ratio 2 setting 3 -> 3, ratio 4 setting 4 -> 2).

### Regression target

The finding named `tests/reference/Vibed/VibratingString.cpp` and `Vibed.cpp`. Those are reference copies that are not compiled into any ctest target; `ctest -N -R '^VibratingString$'` finds no test (see the commands below). The regression is a new test, `tests/src/plugins/VibratingStringTest.cpp` (ctest name `VibratingStringTest`), registered in `tests/CMakeLists.txt` with the string sources compiled into it, the same shape as `VibedWaveformHandoffTest`.

The test reads `oversampleCount()`, a new read-only accessor on `VibratingString` (`plugins/Vibed/VibratingString.h:53`). It is the test seam: calling `nextSample()` on a zero-length buffer is undefined, so the count is checked directly. The accessor is not a control command.

### Test matrix

| Path | Test function | Result before fix | Result after fix |
| --- | --- | --- | --- |
| Reported case: ratio 4, setting 1, output audible and finite | `oneStepWhenTheRateIsFourTimesTheBase` | FAIL: count 0, expected 1 | PASS |
| Unchanged counts where the ratio fits (1x/2x/4x) | `unchangedWhereTheRateFits` | PASS | PASS |
| Output rate below base (base/2) | `aRateBelowTheBaseIsTreatedAsTheBase` | SIGFPE (division by zero) | PASS: count 2 |

Audio-thread constraint: the helper is called from the constructor, which already allocates (`make_unique`) on that path. The fix adds no allocation, lock, log or wait.

Undo, saved projects and commands: not touched. `VibratingString` holds no saved state; the setting in the project file is unchanged, and projects load exactly as before.

## Commands and EXIT codes

Run from the worktree unless stated. `$LOGS` is `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-503.r1/logs`. A `CCACHE_TEMPDIR=$PWD/build/ccache-tmp` prefix was needed on every build (see Notes).

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Build, first try (failed on ccache temp dir, not on code) | `cmake --build build --target zene VibratingStringTest -j4` | 2 | `build-1-red.log` |
| RED build (unfixed, accessor present) | `CCACHE_TEMPDIR=$PWD/build/ccache-tmp cmake --build build --target zene VibratingStringTest -j4` | 0 | `build-2-red.log` |
| **RED test** | `cd build/tests && ctest -R '^VibratingStringTest$' --output-on-failure` | **8** (1 of 1 failed) | `test-1-red.log` |
| Save the fix (product .cpp) | `git diff -- plugins/Vibed/VibratingString.cpp > $LOGS/fix.patch` | 0 | — |
| Revert the fix (proof, 4b) | `git apply -R $LOGS/fix.patch` | 0 | — |
| Unfixed rebuild | `CCACHE_TEMPDIR=… cmake --build build --target zene VibratingStringTest -j4` | 0 | `build-4-unfixed.log` |
| Unfixed run, direct | `cd build/tests && ./VibratingStringTest` | **136** (SIGFPE) | `run-unfixed-direct.log` |
| Unfixed run, ctest | `ctest -R '^VibratingStringTest$' --output-on-failure` | 8 | `test-3-unfixed-ctest.log` |
| Re-apply the fix | `git apply $LOGS/fix.patch` | 0 | — |
| GREEN build | `CCACHE_TEMPDIR=… cmake --build build --target zene VibratingStringTest -j4` | 0 | `build-5-regreen.log` |
| **GREEN test, ctest** | `cd build/tests && ctest -R '^VibratingStringTest$' --output-on-failure` | **0** (100%) | `test-4-regreen-ctest.log` |
| **GREEN test, direct** | `cd build/tests && ./VibratingStringTest` | **0**: `Totals: 5 passed, 0 failed` | `run-regreen-direct.log` |

Proof output, unfixed (`run-unfixed-direct.log`): `FAIL! : VibratingStringTest::oneStepWhenTheRateIsFourTimesTheBase()`, `Actual (string->oversampleCount()): 0`, `Expected (1): 1`, `Loc: [../../tests/src/plugins/VibratingStringTest.cpp(74)]`, then `Received signal 8 (SIGFPE)`.

Affected family (`Vibrat|Vibed`), with the Vibed handoff test rebuilt:

| Step | Command | EXIT | Result |
| --- | --- | --- | --- |
| Build the Vibed handoff test | `CCACHE_TEMPDIR=… cmake --build build --target VibedWaveformHandoffTest -j4` | 0 | `build-6-family.log` |
| Count tests in family | `ctest -N -R 'Vibrat\|Vibed'` | 0 | `Total Tests: 2` |
| Run family | `ctest -R 'Vibrat\|Vibed' --output-on-failure` | 0 | 2/2 passed: VibratingStringTest, VibedWaveformHandoffTest (`test-5-family.log`) |
| Build the plugin that includes the header | `CCACHE_TEMPDIR=… cmake --build build --target vibedstrings -j4` | 0 | `build-7-plugin.log` |
| Finding's suggested ctest name | `ctest -N -R '^VibratingString$'` | 0 | `Total Tests: 0` (`ctest-N-finding-name.log`) |

Gates:

| Gate run | Command | EXIT | Result |
| --- | --- | --- | --- |
| Wrapper, as specified | `python3 …/qwen/feature_check.py <worktree>` | 1 | `PermissionError` creating `<worktree>-feature-check/` outside the write-jail (`feature-check-1.log`); no gate ran |
| Same gate code, logs in LOG FOLDER | `python3 …/new-p9-503.r1/gate_driver.py <worktree> $LOGS/gates` | **0** | `RESULT: PASS` (`gate-table-1.txt`) |

Gate table (`gate-table-1.txt`):

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              exit 1; no new item in your files
  PASS  file-length             exit 1; no new item in your files
RESULT: PASS
```

Complexity and file-length exit 1 on the train by design; the gate's own "new item in your files" filter lists nothing for this branch (the gate logs contain no mention of `VibratingString`, `stepsPerSample` or `VibratingStringTest`).

## Notes and deviations

1. **ccache temp dir.** The first build failed with `ccache: error: failed to create temporary file … Permission denied` in `~/.local/share/zene-bunny-fix/dsh/home/…` (`build-1-red.log`), before any code was compiled. The rebuilds set `CCACHE_TEMPDIR` to a directory inside the build tree. The compiler and flags are unchanged.
2. **Gate wrapper.** `feature_check.py` writes its per-gate logs to `<worktree>/../<name>-feature-check/`, outside this lane's write-jail, and failed at its first `mkdir`. I ran the same checks with `gate_driver.py` (in the run folder): it imports the same functions and runs the same commands and the same NEW-item filter, with logs in the LOG FOLDER and the baseline read from the existing cache for `639e1d23e`. The gate definitions and baselines were not changed.
3. **Fix patch scope.** The 4b proof reverts `plugins/Vibed/VibratingString.cpp` only. The header change is the read-only `oversampleCount()` accessor the test needs; it stays in place for both RED and GREEN, so the RED failure is an assertion, not a missing symbol.
4. **Sub-base rate.** The division-by-zero path (output rate below the base rate) is shown by the code and by the SIGFPE in the unfixed run. I have not run the application with a sub-base output device; whether a real device reports such a rate is not verified here.
5. **Clamp choice.** Clamping the count to 1 means the 4x case at setting 1 gets one string step per sample rather than an intended fraction. The finding asked for this clamp; I did not change the model's behaviour outside the zero case. The regression asserts only the count and that the output is audible and finite.

## Files committed

- `plugins/Vibed/VibratingString.cpp` (helper `stepsPerSample`, initializer change)
- `plugins/Vibed/VibratingString.h` (`oversampleCount()` accessor)
- `tests/src/plugins/VibratingStringTest.cpp` (new regression test)
- `tests/CMakeLists.txt` (registers the test and compiles the string sources into it)
- `tests/fork-sources.txt` (registers the new test file)
- `tests/upstream-modifications.txt` (reasons for the three inherited files changed)
- `docs/qwen-fixes-2026-10/cluster-new-p9-503.md` (this report)
