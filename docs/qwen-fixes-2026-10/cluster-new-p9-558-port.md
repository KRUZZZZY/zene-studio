# Cluster new-p9-558-port — edge snap distance overflow

**Verdict: ALREADY-FIXED** on `bugs/qwen-new-p9-558-port` (base `ded355440`). The fix from `bugs/qwen-new-p9-558` (`69339d683`) is already in this line as `07895d855` (2026-10-06, "fix(arrangement): edge snapping overflowed while measuring the distance"). This port adds only the regression test that the old commit carried, so the fix is pinned at the signed tick limits.

## Evidence

- `src/core/ArrangementEdits.cpp:434-447` (current): `std::int64_t bestDistance = std::int64_t{tolerance} + 1;`; `if (candidate < 0) { continue; }` runs before the subtraction; the distance is `std::abs(std::int64_t{candidate} - std::int64_t{proposed})`.
- `git merge-base --is-ancestor 07895d855 HEAD` → ancestor (`IN_HEAD`).
- Regression test (ported, `tests/src/core/ClipJoinRippleTest.cpp`, `edgeSnapKeepsDistancesWithinTickRange`): a marker at tick 0 (so candidate `0` exists), `length = 1`, `tolerance = 4`. It checks `proposed = INT32_MAX` (the finding's input) and `proposed = INT32_MIN`.
- Observable failure: with the fix reverted, `proposed = INT32_MIN` snaps to `0` (distance wraps). The test fails with `Actual (nearMinimum.start): 0`, `Expected (minimum): -2147483648`.
- The finding's exact inputs (`INT32_MAX`, edge 0) give the same output before and after the fix: the pre-fix `abs(INT32_MIN)` on candidate `-1` is undefined behaviour, but the `candidate >= 0` check discards that candidate either way. I did not check this with a sanitizer. The observable bug is the minimum-tick case above.
- Existing coverage: `ArrangementModesTest::aMaximumToleranceStillSnaps` (`tests/src/core/ArrangementModesTest.cpp:221`) covers the tolerance-overflow path from `07895d855`.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p9-558-port.r1/logs`. The fix patch is `$LOGS/fix.patch` (`git show 07895d855 -- src/core/ArrangementEdits.cpp`).

| Step | Command | Exit |
| --- | --- | --- |
| Reverse-check the fix | `git apply -R --check $LOGS/fix.patch` | 0 |
| Revert the fix (RED setup) | `git apply -R $LOGS/fix.patch > $LOGS/revert-1.log 2>&1` | 0 |
| First build attempt (env failure, see note) | `cmake --build build --target zene ClipJoinRippleTest > $LOGS/build-1.log 2>&1` | 2 |
| Rebuild with ccache temp dir in /tmp | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-558 cmake --build build --target zene ClipJoinRippleTest > $LOGS/build-2.log 2>&1` | 0 |
| **RED** (fix reverted) | from `build/tests`: `ctest -R '^ClipJoinRippleTest$' --output-on-failure > $LOGS/test-red-1.log 2>&1` | **8** (`0% tests passed, 1 tests failed`) |
| Reapply the fix | `git apply $LOGS/fix.patch > $LOGS/apply-fix-1.log 2>&1` | 0 |
| Rebuild with fix | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-558 cmake --build build --target zene ClipJoinRippleTest > $LOGS/build-3.log 2>&1` | 0 |
| **GREEN** (fix applied) | from `build/tests`: `ctest -R '^ClipJoinRippleTest$' --output-on-failure > $LOGS/test-green-1.log 2>&1` | **0** (`100% tests passed, 0 tests failed out of 1`) |
| Build neighbour target | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p9-558 cmake --build build --target ArrangementModesTest > $LOGS/build-4.log 2>&1` | 0 |
| Family (verbose) | from `build/tests`: `ctest -V -R '^(ArrangementModesTest\|ClipJoinRippleTest)$' --output-on-failure > $LOGS/test-family-verbose-1.log 2>&1` | 0 |
| Complexity gate | `bash tests/complexity-gate.sh --check > $LOGS/gate-complexity-1.log 2>&1` | **1** (see below) |
| File-length gate | `bash tests/file-length-gate.sh --check > $LOGS/gate-filelength-1.log 2>&1` | **1** (see below) |

**Tests executed:** the RED/GREEN family is `ClipJoinRippleTest` (1 CTest executable, 16 QTest cases, 16 passed) and `ArrangementModesTest` (1 CTest executable, 18 QTest cases, 18 passed). The family run reports `100% tests passed out of 2`, exit 0. Zero tests run would have been an error; that did not happen.

**Note on the failed first build:** `build-1.log` failed with `ccache: error: failed to create temporary file for /run/user/1000/ccache-tmp/...: Permission denied`. The fix was the environment only: `CCACHE_TEMPDIR` pointed at `/tmp`. The source was not changed by it.

## Gate results (not caused by this change)

Both gates exit 1. Neither the complexity nor the file-length output lists `src/core/ArrangementEdits.cpp` or `tests/src/core/ClipJoinRippleTest.cpp`.

- Complexity: `REGRESSION` entries are in `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ProjectRevisions.cpp`, `ControlCommandsShortcuts.cpp`. The new test function has no branches.
- File-length: `REGRESSION` entries are in `SmfInterchangeReader.cpp`, `TakeLane.cpp`, and about a dozen `tests/src/core/*Test.cpp` files. `ClipJoinRippleTest.cpp` is 366 lines; `ArrangementEdits.cpp` is 517 lines, the same as HEAD (`git show HEAD:... | wc -l`), and it is not in the baseline or the exempt list.

The uncommitted diff before this commit was the test file only, plus the registration line, so these entries come from the base line, not from this change. I did not run the gates on a pristine HEAD checkout to confirm that. The baselines were not edited, and no limits were changed.

## Files committed

- `tests/src/core/ClipJoinRippleTest.cpp`: `#include "ProjectMarkers.h"` and the `edgeSnapKeepsDistancesWithinTickRange` test.
- `tests/upstream-modifications.txt`: one registration line for the test file.
- `docs/qwen-fixes-2026-10/cluster-new-p9-558-port.md`: this report.

No product code was changed. `src/core/ArrangementEdits.cpp` matches HEAD (the revert was reapplied).
