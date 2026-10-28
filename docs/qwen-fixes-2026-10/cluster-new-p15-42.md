FIXED — cluster new-p15-42: incomplete UNC paths (`\\server`, no share) passed the absolute-path check

**Finding:** `src/core/ControlStemSupport.cpp` (was line 512): any value starting with two separators and at least three characters was accepted as an absolute Windows path, so `\\server` passed. On Unix, `\\server` is a relative name, so an output directory could be created under the working directory.

**Verdict: FIXED.** The defect is real on the base commit `37eaf07557` and is fixed on this branch.

## Evidence (file:line on this branch)

- Defect (base): the UNC arm of `isWindowsAbsolutePath` returned true for any two-separator prefix once the value was at least 3 characters long (base `ControlStemSupport.cpp:523-534`). `QDir::isAbsolutePath("\\\\server")` is false on Unix, so the old check passed only because of that arm. The RED run below shows `\\server` accepted.
- Fix, `src/core/ControlStemSupport.cpp:514-561`: `isPathSeparator` (514), `hasUncPrefixForHost` (520-527), `nextSeparator` (530-534), `isCompleteUncPath` (538-544: two separators, a non-empty server, a separator, a non-empty share), and `isWindowsAbsolutePath` (556-561), whose UNC case now delegates to `isCompleteUncPath` and whose drive-letter case is unchanged in meaning (560).
- Fix, `src/core/ControlStemSupport.cpp:569-582` (`stemRequireAbsolutePath`): a backslash-pair value on every host, and either separator pair on Windows, never reaches `QDir::isAbsolutePath` unless `isWindowsAbsolutePath` accepts a complete server+share. This refuses incomplete UNC values such as `\\server` on every host and `//server` on Windows, while preserving Unix `//...` absolute paths and the drive-path rules.
- Regression test, `tests/src/core/StemSplitPipelineTest.cpp:507-551` (`testValidatorRefusesIncompleteUncPaths`, method at 513): refuses `\\server`, `\\server\`, `\\\share\mix`, `\\s`; on Windows also refuses `//server`, `//server/`, and mixed-separator incomplete UNC prefixes. Unix explicitly keeps `//server` accepted. The test accepts `\\server\share`, `\\server\share\`, `\\server\share\stems`, and `//server/share`. It calls the pure validator only, so it is deterministic: no engine, timing or filesystem access.
- Callers, all of which validate command arguments with the same function: `src/core/ControlCommandsStems.cpp:122` (`source`), `src/core/ControlCommandsStems.cpp:181` (`out`), `src/core/ControlCommandsStemModel.cpp:94` (`dest_dir`). No other copy of the UNC logic exists in `src/` (grep for `UNC|isWindowsAbsolute|QDir::isAbsolutePath`).
- Existing UNC coverage still passes: `tests/src/core/StemSplitPipelineTest.cpp:480-481` (`\\server\share\mix.wav`, `//server/share/mix.wav`) and the stem family run below.
- Complexity: the first version of the helper (commit `deb3470c5c`) measured CCN 11 with lizard (base 8), over the CCN 10 limit. The completion check flagged it. The helper was split into `nextSeparator` / `isCompleteUncPath`, bringing every new function to the limit (`feature_check` complexity row PASS on the final tree).

## Commands and exit codes

Log folder: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-42.r1/logs`. Test commands are run from `build/tests`.

| Step | Command | EXIT |
| --- | --- | --- |
| Build, 1st try (ccache temp dir not writable, environment only) | `cmake --build build --target StemSplitPipelineTest -j2 > build-1-red.log 2>&1` | 2 |
| Environment workaround for ccache temp dir | `export CCACHE_TEMPDIR=$HOME/.cache/zene-tmp/ccache-tmp-new-p15-42` | — |
| Build, test added, product unfixed | `cmake --build build --target StemSplitPipelineTest -j2 > build-2-red.log 2>&1` | 0 |
| **RED** (product unfixed, first version of test) | `ctest -R '^StemSplitPipelineTest$' --output-on-failure > test-2-red.log 2>&1` | **8**: `FAIL! : testValidatorRefusesIncompleteUncPaths() ... returned FALSE (incomplete UNC path refused: \\server)`, `Totals: 16 passed, 1 failed` |
| Build, fix v1 | `cmake --build build --target zene StemSplitPipelineTest ControlStemWavTest StemSplitMenuTest -j2 > build-3-green.log 2>&1` | 0 |
| GREEN v1 | `ctest -R '^StemSplitPipelineTest$' --output-on-failure > test-3-green.log 2>&1` | 0 |
| Family v1 | `ctest -R '^(ControlStemWavTest\|StemSplitMenuTest\|StemSplitPipelineTest)$' --output-on-failure > test-4-family-green.log 2>&1` | 0 (3 of 3) |
| Build, final (CCN refactor) | `cmake --build build --target zene StemSplitPipelineTest ControlStemWavTest StemSplitMenuTest -j2 > build-6-refactor.log 2>&1` | 0 |
| **Family, final, verbose** | `ctest -R '^(ControlStemWavTest\|StemSplitMenuTest\|StemSplitPipelineTest)$' -V > test-8-refactor-family.log 2>&1` | **0** |
| 4b: diff vs base | `git diff 37eaf07557 -- src/core/ControlStemSupport.cpp > fix.patch` | 0 |
| 4b: revert fix | `git apply -R fix.patch` | 0 |
| 4b: build unfixed | `cmake --build build --target StemSplitPipelineTest -j2 > build-7-unfixed.log 2>&1` | 0 |
| **4b RED** (fix reverted, final test) | `ctest -R '^StemSplitPipelineTest$' --output-on-failure > test-9-red-final.log 2>&1` | **8**: `FAIL!` at `testValidatorRefusesIncompleteUncPaths()` (`\\server` accepted), `Totals: 16 passed, 1 failed` |
| 4b: re-apply fix | `git apply fix.patch` | 0 |
| 4b: build fixed | `cmake --build build --target StemSplitPipelineTest -j2 > build-8-refixed.log 2>&1` | 0 |
| **4b GREEN** (fix re-applied) | `ctest -R '^StemSplitPipelineTest$' --output-on-failure > test-10-green-final.log 2>&1` | **0** |
| Independent QA: restore base product source only | `git show 37eaf07557842a34c66bd34f6706cb0c31b0c737:src/core/ControlStemSupport.cpp > src/core/ControlStemSupport.cpp` | 0 |
| Independent QA: build with product source reverted | `cmake --build build --target zene StemSplitPipelineTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-42.r1/logs/qa-build-reverted.log 2>&1` | 0 |
| **Independent QA RED** (test change retained, product source reverted) | `ctest -R '^StemSplitPipelineTest$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-42.r1/logs/qa-test-reverted.log 2>&1` | **8**: `testValidatorRefusesIncompleteUncPaths()` rejects `\\server` as expected; `16 passed, 1 failed` |
| Independent QA: restore staged product source | `git checkout -- src/core/ControlStemSupport.cpp` | 0 |
| Independent QA: final build | `cmake --build build --target zene ControlStemWavTest StemSplitPipelineTest StemSplitMenuTest -j4 > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-42.r1/logs/qa-build-restored.log 2>&1` | 0 |
| **Independent QA GREEN**, affected family | `ctest -R '^(ControlStemWavTest|StemSplitPipelineTest|StemSplitMenuTest)$' --output-on-failure > /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p15-42.r1/logs/qa-test-restored.log 2>&1` | **0** (3 of 3; 27 test functions) |
| Independent QA: exact feature gate | `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py /home/kruzzzzy/Documents/AI_KOS_PROJECT/projects/zene-studio-program/zene-qwen-new-p15-42` | 0 |

**Affected-family test count (final code):** 27 test functions executed, 0 failed, across 3 executables: `StemSplitPipelineTest` 17 passed (includes `testValidatorRefusesIncompleteUncPaths`), `StemSplitMenuTest` 4 passed, `ControlStemWavTest` 6 passed (`test-8-refactor-family.log`, Totals at lines 48, 69, 88).

**Gates (final tree):** `qwen/feature_check.py` exits with `PermissionError` when run as shipped, because it writes its logs to `<worktree>/../zene-qwen-new-p15-42-feature-check`, outside this lane's write-jail (`gate-1-feature-check.log`, EXIT=1). The same gate logic was run from a copy in the LOG FOLDER (`gate-tool/feature_check_logdir.py`); the only changes are the log path and `ROOT` pointing at the original `qwen/` folder, so the cached base listings are still used. `gate-3-feature-check.log`, EXIT=0:

```
  PASS  fork-sources            exit 0
  PASS  all-sources-reproduce   exit 0
  PASS  no-upstream-regression  exit 0
  PASS  check-namespace         exit 0
  PASS  no-tautology            exit 0
  PASS  doc-figures             exit 0
  PASS  complexity              no new item in your files
  PASS  file-length             no new item in your files
RESULT: PASS
```

The independent QA also ran the exact feature-gate command on the final source and test changes (`qa-gate-final.log`, EXIT=0); complexity and file-length both passed.

An earlier gate run (`gate-2-feature-check.log`, EXIT=0, before the first commit) also reported complexity PASS while the same function measured CCN 11. I could not reproduce why that run passed. The completion check on the committed v1 flagged it, so the earlier PASS is not evidence for v1. The final-tree result above is the one to rely on.

**Files committed:** `src/core/ControlStemSupport.cpp`, `tests/src/core/StemSplitPipelineTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p15-42.md`. No new source files, so no `fork-sources.txt` / `upstream-modifications.txt` entry is needed. No control command was added and no snapshot changed.

## Self-check

1. **Test FAILS with the product change reverted and PASSES with it:** yes. The builder's final-tree proof is RED exit 8 and GREEN exit 0 (`test-9-red-final.log`, `test-10-green-final.log`). Independent QA repeated the reversal against the branch base while retaining the test: RED exit 8 at `StemSplitPipelineTest.cpp:522` for `\\server`, then restored the product source with `git checkout --` and got the affected family GREEN, EXIT 0 (`qa-test-reverted.log`, `qa-test-restored.log`).
2. **Deterministic:** yes. The test calls `control::stemRequireAbsolutePath` on fixed strings. No timing, sleep, threads, engine or filesystem.
3. **Fix at the cause, callers and siblings checked:** the cause is `isWindowsAbsolutePath`'s UNC arm plus the fall-through to `QDir`. All three callers (listed above) share the function and need no change. The drive arm and complete UNC shares retain their previous acceptance. On Unix, forward-slash absolute paths including `//server` still use `QDir`'s previous rule.
4. **No collateral damage:** the validator is used only at command-argument entry (the three callers above). Saved project data does not pass through it. The changed inputs are incomplete backslash-pair UNC values on all hosts and incomplete separator-pair UNC values on Windows. Complete shares remain accepted (family 27/27 passes).
5. **Realtime, undo, commands:** the validator runs on the control path, not the audio thread. No allocation, lock or log was added to any audio path. No undo step or command was added or changed.
6. **Qt 5.15 / MSVC / macOS portability:** the helpers use `QChar`, `QLatin1Char`, `QStringLiteral`, `QString::startsWith`, `QString::at`, `QString::size`, `Q_OS_WIN` and the existing `QDir::isAbsolutePath`, all available in Qt 5.15 and Qt 6. There is no `windows.h` or `std::filesystem` use.
7. **Residual, not changed (for the reviewer):**
   - A complete UNC share on a Unix host (`\\server\share\x`) is still accepted, as the helper's own comment states (`ControlStemSupport.cpp:519-528`, Q050-035). On Unix that value is a relative name, so a writer given it would still create a relative directory. The existing test `StemSplitPipelineTest.cpp:480` asserts this acceptance, so I did not refuse it. Whether a Windows-form output path should be refused on Unix is a design question for the owner, not something I changed.
   - On Unix, forward-slash `//server` with no share still falls through to `QDir::isAbsolutePath`, which treats it as absolute. This preserves the existing POSIX behavior.
   - The write-jail blocked the gate's and completion check's own log folder, so both were run from copies with only the log path redirected (see Gates above).
