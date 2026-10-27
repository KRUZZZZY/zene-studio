Verdict: FIXED

# Cluster new-p10-308: invalid and oversized project versions parse as zero

Finding (Luna patrol, "BUG 9"): `ProjectVersion` converted each MAJOR/MINOR/PATCH component with a bare `QString::toInt()`, ignoring success. A component outside `int` range became 0, so `2.999999999999.0` parsed with minor 0 and compared as `2.0.0`.

## Evidence (file:line on branch `bugs/qwen-new-p10-308`)

- The bug, before the fix: `src/core/ProjectVersion.cpp:51-53` (`toInt()` with no success check). Confirmed by the RED run: `ProjectVersion("2.999999999999.0").getMinor()` returned `0` (`test-2-red.log`).
- The fix: `src/core/ProjectVersion.cpp:44-53` adds `parseIdentifier()`. It keeps `toInt()` when the conversion succeeds. A conversion that fails on an all-digit string (out of `int` range) returns `INT_MAX`. Any other failed text still returns 0.
- Call sites now using it: `src/core/ProjectVersion.cpp:75-77`. The old-format hack (`m_minor <= 2`, `:84`) and `compare()` read the same parsed values, so they are consistent.
- Regression test: `tests/src/core/ProjectVersionTest.cpp:118-136` (`oversizedComponentsAreClamped`), registered as a QTest slot of the existing `ProjectVersionTest` (ctest `ProjectVersionTest`, `tests/CMakeLists.txt:622`).
- Inherited-file registration: `tests/upstream-modifications.txt` (last line, reason given).

### Deliberate non-change: non-digit text still parses as 0

Malformed text such as `2.x.0` still becomes 0. This is a decision, not an oversight. The existing tests `ProjectVersion("") == "0.0.0"` and `ProjectVersion("1-invalid") == "1.0.0-invalid"` (`ProjectVersionTest.cpp:108-109`) pin the padding behaviour. `DataFile::legacyFileVersion()` (`src/core/DataFile.cpp:2846-2858`) uses `std::upper_bound` over `UPGRADE_VERSIONS` with the raw `creatorversion` string, so the order of odd strings decides which upgrade routines run for old saved projects. Changing it would break "old projects load exactly as before". If the owner wants malformed text rejected, that needs a contract decision first (NEEDS-CONTEXT for that half).

### Known residual limit

Two numeric components that both exceed `INT_MAX` clamp to the same value and compare equal (`2.99999999999.0` vs `2.999999999999.0` on minor). Exact ordering for arbitrarily large components would need a string-based comparison and is out of scope for this bug.

## Behaviour change for saved data

Only a `creatorversion` (or config version) whose MAJOR/MINOR/PATCH has more digits than fit in an `int` changes. Those values used to parse as 0. Also, a huge MINOR no longer triggers the 1.2.2-era `MAJOR.MINOR.PATCH.COMMITS` hack, which needs `minor <= 2`. In-range values are unchanged: `INT_MAX` itself still parses exactly (asserted at `ProjectVersionTest.cpp:129`).

## Commands and results

Build and test logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-308.r1/logs/`.

Environment note: the first build failed with `ccache: error: failed to create temporary file ... Permission denied` (`build-1-red.log`, EXIT=2). That is an environment failure, not a test result. All later builds set `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p10-308` (`$TMPDIR` is on disk). Nothing else in the build or test setup changed.

| Evidence | Command (run from the worktree / `build/tests` as noted) | Result |
| --- | --- | --- |
| RED: regression on unfixed code | `cmake --build build --target ProjectVersionTest -j4 > build-2-red.log 2>&1` then `ctest -R '^ProjectVersionTest$' --output-on-failure > test-2-red.log 2>&1` (in `build/tests`) | build EXIT=0; test EXIT=8. Failing assertion: `FAIL! : ProjectVersionTest::oversizedComponentsAreClamped() Compared values are not the same`, `Actual (ProjectVersion("2.999999999999.0").getMinor()): 0`, `Expected (maxInt): 2147483647`, `Loc: ProjectVersionTest.cpp(125)`. QTest totals: 6 passed, 1 failed. |
| GREEN: after the fix | `cmake --build build --target zene ProjectVersionTest -j4 > build-3-green.log 2>&1`; `ctest -R '^ProjectVersionTest$' --output-on-failure > test-3-green.log 2>&1` (in `build/tests`) | build EXIT=0; test EXIT=0. 1 ctest test run, 100% passed. |
| 4b step 1: `git diff -- src/core/ProjectVersion.cpp > fix.patch && git apply -R fix.patch` | (product file only; test file kept) | patch EXIT=0; revert EXIT=0 |
| 4b step 2: rebuild and run on reverted code | `cmake --build build --target zene ProjectVersionTest -j4 > build-4-unfixed.log 2>&1`; `ctest -V -R '^ProjectVersionTest$' ... > test-4-unfixed-red.log 2>&1` | build EXIT=0; test EXIT=8. Same failing assertion (`ProjectVersionTest.cpp(125)`, Actual 0). QTest totals: 6 passed, 1 failed. |
| 4b step 3: `git apply fix.patch`, rebuild, run | `cmake --build build --target zene ProjectVersionTest -j4 > build-5-refixed.log 2>&1`; `ctest -V -R '^ProjectVersionTest$' ... > test-5-refixed-green.log 2>&1` | apply EXIT=0; build EXIT=0; test EXIT=0. QTest totals: 7 passed, 0 failed. |
| Affected family, ctest count | `ctest -R '^ProjectVersionTest$'` | 1 test executed (`#180`), 7 QTest functions passed (`test-5-refixed-green.log`). |
| Collateral: the DataFile version path (`legacyFileVersion`, `UPGRADE_VERSIONS`) | `cmake --build build --target DataFileFormatTest -j4 > build-6-datafile.log 2>&1`; `ctest -V -R '^DataFileFormatTest$' ... > test-6-datafile.log 2>&1` | build EXIT=0; test EXIT=0. 1 test executed (`#97`), QTest totals: 10 passed, 0 failed. |
| Gate, first attempt | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree> > gate-1-feature-check.log 2>&1` | EXIT=1, but this is environmental: `PermissionError` creating the log folder `zene-qwen-new-p10-308-feature-check` next to the worktree, outside the write-jail. No gate ran. |
| Gate, final (see note) | `python3 -I <run folder>/feature_check_jailcopy.py <worktree> > gate-2-feature-check.log 2>&1` | EXIT=0. All 8 rows PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures; complexity and file-length: "no new item in your files". RESULT: PASS. |

Note on the final gate run: it uses an unchanged copy of `feature_check.py` in the run folder. Only three paths differ: the log folder is `<LOG FOLDER>/feature-check`, the base-listing cache is the existing `qwen/gate-baselines` (the cached listing for base `5c1c6715d` was already there, so nothing was written), and the script's own directory is not used. The gate code is identical (`diff` shown in the log of the copy's creation). Only the output locations changed. The original script is unmodified.

Files committed for this verdict: `src/core/ProjectVersion.cpp`, `tests/src/core/ProjectVersionTest.cpp`, `tests/upstream-modifications.txt`, and this report `docs/qwen-fixes-2026-10/cluster-new-p10-308.md`.

## Self-check

- **Each new test FAILS with the product change reverted, then PASSES restored (exit codes?):** yes. Reverted: `test-4-unfixed-red.log`, EXIT=8, `ProjectVersionTest.cpp(125)`. Restored: `test-5-refixed-green.log`, EXIT=0. The same test file is used in both runs.
- **Every acceptance criterion is proven by a test you ran yourself:** the one criterion, "invalid/out-of-range components are represented or rejected explicitly, not zeroed", is proven by `oversizedComponentsAreClamped` (exit codes above). The malformed-text half is a deliberate non-change, explained above, not a claim of proof.
- **Each edit is ONE undo step / reversibility:** not applicable. This is a parse-time change in a pure value class, with no commands, no undo and no document state.
- **Nothing you added runs on the audio thread:** not applicable. `ProjectVersion` is used only on load and config paths, not from any audio path. The new helper does no allocation beyond the existing `QString` operations in the same constructor.
- **Old projects still load and render exactly as before; new data round-trips:** in-range versions are unchanged (existing assertions in `ProjectVersionComparisonTests` still pass, and `DataFileFormatTest` 10/10 passes). Only overflowing components change (see "Behaviour change for saved data").
- **Qt 5.15 / MSVC / macOS portability:** the code uses `QString::toInt(bool*)`, `QString::trimmed()`, `QString::cbegin()/cend()`, `QChar::unicode()` and `std::all_of` (`<algorithm>`), all available in Qt 5.15 and Qt 6. No Qt-6-only API, no `windows.h`, no `std::filesystem`.
- **Callers and siblings checked:** the constructor is the only parse path. `ProjectVersion(const char*)` delegates to it (`src/core/ProjectVersion.cpp:73-75`). Callers: `ConfigManager.cpp:190-199` and `:904-908`, `DataFile.cpp:2798-2805` and `:2849-2857`, `DataFile.cpp:110-117` (UPGRADE_VERSIONS, all in range, so unchanged). `compare()` and the 1.2.2 hack read the same members, so they stay consistent.
- **Verdict rule:** this is FIXED, not ALREADY-FIXED or FALSE-POSITIVE, so no contract citation is needed beyond the evidence above.
