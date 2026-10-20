**Verdict: FIXED**

# Cluster new-p14-398: three timeline tests not registered with QtTest

**Verdict: FIXED.** The finding is real. `tests/src/gui/ArrangerTimelineTest.cpp` declared a stray `private:` label directly above three test methods, so they were ordinary private methods and QtTest, which runs only `private slots:`, never ran them. The fix removes that label. The three tests now sit in the `private slots:` section and run. A new check in the same file fails while they are not slots and passes once they are.

## Evidence

Baseline (unmodified tree, HEAD `2da2ed356`):

- `tests/src/gui/ArrangerTimelineTest.cpp:802` (before the fix): `private:` directly above `aBurstOfRealNudgesIsOneUndoStep()` (`:806`), `theNavigatorRangeIncludesSongsNearTheTickLimit()` (`:834`) and `oversizedScrollPositionsReachTheEndOfTheArranger()` (`:848`).
- `./ArrangerTimelineTest -functions` on the baseline binary lists 25 functions, and none of the three names (grep exit 1).
- The sibling search (multiline `private:` followed by a `void name()` method, across `tests/src`) found ten files. Every other match is a helper (`rereadClips`, `makeFolder`, `period`, `joinBoth`, `joinSession`, `makeClip`, `buildGraph`, plus a support header). `ArrangerTimelineTest.cpp` is the only file where test methods sit under `private:`.

Regression test (`tests/src/gui/ArrangerTimelineTest.cpp:802-811`, `everyTimelineTestIsAPrivateSlot()`): it asks `staticMetaObject.indexOfSlot(name)` for each of the three names. `indexOfSlot` returns -1 for a method that is not a slot, so the test fails whenever one of them is not registered.

Fix (`tests/src/gui/ArrangerTimelineTest.cpp:813`): the `private:` label that stood above the nudge test (old line 802) is removed. The `private slots:` section (`:122`) now runs through the three tests, and the data members keep their own `private:` at `:869`.

After the fix, the binary registers 29 functions. Run verbose (`run-green-v1.log`): 31 passed, 0 failed. The three moved tests all pass on the current product code, including the nudge test's undo-depth and transaction-record assertions that were never checked before.

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p14-398.r1/logs`

| Step | Command (run from the worktree unless noted) | EXIT |
| --- | --- | --- |
| build baseline | `cmake --build build --target ArrangerTimelineTest -j4` (`build-1-baseline.log`) | 2 (ccache could not write `/run/user/1000/ccache-tmp`: Permission denied; not a code error) |
| build baseline, retry | `env CCACHE_TEMPDIR=/tmp/ccache-tmp-p14 cmake --build build --target ArrangerTimelineTest -j4` (`build-2-baseline-retry.log`) | 0 |
| baseline functions | `build/tests/ArrangerTimelineTest -functions` (`functions-baseline.log`) | 0 (25 functions, none of the three) |
| **RED** build | `env CCACHE_TEMPDIR=... cmake --build build --target ArrangerTimelineTest -j4` with the check added, before the fix (`build-4-red.log`) | 0 |
| **RED** test | `cd build/tests && ctest -R '^ArrangerTimelineTest$' --output-on-failure` (`test-2-red.log`) | **8** (`FAIL! : ...everyTimelineTestIsAPrivateSlot() 'staticMetaObject.indexOfSlot(name) >= 0' returned FALSE. (aBurstOfRealNudgesIsOneUndoStep())`, `Loc: [ArrangerTimelineTest.cpp(809)]`; `Totals: 27 passed, 1 failed`) |
| **GREEN** build | with the fix (`build-5-green.log`) | 0 |
| **GREEN** test | `ctest -R '^ArrangerTimelineTest$' --output-on-failure` (`test-3-green.log`) | **0** (`1/1 Test #363: ArrangerTimelineTest ... Passed`, `100% tests passed out of 1`) |
| GREEN verbose | `build/tests/ArrangerTimelineTest -v1` (`run-green-v1.log`) | 0 (`Totals: 31 passed, 0 failed`; PASS for all four new or moved functions) |
| 4b revert | `git apply -R fix.patch` (`apply-R-4b.log`), then build (`build-6-4b-revert.log`, EXIT 0) and ctest (`test-4-4b-revert.log`) | apply 0, build 0, **ctest 8** (FAIL, same assertion) |
| 4b reapply | `git apply fix.patch` (`apply-4b.log`), `cmp` against the saved fixed file (identical), then build (`build-7-4b-reapply.log`, EXIT 0) and ctest (`test-5-4b-reapply.log`) | apply 0, build 0, **ctest 0** (`Passed`, `100% tests passed out of 1`) |

4b note: the fix touches only the test file, so there is no product diff to revert. `fix.patch` is the one-hunk diff that removes the `private:` label (the regression check stays in place while the fix is reverted). That is why the mutation step reverts the fix rather than the whole file.

Affected family: `ctest -R '^ArrangerTimelineTest$'`, 1 test executed, 1 passed. The change is in this test file only, so no other test binary is affected.

Gates. `feature_check.py` could not run here. It writes its logs to `<worktree>/../<name>-feature-check/`, which is `zene-qwen-new-p14-398-feature-check` in the shared projects directory, and that mkdir fails with `PermissionError: [Errno 13]` (`gate-1-feature-check.log`, EXIT 1). I did not change the gate. I ran each of its gate commands directly from the worktree with the same arguments. The table is from the final run after the last edit (`gate-final-*.log`); the earlier run gave the same exit codes:

| Gate | Command | EXIT |
| --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 |
| check-namespace | `tests/scripted/check-namespace` | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 (6 `REGRESSION` lines; none names `ArrangerTimelineTest.cpp`) |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 (`REGRESSION: new file over 500 lines: tests/src/gui/ArrangerTimelineTest.cpp (696)`) |

The two ratchet exits are not a new failure in my files, for these reasons:

- Complexity: none of the six `REGRESSION` lines is in a file I changed. The new function is a loop with one `QVERIFY2`, far below CCN 10.
- File-length: the file already exceeded 500 code lines at base. Counting HEAD with the gate's own `code_lines` rule (`/tmp/p14-base-count/count.sh`, copied from `tests/file-length-gate.sh:106-136`) gives 689. My change makes it 696 (`linecount-head.txt`, `linecount-now.txt`). The base is HEAD (`merge-base HEAD 050/m0-train-8` = `2da2ed356`), so the same `new file over 500 lines` key is in the base listing, and the feature check's "new item in your files" comparison does not count it. I added 7 code lines to a file that was already red on the train; I did not re-anchor anything.

## Self-check

- **Regression test fails with the fix reverted, passes with it:** RED `test-2-red.log` ctest EXIT 8; GREEN `test-3-green.log` ctest EXIT 0; 4b revert `test-4-4b-revert.log` EXIT 8; 4b reapply `test-5-4b-reapply.log` EXIT 0. The reverted state is the one the check asserts against: `ArrangerTimelineTest.cpp:809` is the check's `QVERIFY2` in the RED file.
- **Deterministic:** the new check reads only the meta-object; it uses no timing, sleeps or threads. The three moved tests are unchanged by this fix. `theNavigatorRangeIncludesSongsNearTheTickLimit` uses a `QTRY_VERIFY` poll (`:855`, inside `:844-856`); that wait is pre-existing and now runs. It passed in every run here (`test-3-green`, `run-green-v1`, `test-5-4b-reapply`). I did not change it, and if it is ever flaky it should be reviewed on its own.
- **Fix at the cause and complete:** the cause is the single `private:` label, and removing it changes only registration, not behaviour. Siblings checked: the other nine files from the multiline search all have helpers under `private:`, not tests. Caveat: that search required `private:` directly above a `void name()` method (optional `//!` lines between), so a sibling written differently would not show.
- **No collateral damage:** no product code changed; no saved data or project format is touched. No limit or check was added to product code; the only new check is the test.
- **Verdict REAL, not ALREADY-FIXED or FALSE-POSITIVE:** the evidence is `ArrangerTimelineTest.cpp:802` before the fix and the baseline function list (25 functions, none of the three).

## Notes for the loop

- Builds in this sandbox need `CCACHE_TEMPDIR=/tmp/ccache-tmp-p14` on the command line. The default ccache temp dir `/run/user/1000/ccache-tmp` returned Permission denied. No config was changed.
- `feature_check.py` cannot write its logs from this sandbox (see above). Its gate commands were run directly with the same arguments, and the results are in the table.
