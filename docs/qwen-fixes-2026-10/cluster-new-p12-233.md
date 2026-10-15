# Cluster new-p12-233: an unreadable legacy launch quantisation loads as None

## Verdict: FIXED

The finding is real. `SessionModel::restoreState` and `SessionModel::restoreScenesSection` read the `launchquantisation` attribute with `QString::toInt()` and no success check. A failed conversion returns 0, and 0 is `LaunchQuantisation::None` (`include/SessionModel.h:73`). So `launchquantisation="bar"` loaded as None, and launches started at once instead of on the default bar line.

The fix is in `src/core/SessionModel.cpp:53-59`. The new helper `restoreGlobalLaunchQuantisation` reads the attribute with `ok`. A value that does not parse takes `SessionModel::DefaultLaunchQuantisation` (Bar). That is the fallback the file already gives any non-grid integer (`src/core/SessionModelPrivate.h:83` comment, `:100` code). A real `0` still restores as None, and the regression test checks that.

Choices made:

- **Fallback, not rejection.** The finding offered two options: apply the default, or preserve the malformed block. I chose the default. Rejecting the block would drop the whole session, and the existing non-grid rule already gives Bar.
- **Native section included.** `restoreScenesSection` reads the same attribute through the same conversion (`src/core/SessionModel.cpp:535`), so it uses the same helper. The finding names the `<session>` path, which is `:426`.
- **Line numbers moved.** The finding's line 401 is now a comment in the version-policy block. The defect sat at the `launchquantisation` conversion, which is now `:426` and `:535`.

Not changed, reported only: `src/core/SessionClip.cpp:178-179` reads the per-clip `quantisation` attribute with the same pattern. A failed conversion becomes None there as well (its fallback is Global). That is outside this finding's path. I did not test or change it. It should be a separate cluster.

Brief note: the "Luna round-1" section of the brief lists no points, and the run folder has no review text. The branch was created from `b02a0ff2b` and has no earlier commits for this cluster. I treated the finding as the scope.

## Regression test

Two tests in `tests/src/core/SessionModelTest.cpp`:

- `anUnreadableSessionLaunchQuantisationFallsBackToBar` (line 360): a `<session>` block with `launchquantisation="bar"` restores as Bar. `launchquantisation="0"` still restores as None.
- `anUnreadableSceneSectionLaunchQuantisationFallsBackToBar` (line 380): a native `<z:scenes>` section written by `saveScenesSection`, with its attribute changed to `bar`, restores as Bar.

## Evidence

The log folder is `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p12-233.r2/logs` (abbreviated `LOGS` below). Every command ran from the worktree unless it says `build/tests`.

| # | Step | Exact command | EXIT | Result |
|---|---|---|---:|---|
| 1 | Build test, product unchanged (first try) | `cmake --build build --target SessionModelTest > LOGS/build-1.log 2>&1; echo EXIT=$?` | 2 | Failed before compiling the test: ccache could not create `/run/user/1000/ccache-tmp/...` (Permission denied). Environment, not code. |
| 2 | Build test, product unchanged, writable ccache temp | `mkdir -p /tmp/ccache-tmp-new-p12-233 && CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p12-233 cmake --build build --target SessionModelTest > LOGS/build-2.log 2>&1; echo EXIT=$?` | 0 | Built. |
| 3 | **RED**, original code plus regression tests | `cd build/tests && ctest -R '^SessionModelTest$' --output-on-failure > LOGS/test-1.log 2>&1; echo EXIT=$?` | **8** | 10 of 12 slots pass, 2 fail. `anUnreadableSessionLaunchQuantisationFallsBackToBar`: Actual 0, Expected 1, `SessionModelTest.cpp(368)`. `anUnreadableSceneSectionLaunchQuantisationFallsBackToBar`: Actual 0, Expected 1, `SessionModelTest.cpp(396)`. |
| 4 | Fix applied, rebuild `zene` | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p12-233 cmake --build build --target zene > LOGS/build-3.log 2>&1; echo EXIT=$?` | 0 | Built. |
| 5 | Rebuild test | `CCACHE_TEMPDIR=/tmp/ccache-tmp-new-p12-233 cmake --build build --target SessionModelTest > LOGS/build-4.log 2>&1; echo EXIT=$?` | 0 | Built. |
| 6 | **GREEN**, fix applied | `cd build/tests && ctest -R '^SessionModelTest$' -V > LOGS/test-3.log 2>&1; echo EXIT=$?` | **0** | 12 of 12 slots pass, 0 fail. |
| 7 | Save product patch (product file only) | `git diff -- src/core/SessionModel.cpp > LOGS/fix.patch; echo EXIT=$?` | 0 | `fix.patch` saved. |
| 8 | Reverse product patch | `git apply -R LOGS/fix.patch; echo EXIT=$?` | 0 | `git diff --stat` then showed only `SessionModelTest.cpp`. |
| 9 | Rebuild with product reversed | `cmake --build build --target zene` (`build-5.log`), then `cmake --build build --target SessionModelTest` (`build-6.log`) | 0, 0 | Built. |
| 10 | **RED** again, product reversed | `cd build/tests && ctest -R '^SessionModelTest$' -V > LOGS/test-4.log 2>&1; echo EXIT=$?` | **8** | 10 pass, 2 fail, same Actual 0 messages as step 3. |
| 11 | Reapply product patch | `git apply LOGS/fix.patch; echo EXIT=$?` | 0 | Restored. `git diff --stat` shows both files. |
| 12 | Rebuild with fix restored | `cmake --build build --target zene` (`build-7.log`), then `cmake --build build --target SessionModelTest` (`build-8.log`) | 0, 0 | Built. |
| 13 | **GREEN** again after reapply | `cd build/tests && ctest -R '^SessionModelTest$' -V > LOGS/test-5.log 2>&1; echo EXIT=$?` | **0** | 12 pass, 0 fail. |
| 14 | Build affected family | `cmake --build build --target ScenesUpconvertTest ProjectVersionTest ProjectOpenIntegrityTest SessionGridRecordTest SessionAudioRecordTest SessionSchedulerTest SessionSchedulerRenderTest > LOGS/build-9.log 2>&1; echo EXIT=$?` | 0 | Built. |
| 15 | **Affected family** | `cd build/tests && ctest -R 'SessionModelTest\|ScenesUpconvertTest\|ProjectVersionTest\|ProjectOpenIntegrityTest\|SessionGridRecordTest\|SessionAudioRecordTest\|SessionSchedulerTest\|SessionSchedulerRenderTest' --output-on-failure > LOGS/test-6.log 2>&1; echo EXIT=$?` | 0 | **8 of 8 ctest tests passed.** |
| 16 | Gate, file length, base commit before any edit | `bash tests/file-length-gate.sh --check > LOGS/gate-pre-file-length-1.log 2>&1; echo EXIT=$?` | 1 | 14 pre-existing REGRESSION entries, none in session files. |
| 17 | Gate, complexity, base commit before any edit | `bash tests/complexity-gate.sh --check > LOGS/gate-pre-complexity-1.log 2>&1; echo EXIT=$?` | 1 | 6 pre-existing REGRESSION entries, none in session files. |
| 18 | **Gate, file length, final tree** | `bash tests/file-length-gate.sh --check > LOGS/gate-post-file-length-1.log 2>&1; echo EXIT=$?` | 1 | REGRESSION list identical to step 16. |
| 19 | **Gate, complexity, final tree** | `bash tests/complexity-gate.sh --check > LOGS/gate-post-complexity-1.log 2>&1; echo EXIT=$?` | 1 | REGRESSION list identical to step 17. |

**Affected-family test count:** 8 ctest tests (`SessionModelTest` plus 7 related). `SessionModelTest` runs 12 slots, of which 2 are the new regression tests.

**Regression proof:** steps 3 and 10 are the RED runs, and both show the same two failing assertions. Steps 6 and 13 are the GREEN runs.

## Gate status

Both required gates exit 1, and that is not caused by this change.

- Before any edit (step 16 and 17), both already failed on the base commit `b02a0ff2b`.
- After the fix (step 18 and 19), the REGRESSION lists are identical to the base run. This change adds no gate entries.
- The failing entries are in other files: 14 test or source files over 500 code lines, and 6 functions over CCN 10. None are `SessionModel*` or `SessionClip*`.

I did not fix those entries. Doing so means editing unrelated files, and the gate baselines must not be re-anchored. Gate status for the branch is therefore still red, as it was before this commit.

## Files committed

- `src/core/SessionModel.cpp`: the helper and its two call sites.
- `tests/src/core/SessionModelTest.cpp`: the two regression tests.
- `docs/qwen-fixes-2026-10/cluster-new-p12-233.md`: this report.

No new source or test file was added, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` did not change. Both edited product and test files were already registered in `tests/fork-sources.txt`.
