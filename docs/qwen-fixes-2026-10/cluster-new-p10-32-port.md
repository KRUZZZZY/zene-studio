# Cluster new-p10-32-port: large `plugin.randomise` seeds (port)

**Verdict: FIXED** (port of `bugs/qwen-new-p10-32` commit `43d518ae5`, re-applied by hand on `ded355440`; not merged, cherry-picked or rebased)

The finding was real on this line. `src/core/ControlCommandsDeviceParams.cpp:316` read the seed with `toInt()`, which cannot hold `2147483648`; that value reached the generator as zero and produced the same parameter as `seed: 0`.

## Fix

- `src/core/ControlCommandsDeviceParams.cpp:305-318` — new `seedOf()` helper beside `amountOf()` (`:301`): the seed is read as a double and refused with `InvalidArgs` unless it is within `0..UINT32_MAX` (the same refusal rule as `readSeed` in `ControlCommandsNoteRandom.cpp:164-180`). The double comparison happens before any integer conversion, so a huge value never reaches an out-of-range cast.
- `src/core/ControlCommandsDeviceParams.cpp:332-339` — `randomise` seeds the generator from `seedOf()` when `seed` is present, otherwise from `std::random_device` as before.
- `src/core/ControlCommandsDeviceParams.cpp:37` and `:40` — `<cstdint>` and `<limits>` added.
- No schema change (`integerProperty()` unchanged), so `ControlCommandsSnapshot` does not move and no command was added.

Fractional seeds never reach the handler: the schema declares `"type":"integer"` (`ControlCommandsAutomationRamp.cpp:341` shows the same declaration form), so only whole numbers arrive.

## Regression test

`tests/src/core/DeviceToolsTest.cpp:549-572`, `randomiseKeepsLargeSeeds()`, next to the MX-093 randomise tests:

1. `seed: 2147483648` must succeed and change the Volume, then undo.
2. `seed: 0` must produce a different Volume (the bug made these equal), then undo.
3. `seed: 4294967296` must be refused with `InvalidArgs`.

No new file was created, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` are unchanged (both already list the two edited files).

## Evidence

The RED/GREEN runs used the `DeviceToolsTest` family (one CTest, 15 QtTest slots). The amplifier module was built, so the regression slot ran rather than skipped (`0 skipped` in every run below).

**Log location:** the run folder `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-32.r1/logs` is not writable from this session (every write, from Bash and from the Write tool, returns EACCES although the directory is owned by the user with `rwx`). All logs were written to `/tmp/zene-qwen-r2/logs/` instead, and the patch to `/tmp/zene-qwen-r2/logs/fix.patch`. The `r2-` numbering below matches those files.

**ccache workaround:** the first amplifier build failed with `ccache: error: failed to create temporary file for /run/user/1000/ccache-tmp/...: Permission denied`. The rebuilds set `CCACHE_TEMPDIR=/tmp/zene-qwen-r2/ccache-tmp`; nothing else in the build was changed.

| # | Step | Exact command (cwd) | EXIT | Result |
|---|---|---|---|---|
| 1 | Build, first attempt | `cmake --build build --target zene amplifier DeviceToolsTest > /tmp/zene-qwen-r2/logs/build-1.log 2>&1; echo EXIT=$?` (worktree) | 2 | amplifier failed on the ccache temp-file denial above; no product or test change involved |
| 2 | Build with the test added, before any fix | `CCACHE_TEMPDIR=/tmp/zene-qwen-r2/ccache-tmp cmake --build build --target zene amplifier DeviceToolsTest > .../build-2.log 2>&1; echo EXIT=$?` (worktree) | 0 | builds |
| 3 | **RED**, original product code | `ctest -R DeviceToolsTest --output-on-failure -V > .../test-1.log 2>&1; echo EXIT=$?` (`build/tests`) | **8** | `FAIL! : DeviceToolsTest::randomiseKeepsLargeSeeds() 'std::abs(largeValue - zeroValue) > 1e-6' returned FALSE.` Totals: 14 passed, 1 failed, 0 skipped |
| 4 | Apply fix, build | `CCACHE_TEMPDIR=... cmake --build build --target zene amplifier DeviceToolsTest > .../build-3.log 2>&1; echo EXIT=$?` (worktree) | 0 | builds, no errors or warnings in log |
| 5 | **GREEN**, fixed code | `ctest -R DeviceToolsTest --output-on-failure -V > .../test-2.log 2>&1; echo EXIT=$?` (`build/tests`) | **0** | `PASS : DeviceToolsTest::randomiseKeepsLargeSeeds()`; Totals: **15 passed, 0 failed, 0 skipped** |
| 6 | Proof: save the product diff | `git diff -- src/core/ControlCommandsDeviceParams.cpp > /tmp/zene-qwen-r2/logs/fix.patch; echo EXIT=$?` (worktree) | 0 | patch saved |
| 7 | Proof: reverse the product fix | `git apply -R /tmp/zene-qwen-r2/logs/fix.patch; echo EXIT=$?` (worktree) | 0 | product file back to its `HEAD` (`ded355440`) code |
| 8 | Proof: rebuild reverted | `CCACHE_TEMPDIR=... cmake --build build --target zene amplifier DeviceToolsTest > .../build-4.log 2>&1; echo EXIT=$?` (worktree) | 0 | builds |
| 9 | Proof: **RED** with fix reversed | `ctest -R DeviceToolsTest --output-on-failure -V > .../test-3.log 2>&1; echo EXIT=$?` (`build/tests`) | **8** | same assertion: `'std::abs(largeValue - zeroValue) > 1e-6' returned FALSE`; 14 passed, 1 failed |
| 10 | Proof: reapply fix | `git apply /tmp/zene-qwen-r2/logs/fix.patch; echo EXIT=$?` (worktree) | 0 | `git diff` of the product file byte-identical to the saved patch (`cmp` silent) |
| 11 | Proof: rebuild restored | `CCACHE_TEMPDIR=... cmake --build build --target zene amplifier DeviceToolsTest > .../build-5.log 2>&1; echo EXIT=$?` (worktree) | 0 | builds |
| 12 | Proof: **GREEN** after restore | `ctest -R DeviceToolsTest --output-on-failure -V > .../test-4.log 2>&1; echo EXIT=$?` (`build/tests`) | **0** | 15 passed, 0 failed, 0 skipped |

**Affected family:** `DeviceToolsTest` — 15 tests executed in the GREEN run (step 5 and step 12). `plugin.randomise` is referenced only in `tests/src/core/DeviceToolsTest.cpp` (`rg -l "plugin\.randomise" tests/src tests/*.py tests/*.sh`), so no other family exercises it.

**Caveat on the RED evidence:** the RED run stopped at the first assertion of the new test. The third assertion (the `4294967296` refusal, `InvalidArgs`) was therefore not observed to fail on the original code; it is shown to pass only with the fix (step 5). The `2147483648`-vs-`0` assertion is the one that demonstrated the bug.

### Ratchet gates

Run on the final tree (both product files and the test file, after the last edit):

| Gate | Command (cwd) | EXIT | Result |
|---|---|---|---|
| Complexity | `bash tests/complexity-gate.sh --check > /tmp/zene-qwen-r2/logs/gate-1.log 2>&1; echo EXIT=$?` (worktree) | **1** | 6 `REGRESSION` lines, none in the files I changed (e.g. `XyPadPairTest.cpp dotCentreAlong` CCN 16, `ControlCommandsMixerTools.cpp adjust` CCN 12) |
| File length | `bash tests/file-length-gate.sh --check > /tmp/zene-qwen-r2/logs/gate-2.log 2>&1; echo EXIT=$?` (worktree) | **1** | 14 `REGRESSION: new file over 500 lines` lines (e.g. `tests/src/core/VideoTrackTest.cpp (589)`), none in the files I changed |

**Both gates fail, and they fail identically without my change.** To confirm, I reversed the full diff (`git apply -R` of the saved full patch, EXIT 0), ran both gates on the base tree, then reapplied the diff (EXIT 0):

| Gate on base tree (diff reversed) | Command | EXIT | `REGRESSION` lines |
|---|---|---|---|
| Complexity | `bash tests/complexity-gate.sh --check > .../gate-base-1.log 2>&1; echo EXIT=$?` | **1** | 6 — sorted set identical to the fixed tree (`diff` silent) |
| File length | `bash tests/file-length-gate.sh --check > .../gate-base-2.log 2>&1; echo EXIT=$?` | **1** | 14 — sorted set identical to the fixed tree (`diff` silent) |

The failures are baseline drift on this line (files this bug does not touch), and I did not change any gate limit or baseline, as the rules require. Clearing them is outside this single-bug port; the gates need a separate owner decision.

## Files committed

- `src/core/ControlCommandsDeviceParams.cpp`
- `tests/src/core/DeviceToolsTest.cpp`
- `docs/qwen-fixes-2026-10/cluster-new-p10-32-port.md`

## Luna round-0 points

The brief's round-0 section asked for the port only; no separate numbered review points were listed. The port itself is this report.
