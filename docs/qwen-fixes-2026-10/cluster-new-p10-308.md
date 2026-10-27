Verdict: FIXED

# Cluster new-p10-308: invalid and oversized project versions parse as zero

Finding (Luna patrol, "BUG 9"): `ProjectVersion` converted each MAJOR/MINOR/PATCH component with a bare `QString::toInt()`, ignoring success. An out-of-range component became 0, so `2.999999999999.0` compared as `2.0.0`, and a non-numeric component such as `2.x.0` also compared as `2.0.0`.

Round 2 (review REWORK: "malformed components still silently become zero, 2.x.0 compares as 2.0.0") changes the malformed case. Round 1 had fixed only the out-of-range case.

## Policy

Each MAJOR, MINOR or PATCH identifier is one of three things:

| Input | Value | Valid | Reason |
| --- | --- | --- | --- |
| Empty, or only whitespace (missing) | 0 | yes | Padding of short versions, as before. `"" == "0.0.0"` still holds. |
| Digits that fit in an `int` | the number | yes | As before. |
| Digits that overflow an `int` | `INT_MAX` | yes | Round 1: sorts above every representable value. |
| Anything else (`x`, `1a`, `2 x`) | `-1` (`MalformedIdentifier`) | **no** | Reported, not read as 0. Sorts below every real number in its position. |

`isValid()` is false when any of the three identifiers is malformed. Two malformed identifiers in the same position compare equal. Prerelease labels are parsed exactly as before.

Consequences of `-1` for ordering:

- `2.x.0 < 2.0.0` and `2.x.0 < 2.0.1`, and `2.x.0 != 2.0.0`.
- Under `CompareType::Major` or `Minor`, a malformed identifier outside the compared slots does not matter. `2.x.0` equals `2.0.0` under Major, and `1.2.3x` equals `1.2.0` under Minor.
- The 1.2.2-era `MAJOR.MINOR.PATCH.COMMITS` hack still applies to malformed majors and minors, as it did when they were 0. A malformed patch is not bumped by the hack, so it does not become the number 0 + 1.

`compare()` now returns the three-way sign of each identifier comparison (`compareIdentifiers`). The old `a - b` could overflow once a value is `INT_MAX` and the other is `-1`. The sign of every comparison is unchanged for valid versions.

## Evidence (file:line on branch `bugs/qwen-new-p10-308`)

- Policy and parser: `src/core/ProjectVersion.cpp:37-76` (anonymous namespace: `MalformedIdentifier` at `:42`, `parseIdentifier` at `:54-67`, `compareIdentifiers` at `:71-74`).
- Constructor use: `src/core/ProjectVersion.cpp:95-101` (`m_valid` at `:101`); the hack's guarded bump at `:115`; the constructor's delegate `ProjectVersion(const char*)` at `:122-124`.
- Comparison: `src/core/ProjectVersion.cpp:130` onward; the component comparisons use `compareIdentifiers` at `:142-144`.
- Public accessor: `include/ProjectVersion.h:55-57` (`isValid()`), member `include/ProjectVersion.h:70` (`m_valid = true`).
- Regression tests: `tests/src/core/ProjectVersionTest.cpp:118-135` (`oversizedComponentsAreClamped`, round 1) and `:141-169` (`malformedIdentifiersAreNotZero`, round 2). Both are QTest slots of the existing `ProjectVersionTest` (ctest `ProjectVersionTest`, `tests/CMakeLists.txt:622`).
- Inherited-file registrations: `tests/upstream-modifications.txt` has one line for `src/core/ProjectVersion.cpp` (round 1) and one for `include/ProjectVersion.h` (round 2). Each carries a reason.

## Caller compatibility

Every caller of `ProjectVersion` in the tree, and what the policy changes for it:

1. **`src/core/DataFile.cpp:2798-2818`** (version-difference notice on load, `creatorversion`, Minor compare). Valid MAJOR and MINOR behave as before. A malformed MAJOR or MINOR now differs from the running version, so the notice appears, as it already did for any mismatch. A malformed PATCH is not compared under Minor, so the outcome is unchanged. This is pinned by `ProjectVersionTest.cpp:157`, which uses `0.3.0svn1024` under Minor.
2. **`src/core/DataFile.cpp:2846-2858`** (`legacyFileVersion()`, only for files with no `version` attribute or `version="1.0"`, reached from `DataFile.cpp:2776`). The `svn` tag is removed before parsing, so svn-tagged creators are numeric and behave as before. **Residual difference:** a version-less file whose `creatorversion` still has a non-numeric MAJOR/MINOR/PATCH after the `svn` removal, such as `1.2.x`, now sorts below `1.2.0`. Its `upper_bound` result over `UPGRADE_VERSIONS` moves from index 17 (`1.3.0`) to index 16 (`1.2.0-rc3`). That changes which upgrade routine runs. This was derived by hand from the ordering rules and is **not executed by any test**. No LMMS writer produces such a string, since `LMMS_VERSION` is numeric and `svn` is stripped first. Changing it back would need the zero mapping this round removed, so it is listed for the owner's decision.
3. **`src/core/ConfigManager.cpp:190-199`** (Minor compare of the config file's version with `LMMS_VERSION`) and **`:902-908`** (`legacyConfigVersion`, Build compare). Config versions are written by this program and are numeric, so behaviour is unchanged. A malformed value now sorts below every number instead of as 0.
4. **`src/core/DataFile.cpp:110-117`** (`UPGRADE_VERSIONS`, literals). All numeric, so the ordering of these fixed values is unchanged.

Not changed and out of scope: numeric prerelease labels still use `toInt()` (a label too large for an int still reads as 0); a fourth component on a version that is not the 1.2.2-era form is still ignored. Both are pre-existing and the finding names MAJOR, MINOR and PATCH only. Prerelease behaviour is preserved as the task required.

### Known residual limit

Two numeric components that both exceed `INT_MAX` clamp to the same value and compare equal (`2.99999999999.0` vs `2.999999999999.0` on minor). Exact ordering for arbitrarily large components would need a string-based comparison and is out of scope.

## Behaviour change for saved data

Changes only for: (a) a `creatorversion` or config version with an out-of-range number (round 1); (b) any MAJOR, MINOR or PATCH that is not a number (round 2), where the change matters only in the places listed under Caller compatibility; (c) the 1.2.2-era hack, which now does not apply to an out-of-range minor (round 1) and does not bump a malformed patch (round 2). In-range valid versions are unchanged. `INT_MAX` itself still parses exactly (`ProjectVersionTest.cpp:129`).

## Commands and results

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p10-308.r2/logs/` (round 2). Round 1 logs are in `.../new-p10-308.r1/logs/`.

Environment notes, unchanged from round 1: builds set `CCACHE_TEMPDIR=$TMPDIR/ccache-tmp-new-p10-308` because the default ccache temp directory is not writable here. `feature_check.py` and `submit_check.py` cannot create their log folders outside the write-jail, so I ran unchanged copies (`feature_check_jailcopy.py`, `submit_check_jailcopy.py`) from the run folder. Only the log and cache paths differ.

| Evidence | Command (run in worktree, or `build/tests` for ctest) | Result |
| --- | --- | --- |
| Build zene and both test targets, with the fix | `cmake --build build --target zene ProjectVersionTest DataFileFormatTest -j4 > build-1-green.log 2>&1` | EXIT=0 |
| ProjectVersionTest, with the fix (first assertion order) | `ctest -V -R '^ProjectVersionTest$' --output-on-failure > test-1-green.log 2>&1` | EXIT=0. QTest totals 8 passed, 0 failed |
| DataFileFormatTest, with the fix | `ctest -V -R '^DataFileFormatTest$' --output-on-failure > test-2-datafile.log 2>&1` | EXIT=0. QTest totals 10 passed, 0 failed |
| Rebuild and GREEN after reordering the assertions | `cmake --build build --target zene ProjectVersionTest -j4 > build-4-green.log 2>&1`; `ctest -V -R '^ProjectVersionTest$' ... > test-5-green.log 2>&1` | build EXIT=0; test EXIT=0 |
| 4b step 1: revert the `.cpp` hunk only (`git diff -- src/core/ProjectVersion.cpp > fix-r2.patch && git apply -R fix-r2.patch`) | the header keeps `isValid()`, so the test still compiles and fails on behaviour | patch EXIT=0; revert EXIT=0 |
| 4b step 2: rebuild and run on the reverted `.cpp` | `cmake --build build --target zene ProjectVersionTest -j4 > build-5-reverted.log 2>&1`; `ctest -V -R '^ProjectVersionTest$' ... > test-6-reverted-red.log 2>&1` | build EXIT=0; test EXIT=8. Failing assertion: `FAIL! : ProjectVersionTest::malformedIdentifiersAreNotZero() '!(ProjectVersion("2.x.0") == "2.0.0")' returned FALSE`, `Loc: ProjectVersionTest.cpp(146)`. QTest totals 7 passed, 1 failed |
| 4b step 3: `git apply fix-r2.patch`, rebuild, run | `cmake --build build --target zene ProjectVersionTest -j4 > build-6-refixed.log 2>&1`; `ctest -V -R '^ProjectVersionTest$' ... > test-7-refixed-green.log 2>&1` | apply EXIT=0; build EXIT=0; test EXIT=0. QTest totals 8 passed, 0 failed |
| Affected family ctest count | `ctest -R '^ProjectVersionTest$'` | 1 test executed (`#180`), 8 QTest functions passed |
| Collateral: DataFile version path | `ctest -V -R '^DataFileFormatTest$'` (`test-2-datafile.log`) | 1 test executed (`#97`), 10 passed |
| Gate, before header registration | `python3 -I .../feature_check_jailcopy.py <worktree> > gate-1-feature-check.log 2>&1` | EXIT=0, RESULT: PASS. Run before the `include/ProjectVersion.h` line was added to `tests/upstream-modifications.txt`, so it does not prove that registration |
| Gate, final | same command, `gate-2-feature-check.log` | EXIT=0, RESULT: PASS. All rows PASS: fork-sources, all-sources-reproduce, no-upstream-regression, check-namespace, no-tautology, doc-figures, complexity and file-length ("no new item in your files") |

Note on the reversal: the review asked for a proof that the test fails without the fix. Reverting the whole product diff would also remove `isValid()`, so the test would fail to compile and show nothing about behaviour. I reverted only the `.cpp`, which carries all the behaviour, and kept the header's declarations. The RED is therefore a behavioural failure on the round-1 parser, and it reproduces the reported symptom: `2.x.0 == 2.0.0` is true.

Files committed for this round: `include/ProjectVersion.h`, `src/core/ProjectVersion.cpp`, `tests/src/core/ProjectVersionTest.cpp`, `tests/upstream-modifications.txt`, and this report.

## Self-check

- **Each new test FAILS with the product change reverted, then PASSES restored (exit codes?):** yes for round 2. Reverted `.cpp`: `test-6-reverted-red.log`, EXIT=8, failing at `!(2.x.0 == 2.0.0)`. Restored: `test-7-refixed-green.log`, EXIT=0. Round 1 has its own evidence above.
- **The test is deterministic (no wall-clock, sleeps or races):** yes. `malformedIdentifiersAreNotZero` and `oversizedComponentsAreClamped` construct `ProjectVersion` from string literals and compare integers and booleans. No clock, I/O, threads or randomness, so no seam or injected value is needed.
- **Every acceptance criterion is proven by a test you ran yourself:** the malformed-component criterion by `malformedIdentifiersAreNotZero` (exit codes above). The missing-component and prerelease behaviour, which must not change, is asserted in the same test (`1.2`, `1`, `""`, `1.0.0-rc.x`) and in the existing `ProjectVersionComparisonTests`, which still pass.
- **Each edit is ONE undo step / reversibility:** not applicable. This is a parse-time change in a pure value class, with no commands, no undo and no document state.
- **Nothing you added runs on the audio thread:** not applicable. `ProjectVersion` is used only on load and config paths. The helpers allocate nothing beyond the `QString` operations already in the constructor.
- **Old projects still load and render exactly as before; new data round-trips:** in-range numeric versions are unchanged (their comparison sign is identical). `DataFileFormatTest` 10/10 passes. The one place where a previously-zero value now changes an outcome, `legacyFileVersion` for a non-numeric creator, is documented as a residual above and needs an owner decision. It is not covered by a passing test and I did not claim otherwise.
- **Qt 5.15 / MSVC / macOS portability:** `QString::toInt(bool*)`, `trimmed()`, `cbegin()/cend()`, `QChar::unicode()`, `std::all_of` (`<algorithm>`). All exist in Qt 5.15 and Qt 6. No Qt-6-only API, no `windows.h`, no `std::filesystem`.
- **Callers and siblings checked:** every caller is listed under Caller compatibility, with file:line. The constructor is the only parse path. `compare()`, the hack, and `isValid()` read the same members.
- **Verdict rule (ALREADY-FIXED / FALSE-POSITIVE citation):** not applicable. The verdict is FIXED, with the evidence at `src/core/ProjectVersion.cpp:37-76` and `:95-101`.
