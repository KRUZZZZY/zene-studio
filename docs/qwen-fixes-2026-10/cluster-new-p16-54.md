# Cluster new-p16-54: a failed recovery assertion can leak the save-fault variable

**Verdict: FIXED**

## Decision

REAL, latent. The finding is correct about the mechanism and I fixed it, but the current suite is not yet
affected by it, because every test function that follows resets the seam at its own start.

- The seam is `ZENE_TEST_SAVE_FAULT`, read by `DataFile::writeFile()` at `src/core/DataFile.cpp:858`
  and checked at `:867`, `:898`, `:925`.
- Before the fix, `tests/src/core/DataFileSaveIntegrityTest.cpp:455` armed the seam, `:458` was
  `QVERIFY( !second.writeFile( target ) )`, and `:459` disarmed it. `QVERIFY` returns from the test
  function on failure, so if the save unexpectedly succeeded the disarm was skipped.
- Masking: the test that follows, `truncatedNewFileIsDiscardedWithReport` (`:489`), starts with
  `qunsetenv` (`:491`), and so do `unrelatedXmlNewFileIsNotAdopted` (`:532`),
  `deeplyNestedNewFileIsRefusedBeforeParsing` (`:571`) and `newFileDeclaringAnEnormousSizeIsRefusedBeforeDecompressing`
  (`:615`). No later test in this binary inherits the armed seam today.
- The sibling loop at `:383-387` already disarmed before asserting, so the `:458` site was the one out of
  step with the file's own pattern.

## Fix

Test-side only. No product file changed.

- `tests/src/core/DataFileSaveIntegrityTest.cpp:93-101`: new helper `saveWithFault()` arms the seam, calls
  `writeFile()`, disarms, and returns the result. Assertions run only after it returns.
- `:396`: the loop site (`:383` before the fix) now uses the helper.
- `:466`: the adoption test site (`:458` before the fix) is now `QVERIFY( !saveWithFault( ... "after-write" ) )`.
- `:492-506`: new regression `saveFaultIsDisarmedWhenTheSaveDoesNotStop`. It arms a seam name that
  `writeFile()` does not check, so the save succeeds, then asserts the seam is clear and the save returned true.

## Evidence

Test family: `DataFileSaveIntegrityTest` (1 CTest target, 16 test functions after the fix, 15 before it plus the regression).

| Step | Command (run from `build/tests` unless noted) | Result | Log |
| --- | --- | --- | --- |
| Build, before the helper existed | `cmake --build build --target DataFileSaveIntegrityTest -j4` with `CCACHE_TEMPDIR=/tmp/ccache-tmp-p16-54` | **EXIT=2**: `error: 'saveWithFault' was not declared` at `:457` and `:498` | `build-2-red.log` |
| RED (assertion-level) | Build with a deliberately broken helper that disarms only when the save stopped (`if( !saved ) qunsetenv`), then `ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` | build **EXIT=0**; ctest **EXIT=8**: `FAIL! ... saveFaultIsDisarmedWhenTheSaveDoesNotStop() 'qgetenv(...).isEmpty()' returned FALSE (the save-fault seam was left armed after the save)`; `Totals: 15 passed, 1 failed` | `build-3-red-mutant.log`, `test-1-red-mutant.log` |
| GREEN | Correct helper restored, `cmake --build ... -j4`, then `ctest -R '^DataFileSaveIntegrityTest$' --output-on-failure` | build **EXIT=0**; ctest **EXIT=0**, `100% tests passed out of 1` | `build-4-green.log`, `test-2-green.log` |
| Count, verbose | `ctest -R '^DataFileSaveIntegrityTest$' -V` | **EXIT=0**; `Totals: 16 passed, 0 failed`; `PASS: saveFaultIsDisarmedWhenTheSaveDoesNotStop()` | `test-3-green-verbose.log` |

About the RED step: the literal 4b recipe (`git diff -- <product files>` and `git apply -R`) does not apply here,
because no product file changed and the fix lives in the test file. Reverting the whole test file would also
remove the regression. So the RED proof is a targeted mutation: the one hazard the fix removes (cleanup skipped
on one path) was reintroduced in the helper alone, and the regression failed on its assertion. The
compile-level failure in `build-2-red.log` is the state before the helper existed.

Gates. `feature_check.py` could not run in this session: it creates `<worktree>-feature-check/` next to the
worktree (`/home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/`), which is outside the write
area (`PermissionError`, `gates-1.log`). I ran the same commands it lists, directly in the worktree, with logs in
the LOG FOLDER:

| Gate | Command | EXIT | Log |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | 0 | `gate-fork-sources.log` |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | 0 | `gate-all-sources.log` |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | 0 | `gate-no-upstream.log` |
| check-namespace | `tests/scripted/check-namespace` | 0 | `gate-check-namespace.log` |
| no-tautology | `bash tests/no-tautology-gate.sh` | 0 | `gate-no-tautology.log` |
| doc-figures | `python3 tests/doc-figures-gate.py` | 0 | `gate-doc-figures.log` |
| complexity (ratchet) | `bash tests/complexity-gate.sh --check` | 1 | `gate-complexity.log` |
| file-length (ratchet) | `bash tests/file-length-gate.sh --check` | 1 | `gate-file-length.log` |

The two ratchets are red on the train on purpose. Their `REGRESSION:` lines (15 in file-length, 6 in complexity)
name other files; none names `tests/src/core/DataFileSaveIntegrityTest.cpp` or the new functions, so no item is
new in a file this branch changed. I could not reproduce the base-listing comparison (it needs a git worktree in
the shared repo), so that part is by inspection of the output, not by the script.

## Self-check

- **The regression FAILS with the fix reverted and PASSES with it.** Done as a mutation, not the literal
  `git apply -R` recipe (see above). With the mutant helper, `test-1-red-mutant.log` shows EXIT=8 and the assertion
  `'...isEmpty()' returned FALSE`. With the correct helper, `test-2-green.log` shows EXIT=0 and 16/16 pass.
- **Deterministic.** No timing, sleeps or threads. The seam is a process env var set and cleared synchronously
  around one `writeFile()`. The regression uses a seam name (`no-such-point`) that `DataFile.cpp` does not check,
  so the save outcome is fixed by the code, not by the environment.
- **At the cause, complete.** Every arm site in the file was checked: `:396` (loop), `:466` (adoption), and the new
  regression `:507` all go through `saveWithFault()`. The remaining `qunsetenv` calls in the file are resets at
  the start of test functions and do not arm anything. `ZENE_TEST_SAVE_FAULT` is set nowhere else in `tests/`
  (`rg -n ZENE_TEST_SAVE_FAULT`). The product seam in `DataFile.cpp` is unchanged.
- **No collateral damage.** Test-only change. No product code, saved format, control command, or realtime path
  changed. The helper sets and clears a process env var at test time only.

## Notes for the reviewer

- `CCACHE_TEMPDIR=/tmp/ccache-tmp-p16-54` was needed for the build. ccache's default temp dir
  (`/run/user/1000/ccache-tmp`) was not writable from this session. This is environment only, not a code change.
- `make` printed `jobserver unavailable: using -j1` for the inherited `make-jobserver.fifo`. The build still
  ran with `-j4` requested at the command line; this affected only make's own jobserver.
