# Cluster new-p17-133: revision rotation on Windows with non-ASCII paths

**Verdict: BLOCKED** — the defect is real and the one-line fix plus a Windows-only regression test are committed. The RED→GREEN proof cannot run on this Linux host: the defect and the test are both inside `#ifdef Q_OS_WIN`, and there is no Windows toolchain here to build or run them. The Windows run that closes this is described under "Regression proof".

## Finding

Luna's report: on Windows, `replaceWithStaged` converts the staged and destination paths with `toLocal8Bit()` before calling `std::filesystem::rename`. The narrow overload converts through the active ANSI code page, so a path with characters that page cannot represent is mangled and the replacement fails. Revision rotation then cannot address the staged or destination file.

## Evidence (before the fix)

- `src/core/ProjectRevisions.cpp:68-71` (the finding's line 68 is `#ifdef Q_OS_WIN`; the rename was line 70): the Windows branch was `std::filesystem::rename(staged.toLocal8Bit().constData(), to.toLocal8Bit().constData(), ec);`. `grep -c toLocal8Bit` over `ProjectRevisions.cpp`, `ControlCommandsRevisions.cpp`, `ControlReversibilityTableRevisions.cpp`, `RevisionTimelineCompare.cpp`, `RevisionTimeline.cpp` and `RevisionTimelineGit.cpp` finds no other use.
- Both callers of `replaceWithStaged` take the fix together: `copyRevision` (`:124-126`, reached from the rotation shift in `shiftRevisionsUp` at `:200` and from `rotateProjectRevision` at `:269`) and the project replacement in `replaceProjectWithStaged` (`:386`, call at `:395`).
- The POSIX branch (`:75-78`) uses `QFile::encodeName` with `std::rename`, which is correct and unchanged.
- The repo's existing wide-path idiom: `src/core/DataFile.cpp:160-161` and `src/core/BounceInPlace.cpp:195` both build `std::filesystem::path(x.toStdWString())`. The fix uses the same idiom.

## Fix

`src/core/ProjectRevisions.cpp:68-74` (Windows branch only): the rename now receives `std::filesystem::path(staged.toStdWString())` and `std::filesystem::path(to.toStdWString())`, with a comment on why the narrow overload is wrong. The POSIX branch, the test seam, the staging and the error handling are untouched.

## Regression test

`tests/src/core/ProjectRevisionRestoreStagingTest.cpp`, new slot `rotationAndRestoreUseUnicodePathOutsideAnsiCodePage`, compiled only under `#ifdef Q_OS_WIN` (the same guard `RevisionTimelineTest.cpp:982` uses), so the Linux test set is unchanged.

What it does, with real files in the fixture's `QTemporaryDir`:

1. Creates a directory named `proj-` plus U+1F3B5 (a musical-note emoji, which no ANSI code page holds) and a project `song.mmp` in it.
2. Writes a document, calls `rotateProjectRevision` once and expects a count of 1, `refused == false`, and revision 0 holding the bytes. This runs the copy-to-revision-0 rename.
3. Writes a second document, rotates again and expects a count of 2, revision 0 holding the new bytes and revision 1 holding the first. This runs the shift rename and the replacing rename.
4. Writes a third document and calls `restoreProjectRevision(project, 1)`; the live file must then hold the first document. This runs the project replacement rename.

Skip condition: if `QString::fromLocal8Bit(name.toLocal8Bit()) == name`, the active code page holds the name (UTF-8 does), the defect cannot occur, and the test calls `QSKIP`.

**Mechanism, pre-fix, on a code page without the emoji (for example CP1252):** `toLocal8Bit()` turns the name into `proj-?`, the rename targets a directory that does not exist, `copyRevision` fails, and `rotateProjectRevision` returns `refused == true`, so the `QVERIFY2(!refused, …)` at step 2 fails. **Post-fix:** wide paths name the real directory and every step passes. Both are reasoning from the code, not a Windows run.

Determinism: no clock, sleep, random value, thread or injected seam. The name is a fixed literal and the skip is a deterministic property of the code page.

## Commands and results

| Step | Command (from the worktree, or `build/tests` for ctest) | EXIT |
| --- | --- | --- |
| Baseline build, first try | `cmake --build build --target ProjectRevisionRestoreStagingTest RevisionTimelineTest -j4 > …r1/logs/build-1-baseline.log 2>&1` | 2 (ccache could not write to `/run/user/1000/ccache-tmp`; not a code error) |
| Baseline build, retry | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p17 cmake --build build --target ProjectRevisionRestoreStagingTest RevisionTimelineTest -j4 > …r1/logs/build-2-baseline.log 2>&1` | 0 |
| Baseline tests (unmodified code) | `cd build/tests && ctest -R '^(ProjectRevisionRestoreStagingTest\|RevisionTimelineTest)$' --output-on-failure > …r1/logs/test-1-baseline.log 2>&1` | 0 (2/2 passed) |
| Standalone syntax check of the fix statement | `g++ -std=c++20 -fsyntax-only $(pkg-config --cflags Qt6Core) win_branch_check.cpp` (file in `/tmp`) | 0 |
| Fixed build, round 1 | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p17 cmake --build build --target zene ProjectRevisionRestoreStagingTest RevisionTimelineTest -j4 > …r1/logs/build-3-fixed.log 2>&1` | 0 |
| Fixed tests, round 1 | `cd build/tests && ctest -R '^(ProjectRevisionRestoreStagingTest\|RevisionTimelineTest)$' --output-on-failure > …r1/logs/test-2-fixed.log 2>&1` | 0 (2/2 passed) |
| Windows-branch syntax, real headers, `-DQ_OS_WIN` | `c++ <flags from build/compile_commands.json> -DQ_OS_WIN -fsyntax-only <file>` for the test file and for `ProjectRevisions.cpp` | 1 both: Qt's own headers fail under a forced `Q_OS_WIN` on Linux (`qwindowdefs_win.h`, then `qassert.h`); not our code |
| Windows-branch syntax, guard rewritten | the same flags, on copies in `/tmp/p17-syntax/xform/` where only `#ifdef Q_OS_WIN` is changed to `#if 1` (`…r2/logs/syntax-xform-test.log`, `…/syntax-xform-product.log`) | 0 both, 0 errors |
| Build with the new test, round 2 | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p17 cmake --build build --target zene ProjectRevisionRestoreStagingTest RevisionTimelineTest -j4 > …r2/logs/build-4-test-edit.log 2>&1` | 0 |
| Tests after the test edit | `cd build/tests && ctest -R '^(ProjectRevisionRestoreStagingTest\|RevisionTimelineTest)$' --output-on-failure > …r2/logs/test-3-after-test-edit.log 2>&1` | 0 (2/2 passed: #184 RevisionTimelineTest, #185 ProjectRevisionRestoreStagingTest) |
| Gates, direct `feature_check.py` | `python3 feature_check.py <worktree>` | 1: `PermissionError` creating `…r2/logs` beside the worktree (`…r2/logs/gates-direct-r2.log`); no table |
| Gates, via `/tmp` wrapper | `python3 -I /tmp/p17-syntax/gate_wrap.py <worktree> > …r2/logs/gates-table-r2.log 2>&1` (loads the script unchanged, reuses its `SIMPLE`/`RATCHET` lists and comparison, logs to the run folder) | 0, RESULT PASS |

Gate table (`gates-table-r2.log`):

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

Affected family: 2 of 2 tests executed and passed on Linux. The Windows-only test is not in that count on Linux.

## Regression proof — still open, needs a Windows run

- **RED (must fail with the fix reverted): not run here.** The defect and the test both compile only under `Q_OS_WIN`. This host has no Windows cross-compiler; `wine` is installed but cannot build the product.
- **Step 4b (revert, fail, reapply, pass): not run here.** On Linux, a revert of the Windows branch changes nothing the tests execute.
- The Linux runs above (`test-1-baseline` EXIT 0, `test-3-after-test-edit` EXIT 0) show that Linux behaviour is unchanged. They do not show the fix works.

A Windows lane closes this with these steps, using the repo's own procedure:

1. `git diff -- src/core/ProjectRevisions.cpp > …/fix.patch && git apply -R …/fix.patch` (product file only; the test stays).
2. `cmake --build build --target ProjectRevisionRestoreStagingTest -j4 > …/build-red.log 2>&1; echo EXIT=$?`
3. `cd build/tests && ctest -R '^ProjectRevisionRestoreStagingTest$' --output-on-failure > …/test-red.log 2>&1; echo EXIT=$?` — expected: the new test fails at the `!refused` check in step 2 of the test. Record the EXIT and the failing assertion.
4. `git apply …/fix.patch`, rebuild, and run the same ctest. Expected: pass, with the test executed (not skipped). Record the EXIT.

If the test skips on the Windows machine, the code page holds U+1F3B5 and the defect cannot occur there; use a machine on a single-byte or DBCS ANSI code page.

## Out of scope, not verified

The same narrow-path pattern (`toLocal8Bit()` passed to a path-taking API) appears in `sf_open` calls at `src/core/BounceInPlace.cpp:528,577,598`, `src/core/ClipConsolidate.cpp:99`, `src/core/RenderQueue.cpp:89` and `src/core/audio/AudioFileFlac.cpp:75`. I did not check whether libsndfile treats those narrow paths as the ANSI code page on Windows. That needs its own cluster.

## Self-check

Pre-submit check run 1 (`submit-check-1.log`, round 1): PASS commits, PASS clean tree, PASS report, FAIL tests added or changed, FAIL gates (the `PermissionError` above, not a gate failure). Round 2 added the test; the pre-submit check for round 2 is the authority for the status row.

- ? **The regression test FAILS with the product change reverted and PASSES with it (exit codes).** Not answered by a run here. The test is Windows-only, and this host cannot build or run Windows code. The Linux evidence is `test-1-baseline` EXIT 0 and `test-3-after-test-edit` EXIT 0, which show no change on Linux. The RED and GREEN EXIT codes need the Windows run in "Regression proof", steps 2-4. The mechanism for why RED should fail is in "Regression test" above.
- ? **The test is deterministic.** It uses no wall clock, sleep, random value, thread or injected seam. Its inputs are a fixed directory name, a fixed project name and fixed document bytes, and it operates on real files in `QTemporaryDir`. The only environmental variable is the active ANSI code page, which decides the skip: skip is decided before any file is touched.
- ? **The fix is at the cause and complete.** The defect is the narrow-path conversion in `replaceWithStaged`, and it is the only one in the revision sources (see Evidence). Both callers are consistent with the fix: `copyRevision` (`:124-126`, reached from `shiftRevisionsUp` `:200` and `rotateProjectRevision` `:269`) and the project replacement (`:393-395`). The test covers all three paths: the copy into revision 0, the shift plus replacement, and the restore replacement. The other revision-file operations (`QFile::remove`, `QDir::entryList`, `QTemporaryFile`, `QFile::setPermissions`) take `QString` through Qt and are Unicode-safe on Windows.
- ? **No collateral damage.** The product change is inside `#ifdef Q_OS_WIN`; the POSIX branch is unchanged, and `test-1-baseline` and `test-3-after-test-edit` both pass with EXIT 0. The test is also inside `#ifdef Q_OS_WIN`, so Linux test behaviour is unchanged. No file format, saved-data layout, limit or check changed, so old projects and saved data load exactly as before. Windows ASCII names address the same files as before. Only names the ANSI code page cannot represent change behaviour, and those were failing before.
- Verdict is BLOCKED, not ALREADY-FIXED or FALSE-POSITIVE, so no "already fixed" citation is needed.

Round-2 completion check (`submit-check-r2.log`, EXIT 1): PASS commits on the branch, PASS working tree clean, PASS report, PASS tests added or changed (`ProjectRevisionRestoreStagingTest.cpp`). FAIL gates: the same `PermissionError` as before, from `feature_check.py` writing `zene-qwen-new-p17-133-feature-check/fork-sources.log` outside the write-jail (`gates-direct-r2b.log`, EXIT 1). The gate commands themselves pass through the wrapper (`gates-table-r2.log`, EXIT 0, RESULT PASS). The five `?` lines still print because the check lists them unconditionally; each is answered above with its evidence. The RED→GREEN row is answered "not run here" and stays open until the Windows run.

**Status:** the defect is real. The fix and a Windows-only regression test are committed. The RED→GREEN proof is open and needs one Windows run.
