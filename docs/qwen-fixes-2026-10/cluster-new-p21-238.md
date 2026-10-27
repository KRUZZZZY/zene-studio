ALREADY-FIXED

# cluster new-p21-238: Microtuner::keyToFreq has no guard for an empty interval list

Finding (Luna patrol, BUG 4): a scale with no intervals reaches `Microtuner::keyToFreq`, where `octaveDegree` becomes -1 and is used in division, modulo and as an index into `intervals`. Suggested fix: return 0 when the interval list is empty.

## Verdict and decision

ALREADY-FIXED for every production path. On this branch no path builds an empty `Scale` that can reach `keyToFreq`. The loader that the finding's fix would protect is guarded by commit `7c5051c63` (`qa-fix(new-p14-279): keep invalid scale loads nonempty`), which is in this branch's history. The guard the finding proposes (`keyToFreq` returns 0 for an empty list) is not in the code, and I did not add it: no reachable state reaches it, so adding it would change the contract without fixing a bug.

Residual, not reachable today: the public constructor `Scale(QString, std::vector<Interval>)` and `Scale::setIntervals` accept an empty vector without a check. No caller passes an empty vector (see below). This is noted, not fixed.

## Evidence (file:line, branch `bugs/qwen-new-p21-238` at `639e1d23e`)

- `src/core/Microtuner.cpp:65-112` `keyToFreq`. Line 74 takes `intervals` from `scale->getIntervals()`. Line 83 `const int octaveDegree = intervals.size() - 1;` is -1 only when the list is empty (size_t 0 minus 1 converted to int). Line 107 `intervals[octaveDegree]` is then out of bounds. The finding's line ~70 has moved to these lines.
- Every production construction of a `Scale` yields at least one interval:
  - `src/core/Scale.cpp:127-130` default constructor seeds `Interval(1, 1)`.
  - `src/core/Scale.cpp:164-189` `Scale::loadSettings` clears the list, keeps only valid intervals, then falls back to `Interval(1, 1)` at line 188 when none are valid (`if (m_intervals.empty()) { m_intervals.emplace_back(1, 1); }`). This is the `7c5051c63` fix. Its regression is `aScaleWithOnlyAnUnusableCentsIntervalFallsBackToUnison` in `tests/src/core/ScaleIntervalOverflowTest.cpp:190-202`.
  - `src/gui/MicrotunerConfig.cpp:400-401` `applyScale()` seeds `newIntervals` with `Interval(1, 1)` before parsing lines, then builds the scale at line 428 (`std::make_shared<Scale>(...)`).
  - `src/core/Song.cpp:219` fills every song scale slot with the default `Scale()`.
- Callers of the two unguarded entry points:
  - `Scale::setIntervals` (`include/Scale.h:82`): no callers in `src/`, `plugins/` or `tests/` (`rg -n setIntervals`).
  - `Scale(QString, std::vector<Interval>)` (`include/Scale.h:76`, `src/core/Scale.cpp:133-137`): one caller, `src/gui/MicrotunerConfig.cpp:428`, always non-empty.
- Scale ingress into the song: `Song::setScale` (`src/core/Song.cpp:3178`) and `Song::getScale` (`src/core/Song.cpp:3148`) store and return whatever is given. Their only callers pass the non-empty scales above.
- The caller path `NotePlayHandle.cpp:688-695` only calls `keyToFreq` when the Microtuner is enabled and the key is mapped.

## Regression test

The existing test `aScaleWithOnlyAnUnusableCentsIntervalFallsBackToUnison` (`tests/src/core/ScaleIntervalOverflowTest.cpp:190-202`) is the regression. It loads a scale whose only interval is refused and requires the unison fallback. I did not add a second test. A `keyToFreq` test on an empty scale would need an invalid state that no production path produces.

Proof that the test catches the cause. The change is the three lines of commit `7c5051c63` in `src/core/Scale.cpp` (saved to `logs/fix.patch`). They were reverted, the test was rebuilt and run (RED), then re-applied and run again (GREEN). The tree is back at `639e1d23e` with no diff.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p21-238.r1/logs`.

Build note: the first build attempt failed with `ccache: error: failed to create temporary file for .../dsh/home/ccache-tmp-r6/...: Permission denied` (`build-1.log`). That path is outside the write-jail. Every later build used `CCACHE_TEMPDIR=/home/kruzzzzy/.cache/ccache-bunny/tmp-p21-238` (a build-cache directory).

| Step | Command (run from the worktree, or `build/tests` for ctest) | EXIT | Log |
|---|---|---|---|
| First build (failed, ccache permission) | `cmake --build build --target zene ScaleIntervalOverflowTest -j4` | not captured (make errors in log) | build-1.log |
| Test target build | `CCACHE_TEMPDIR=... cmake --build build --target ScaleIntervalOverflowTest -j4` | log shows "Built target ScaleIntervalOverflowTest" | build-2.log |
| GREEN on unmodified branch | `ctest -R '^ScaleIntervalOverflowTest$' --output-on-failure` (in `build/tests`) | 0 | test-1.log, test-4-green.log |
| RED: revert fix | `git apply -R logs/fix.patch` | 0 | apply-R.log |
| RED build | `CCACHE_TEMPDIR=... cmake --build build --target ScaleIntervalOverflowTest -j4` | 0 | build-5-red.log |
| RED test | `ctest -R '^ScaleIntervalOverflowTest$' --output-on-failure` (in `build/tests`) | 8 | test-5-red.log |
| GREEN: re-apply fix | `git apply logs/fix.patch` | 0 | apply-1.log |
| GREEN build | `CCACHE_TEMPDIR=... cmake --build build --target ScaleIntervalOverflowTest -j4` | 0 | build-6-green.log |
| GREEN test | `ctest -R '^ScaleIntervalOverflowTest$' --output-on-failure` (in `build/tests`) | 0 | test-6-green.log |

The RED failure, from `test-5-red.log` / `test-2-red.log`:

```
FAIL!  : ScaleIntervalOverflowTest::aScaleWithOnlyAnUnusableCentsIntervalFallsBackToUnison() Compared values are not the same
   Actual   (static_cast<int>(scale.getIntervals().size())): 0
   Expected (1)                                            : 1
   Loc: [../../tests/src/core/ScaleIntervalOverflowTest.cpp(200)]
```

Tests executed in the affected family: `ScaleIntervalOverflowTest`, 1 test (ctest summary `100% tests passed out of 1` on GREEN; `0% tests passed, 1 tests failed out of 1` on RED). `SessionTuningTest`, `MicrotunerScaleFileTest` and `Sf2PlayerRealtimeTest` were not built or run in this session. No product code changed on the branch, so their results are unaffected by this report, but I have not measured them here.

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` was run first and could not run: it writes its logs to `<worktree>/../zene-qwen-new-p21-238-feature-check`, outside the write-jail. Its traceback is in `gates-1.log` (`PermissionError`, EXIT 1). Instead, each gate command the script runs was run from the worktree with its output in the log folder (`gates-run.sh`, `gates-summary.log`, `gate-<name>.log`):

| Gate | Command | EXIT |
|---|---|---|
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 |

The two ratchet gates are red on the train on purpose. By the script's rule they pass when no item they list is new in a file this branch changed. The changed-file set is empty: `git diff --name-only 639e1d23e..HEAD` prints nothing, and this branch adds no product file. So no item can be new in my files, and both ratchet rows pass by that rule. The raw exit code is 1 for both.

## Self-check

Run: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/submit_check.py <worktree> new-p21-238`, saved as `submit-check-1.log` in the log folder. Its result before this commit: 3 FAIL (`commits on the branch`, `working tree clean`, `gates (feature_check.py)`) and 5 "?" items, answered below.

- **Regression test fails with the change reverted and passes with it:** yes. RED: `ctest -R '^ScaleIntervalOverflowTest$'` with the three-line loader fallback reverted (`git apply -R logs/fix.patch`) gives EXIT 8, `FAIL! ... Actual 0, Expected 1, ScaleIntervalOverflowTest.cpp(200)`. GREEN: with the fallback restored (`git apply logs/fix.patch`) EXIT 0, `100% tests passed out of 1`. Exit codes are in the table above and in `test-5-red.log` / `test-6-green.log`.
- **Deterministic:** yes. The test builds its scale from fixed XML text (`centsInterval(document, "200000")`, `tests/src/core/ScaleIntervalOverflowTest.cpp:190-202`). It has no timing, sleeps, threads or randomness, and no injected seam is needed.
- **Fix at the cause and complete:** there is no new product fix in this commit. The cause the finding names (an empty list from a loaded scale) is closed at its only reachable entry, `Scale::loadSettings` (`src/core/Scale.cpp:186-188`). The `keyToFreq` guard the finding proposes is not added, because no reachable state reaches it. The unguarded entry points (`Scale(QString, std::vector<Interval>)` and `Scale::setIntervals`, `include/Scale.h:76,82`) have no caller that passes an empty vector, and this is recorded as a residual.
- **No collateral damage:** this commit changes no product code. The loader fallback (already on the branch) applies only when every stored interval is refused, and before it the scale was empty and `keyToFreq` indexed out of bounds. Valid projects and scales do not reach the fallback.
- **ALREADY-FIXED evidence:** `src/core/Microtuner.cpp:83,107` (the empty-list index), `src/core/Scale.cpp:186-188` (the fallback, commit `7c5051c63`), `src/gui/MicrotunerConfig.cpp:400-401,428` (the GUI seeds `1/1`), and `src/core/Scale.cpp:127-130` (the default constructor seeds `1/1`). See "Evidence" above.

**Gates (feature_check.py) — not runnable here.** The script writes its logs to `<worktree>/../zene-qwen-new-p21-238-feature-check`, outside the write-jail, and failed with `PermissionError` (`gates-1.log`). I ran each gate command the script runs, from the worktree (table above). Six pass. The two ratchets (complexity, file-length) exit 1, red on the train by design; the changed-file set is empty, so no item is new in my files. The FAIL in `submit_check` is therefore a sandbox limit, not a result. The gate run should be repeated by the loop where the output folder is writable.
