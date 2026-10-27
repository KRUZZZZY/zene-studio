ALREADY-FIXED

# Cluster new-p10-124: large finite waveform phases overflow integer conversions

## Verdict

ALREADY-FIXED on `bugs/qwen-new-p10-124`. The fix is commit `0427f1dc6` ("Fix Xpressive large phase conversion (cluster new-p9-142)"). `git merge-base --is-ancestor 0427f1dc6 HEAD` exits 0, so the fix is in this branch's history. No product change is needed here, so this commit adds only this report.

## Code evidence

The finding's line 130 is historical. In the current tree, `plugins/Xpressive/ExprSynth.h:130` is the float member `m_note_rel_sec`. The helper now lives at `plugins/Xpressive/ExprSynth.h:149-162`:

- `ExprSynth.h:153` takes the fractional part with `std::fmod(x, 1.0f)`. No float-to-`int` conversion remains in the helper.
- `ExprSynth.h:154-155` returns 0 for an exact zero fraction.
- `ExprSynth.h:156-160` wraps a negative fraction into `[0, 1)`, and returns 0 if `fractionalPart + 1.0f` rounds up to `1.0f`.
- `ExprSynth.h:151-152` returns 0 for NaN and infinite inputs, as before.

The pre-fix helper, from `git show 0427f1dc6^:plugins/Xpressive/ExprSynth.h` lines 128-133, is the code the finding describes:

```
if (x<0) { x+=static_cast<int>(1-x); }
return x-static_cast<int>(x);
```

For a finite value outside `int` range, `static_cast<int>` is undefined behaviour. `3e9` and `-3e9` (and `1 - x` below `INT_MIN`) are exactly that case. The fixed helper never converts the input to `int`, so the bug is gone.

Remaining `int` conversions on this path are bounded by the table size, so they are not this bug:

- `plugins/Xpressive/ExprSynth.cpp:206` computes `(int)(positiveFraction(index) * m_size)`. The helper returns a value in `[0, 1)`, so the result is below `m_size`.
- `plugins/Xpressive/ExprSynth.cpp:224` computes `(int)x` with `x = positiveFraction(index) * m_size`, which is again below `m_size`.

Callers of the helper: `plugins/Xpressive/ExprSynth.cpp:423, 432, 442, 460, 469, 482, 496, 509` (the `sinew` path and the other waveform functions) call it with an unbounded phase. All of them receive the fixed helper's output.

The tests of this helper are `tests/src/plugins/PluginPortsMigrationTest.cpp:1663-1677`:

- `positiveFractionHandlesVeryLargePositiveValues` (`FLT_MAX`, which is far above `INT_MAX`)
- `positiveFractionHandlesVeryLargeNegativeValues` (`-FLT_MAX`, which is far below `INT_MIN`)
- `positiveFractionPreservesOrdinaryValues` at `:1679-1684` (`1.25 -> 0.25`, `-0.25 -> 0.75`, `-2.0 -> 0.0`), which checks behaviour is unchanged.

`FLT_MAX` covers the finding's `3e9` class: any finite value above `INT_MAX` is in the same conversion range. These tests do not assert the NaN/infinite inputs; those return 0 before and after the change.

`tests/reference/Xpressive/ExprSynth.h:124-132` still holds the old helper. That file is a frozen copy tracked in `tests/reference/ORIGIN.tsv`. No CMake line references `tests/reference/Xpressive` (checked with a content search of `tests/CMakeLists.txt`), so it is not compiled into any test or the product. I did not change it; it is out of scope for this finding.

## Commands and exit codes

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-124.r1/logs/`.

| # | Command (run from the worktree unless noted) | Log | EXIT | Result |
|---|---|---|---:|---|
| 1 | `cmake --build build --target zene PluginPortsMigrationTest -j4 > .../01-build-green.log 2>&1` | 01 | 2 | ccache could not write its temp file (`Permission denied`) under `~/.local/share/zene-bunny-fix/dsh/home`, outside the write area. Environment, not code. |
| 2 | `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p10-124 cmake --build build --target zene PluginPortsMigrationTest -j4 > .../02-build-green.log 2>&1` | 02 | 0 | Build passes with ccache temp in `$TMPDIR`. |
| 3 | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../03-test-green.log 2>&1` | 03 | 8 | 81 passed, 1 failed: `nonRealtimeLadspaEffectsAreInvalid` ("SWH Decimator test fixture was not loaded"). Unrelated to `positiveFraction`. `build/plugins/ladspa/decimator_1202.so` did not exist. |
| 4 | `CCACHE_TEMPDIR=... cmake --build build --target decimator_1202 -j4 > .../04-build-decimator.log 2>&1` | 04 | 0 | Builds the missing fixture; `build/plugins/ladspa/decimator_1202.so` now exists. |
| 5 | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../05-test-green.log 2>&1` | 05 | 0 | GREEN on the current (fixed) tree: `100% tests passed out of 1`. |
| 6 | `git show 0427f1dc6 -- plugins/Xpressive/ExprSynth.h > .../fix.patch`; `git apply -R .../fix.patch` | — | 0 | Header reverted in the worktree (`git status` showed ` M plugins/Xpressive/ExprSynth.h`). |
| 7 | `CCACHE_TEMPDIR=... cmake --build build --target zene PluginPortsMigrationTest -j4 > .../06-build-red.log 2>&1` | 06 | 0 | Rebuilt on the reverted header. |
| 8 | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../07-test-red.log 2>&1` | 07 | 8 | RED: `positiveFractionHandlesVeryLargePositiveValues` fails `'result < 1.0f'` (`PluginPortsMigrationTest.cpp:1668`); `positiveFractionHandlesVeryLargeNegativeValues` fails `'result >= 0.0f'` (`:1675`). `Totals: 81 passed, 2 failed, 2 skipped`. The two other `Loc:` lines in the log are SKIPs (SF2Player not built), not failures. |
| 9 | `git apply .../fix.patch` | — | 0 | Fix restored; `git status --short` is empty again. |
| 10 | `CCACHE_TEMPDIR=... cmake --build build --target zene PluginPortsMigrationTest -j4 > .../08-build-restored.log 2>&1` | 08 | 0 | Rebuilt with the fix. |
| 11 | `cd build/tests && ctest -R '^PluginPortsMigrationTest$' --output-on-failure > .../09-test-restored.log 2>&1` | 09 | 0 | GREEN: `100% tests passed out of 1`. |
| 12 | `ctest -N -R 'ExprSynth\|PluginPortsMigration\|Xpressive'` | 11 | 0 | Family = 3 tests: `ExprSynthTest` (#582), `ExprSynthAllocationTest` (#586), `PluginPortsMigrationTest` (#587). |
| 13 | `CCACHE_TEMPDIR=... cmake --build build --target ExprSynthTest ExprSynthAllocationTest -j4 > .../12-build-family.log 2>&1` | 12 | 0 | Family targets built. |
| 14 | `cd build/tests && ctest -R 'ExprSynth\|PluginPortsMigrationTest' --output-on-failure > .../13-test-family.log 2>&1` | 13 | 0 | `100% tests passed out of 3`. |
| 15 | feature gates: `python3 $TMPDIR/gate-copy-new-p10-124/feature_check_relocated.py <worktree> > .../10-feature-check.log 2>&1` | 10 | 0 | `RESULT: PASS`. All 8 rows PASS; complexity and file-length "no new item in your files". |

Gate-run note: the stock `feature_check.py` writes its logs to `<worktree parent>/<worktree name>-feature-check`, which is outside the write area and returned `PermissionError`. I ran a copy from `$TMPDIR` that changes only two things: the log directory (into the run folder) and the harness root (pinned to the original `qwen/` folder so `FIX-BASE` and `gate-baselines` resolve). Gate logic, limits and baselines are unchanged.

RED/GREEN summary:

| Step | Command | EXIT | Tests run | Result |
|---|---|---:|---:|---|
| RED (fix reverted) | item 8 | 8 | 1/1 test target; 85 cases (81 pass, 2 fail, 2 skip) | `positiveFraction` large-value tests fail |
| GREEN (fix present) | item 11 | 0 | 1/1 test target passed (case count not printed on success) | all pass |

Affected family (`ExprSynth|PluginPortsMigrationTest`): 3/3 passed (item 14).

Gate result: `feature_check` RESULT PASS, exit 0 (item 15).

Files committed: `docs/qwen-fixes-2026-10/cluster-new-p10-124.md` only. No product or test file changed.

## Self-check

Completion check: `python3 .../qwen/submit_check.py <worktree> new-p10-124` exits 1 (log `.../logs/14-submit-check.log`). Its commits, clean-tree and report checks PASS. Its gates item FAILs for a reason outside the code: that check runs the stock `feature_check.py`, which must create `<worktree parent>/<worktree name>-feature-check`, and the write area does not allow that. The stock run exits 1 with `PermissionError` (log `.../logs/15-feature-check-stock.log`, EXIT=1). The same gate logic run from a copy whose only change is the log folder (and the harness root pinned) exits 0 with `RESULT: PASS` (item 15, log `10-feature-check.log`). So the gates pass in content, but the stock script has not been run to completion here, and the owner or the loop must run it where the log directory is writable.

1. **Regression FAILS with the change reverted and PASSES with it.** Reverting `plugins/Xpressive/ExprSynth.h` (`git apply -R` of `0427f1dc6`'s header diff) and rebuilding gives `ctest -R '^PluginPortsMigrationTest$'` EXIT 8 with `positiveFractionHandlesVeryLargePositiveValues` and `positiveFractionHandlesVeryLargeNegativeValues` failing (`PluginPortsMigrationTest.cpp:1668`, `:1675`; log `07-test-red.log`). Restoring the header and rebuilding gives EXIT 0 (log `09-test-restored.log`). The regression is the existing test from `0427f1dc6`; this cluster adds no test.

2. **Deterministic.** The three `positiveFraction` cases (`PluginPortsMigrationTest.cpp:1663-1684`) call the helper directly with fixed inputs: `std::numeric_limits<float>::max()`, its negation, `1.25f`, `-0.25f`, `-2.0f`. No sleeps, no clocks, no threads, no timing. The helper is pure.

3. **Fix at the cause, callers consistent.** The helper's callers are `plugins/Xpressive/ExprSynth.cpp:206, 224` (the wave lookups; `positiveFraction` output is in `[0, 1)`, so `(int)(... * m_size)` stays below `m_size`) and `ExprSynth.cpp:423, 432, 442, 460, 469, 482, 496, 509` (waveform functions). All of them receive the same fixed helper. The frozen `tests/reference/Xpressive/ExprSynth.h:124-132` copy still has the old code, but no CMake line compiles it; it is not a sibling in the build.

4. **No collateral damage.** The branch has no product change from this cluster, so nothing that loads or saves changes. The fixed helper keeps ordinary values identical to the old one: for `x = 1.25` both give `0.25`; for `x = -0.25` the old code gives `-0.25 + 1 = 0.75` and the fixed code gives `fmod(-0.25, 1) + 1 = 0.75`. The existing `positiveFractionPreservesOrdinaryValues` asserts these. NaN and infinite inputs still return `0`, as before.

5. **ALREADY-FIXED evidence (file:line).** Fixed helper `plugins/Xpressive/ExprSynth.h:149-162` (no float-to-`int` conversion; `fmod` at `:153`). Old helper in `0427f1dc6^`, `ExprSynth.h` lines 128-133 (`static_cast<int>(1-x)` and `static_cast<int>(x)`). Fix commit `0427f1dc6` is an ancestor of `HEAD` (`git merge-base --is-ancestor` EXIT 0). Regression cases `PluginPortsMigrationTest.cpp:1663-1677`.
