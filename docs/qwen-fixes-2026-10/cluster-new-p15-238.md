ALREADY-FIXED

Cluster new-p15-238: Vibed can divide by zero when its sample-rate ratio is below one.

## Verdict and evidence

**Already fixed on the integration base.** The branch this worktree is on, `integration/new-p15-238-c6ee4fc7bc` (HEAD `c6ee4fc7bc`), contains cluster new-p9-503's fix (`42159aeade`, an ancestor of HEAD, checked with `git merge-base --is-ancestor`).

- `plugins/Vibed/VibratingString.cpp:43-47` (HEAD) computes
  `ratio = std::max(sampleRate / baseSampleRate(), sample_rate_t{1})` and returns
  `std::max(2 * setting / static_cast<int>(ratio), 1)`. An output rate below the base rate makes the integer quotient
  zero, and the clamp holds the divisor at one, so nothing divides by zero. Rates at or above the base are unchanged.
- The regression that already guards it: `tests/src/plugins/VibratingStringTest.cpp` (HEAD),
  `aRateBelowTheBaseIsTreatedAsTheBase`, renders a string at `base / 2` and expects `oversampleCount() == 2`.
  Without the clamp that test dies with SIGFPE (see RED below).
- The only construction site is `plugins/Vibed/Vibed.cpp:81` (`StringContainer::addString`), reached from
  `Vibed::playNoteImpl`.

**The lane's own change is superseded.** Branch `bugs/qwen-new-p15-238` at `0ceb548cea` carries a second fix for the same
line (`2 * oversample / std::max(1, static_cast<int>(sampleRate / base))`). It is equivalent: both use divisor one below
the base rate and the same value at or above it. Integrating both makes `VibratingString.cpp` conflict, which is what
the integration apply left behind. This commit keeps HEAD's `VibratingString.cpp`, so the product code in this branch
is unchanged by this lane. I did not touch `bugs/qwen-new-p15-238`.

## Branch note

The brief named branch `bugs/qwen-new-p15-238`. The worktree at this path was checked out on
`integration/new-p15-238-c6ee4fc7bc`, with an unresolved apply of the lane's patch in its index. I resolved that
apply (see "Commands") and committed on the checked-out branch. I did not switch, merge, stash or reset.

## Regression test

`tests/src/plugins/VibedSubBaseRateTest.cpp` (from the lane's `0ceb548cea`, kept) is an end-to-end guard on the
product path: a Vibed note through `Instrument::playNote`, with the device pinned at `base / 2`, `base`, `base * 2`
and `base / 2` again (`setSampleRateForTesting`, `QCOMPARE` after each pin). Each render must be finite and non-silent.
It is deterministic: no wall-clock, sleep or race; `stopProcessing()` stops background pulls, as in
`VibedWaveformHandoffTest`.

Test matrix:

| Path | Covered by |
| --- | --- |
| Normal (rate == base, rate == 2 x base) | `noteRendersAtEveryDeviceRate`, two of its four steps; `VibratingStringTest::unchangedWhereTheRateFits` |
| Failure: rate below base (the bug) | `noteRendersAtEveryDeviceRate` first and last steps; `VibratingStringTest::aRateBelowTheBaseIsTreatedAsTheBase` |
| Reconfiguration: rate changes between notes | the sequence base/2, base, 2x base, base/2 |
| Audio-thread constraints | no product code changed in this commit |
| Platform | Linux build only; no platform branch involved |

## RED / GREEN (step 4b)

The clamp is already in HEAD, so the "fix" to take out for RED is the clamp on line 45. Saved as
`logs/unfix.patch` in the run folder (`sed` on `VibratingString.cpp:45`, then `git diff`). Both tests were run with the
clamp removed and then with it restored.

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Remove clamp | `sed -i 's/std::max(sampleRate \/ ...baseSampleRate(), sample_rate_t{1})/sampleRate \/ ...baseSampleRate()/' plugins/Vibed/VibratingString.cpp` and `git diff > unfix.patch` | 0 | `unfix.patch` |
| Build unfixed | `CCACHE_TEMPDIR=... cmake --build build --target VibedSubBaseRateTest VibratingStringTest -j4` | 0 | `build-5-red.log` |
| **RED** | `cd build/tests && ctest -R 'VibedSubBaseRateTest\|VibratingStringTest' --output-on-failure` | **8**, 0 of 2 passed (`SIGFPE, FPE_INTDIV` in both) | `test-2-red.log` |
| Restore fix | `git apply -R logs/unfix.patch`; `git diff --stat -- plugins/Vibed/VibratingString.cpp` (empty = equal to HEAD) | 0 / empty | |
| Rebuild fixed | `CCACHE_TEMPDIR=... cmake --build build --target VibedSubBaseRateTest VibratingStringTest -j4` | 0 | `build-6-green-restored.log` |
| **GREEN** | `cd build/tests && ctest -R 'Vibed\|VibratingString' --output-on-failure` | **0**, 3 of 3 passed | `test-3-green-restored.log` |

## Commands and exit codes (all in the run folder's `logs/`)

| Step | Command | EXIT | Log |
| --- | --- | --- | --- |
| Resolve apply conflicts | `python3 -I logs/resolve.py` on `tests/CMakeLists.txt` and `tests/upstream-modifications.txt` (keeps both sides; drops the `VibratingString.cpp` modification line); `git show HEAD:plugins/Vibed/VibratingString.cpp > ...`; `git add` on those three paths | 0 | |
| Build (first try) | `cmake --build build --target VibedSubBaseRateTest VibratingStringTest VibedWaveformHandoffTest -j4` | 2 (`No rule to make target`; tree not regenerated) | `build-1-green.log` |
| Regenerate | `cmake --build build --target zene -j4` | 2 (CMake: `tests/CMakeLists.txt:2642` endforeach not nested; the shared `endif()` was missing after the `ours` block) | `build-2-regen-zene.log` |
| Fix the nesting | inserted `endif()` before the new block (script) and re-ran the regenerate | 0 | `build-3-regen-zene.log` |
| Build the three Vibed tests | `CCACHE_TEMPDIR=... cmake --build build --target VibedSubBaseRateTest VibratingStringTest VibedWaveformHandoffTest -j4` | 0 | `build-4-green.log` |
| GREEN (first) | `cd build/tests && ctest -R 'Vibed\|VibratingString' --output-on-failure` | 0, 3 of 3 passed | `test-1-green.log` |
| RED / GREEN | see the table above | 8 / 0 | `test-2-red.log`, `test-3-green-restored.log` |
| Gates | `python3 feature_check.py <worktree>` | 1 (`PermissionError`: it creates `<worktree>-feature-check/` outside the write-jail; the script was not changed) | `gates-1.log` |
| Gates (same checks, logs in run folder) | `python3 -I logs/gate-driver.py <worktree> logs/gates` (imports `feature_check`, same `SIMPLE` and `RATCHET` lists) | 0, 8 of 8 PASS | `gates-2.log`, `logs/gates/` |

Affected family (`ctest -R 'Vibed|VibratingString'`): 3 tests executed, 3 passed.

`VibratingString` has no ctest name of its own. The `tests/reference/Vibed/` copies are not built and were not changed.

## Gates

| Gate | Result |
| --- | --- |
| fork-sources | PASS (exit 0) |
| all-sources-reproduce | PASS (exit 0) |
| no-upstream-regression | PASS (exit 0) |
| check-namespace | PASS (exit 0) |
| no-tautology | PASS (exit 0) |
| doc-figures | PASS (exit 0) |
| complexity | PASS (no new item in changed files) |
| file-length | PASS (no new item in changed files) |

## Files changed in this commit

- `tests/src/plugins/VibedSubBaseRateTest.cpp`: the regression test (fork-NEW).
- `tests/CMakeLists.txt`: registers the test and compiles the Vibed sources into it (keeps the `VibratingStringTest` registration from HEAD).
- `tests/all-sources.txt`, `tests/fork-sources.txt`: list the new test.
- `tests/upstream-modifications.txt`: records the `tests/CMakeLists.txt` change; the `VibratingString.cpp` line from the lane is dropped because that file now equals HEAD.
- `docs/qwen-fixes-2026-10/cluster-new-p15-238.md`: this report.

`plugins/Vibed/VibratingString.cpp` is not in this commit (it equals HEAD).

## Self-check

- **Regression test fails without the clamp and passes with it:** yes. RED `test-2-red.log` EXIT 8 (both tests SIGFPE);
  GREEN `test-3-green-restored.log` EXIT 0 (3 of 3).
- **Deterministic:** the device rate is pinned with the existing `AudioDevice::setSampleRateForTesting` seam, checked
  with `QCOMPARE`. The assertions are finiteness and non-silence.
- **Fix at the cause and complete:** the cause is the integer quotient reaching zero, guarded at HEAD by the clamp at
  `VibratingString.cpp:45-46`. No other division by `baseSampleRate()` in this path; `AudioFileProcessor.cpp:412` is a different computation.
- **No collateral damage:** no product code changed in this commit. Saved projects are unaffected.
- **Realtime / undo / commands:** no product code changed; no new control command.
- **Gates:** see "Gates" above. The table comes from the gate driver (EXIT 0); `feature_check.py` itself is blocked by
  the write-jail (EXIT 1, `gates-1.log`).
- **Open question:** whether a sub-base output should run fewer string steps per second (the current clamp) or scale
  the oversampling by base/output. That trade-off was decided by `42159aeade`, not by this lane; it is not changed here.
