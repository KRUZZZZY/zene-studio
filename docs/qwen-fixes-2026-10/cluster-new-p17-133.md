# Cluster new-p17-133: revision rotation on Windows with non-ASCII paths

**Verdict: BLOCKED** — the defect is real and the one-line fix is in, but the RED→GREEN regression proof cannot run on this Linux host. The defective statement is inside `#ifdef Q_OS_WIN`, and there is no Windows toolchain here to build or run it.

## Finding

Luna's report: on Windows, `replaceWithStaged` converts the staged and destination paths with `toLocal8Bit()` before calling `std::filesystem::rename`. The narrow overload converts through the active ANSI code page, so a path with characters that page cannot represent is mangled and the replacement fails. Revision rotation then cannot address the staged or destination file.

## Evidence (before the fix)

- `src/core/ProjectRevisions.cpp:68-71` (historical line 68 → 70 in the tree at this commit): the Windows branch was `std::filesystem::rename(staged.toLocal8Bit().constData(), to.toLocal8Bit().constData(), ec);`. This is the only narrow-path conversion in the revision code. `rg toLocal8Bit` over `ProjectRevisions.cpp`, `ControlCommandsRevisions.cpp`, `ControlReversibilityTableRevisions.cpp` and `RevisionTimeline*.cpp` returns nothing else.
- Both callers of `replaceWithStaged` take the fix together: `copyRevision` (`:124-126`, reached from the rotation shift at `:200` and the rotation at `:269`) and the project replacement at `:393-395`.
- The POSIX branch (`:75-78`) uses `QFile::encodeName` with `std::rename`, which is correct and unchanged.
- The repo's existing wide-path idiom: `src/core/DataFile.cpp:160-161` and `src/core/BounceInPlace.cpp:195` both build `std::filesystem::path(x.toStdWString())`. The fix uses the same idiom.

## Fix

`src/core/ProjectRevisions.cpp:68-74` (Windows branch only): the rename now receives `std::filesystem::path(staged.toStdWString())` and `std::filesystem::path(to.toStdWString())`, with a short comment on why the narrow overload is wrong. The POSIX branch, the test seam, the staging and the error handling are untouched.

## Commands and results

| Step | Command (run from the worktree or `build/tests` as noted) | EXIT |
| --- | --- | --- |
| Baseline build, first try | `cmake --build build --target ProjectRevisionRestoreStagingTest RevisionTimelineTest -j4 > …/build-1-baseline.log 2>&1` | 2 (ccache could not write to `/run/user/1000/ccache-tmp`; not a code error) |
| Baseline build, retry | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p17 cmake --build build --target ProjectRevisionRestoreStagingTest RevisionTimelineTest -j4 > …/build-2-baseline.log 2>&1` | 0 |
| Baseline tests (unmodified code) | `cd build/tests && ctest -R '^(ProjectRevisionRestoreStagingTest\|RevisionTimelineTest)$' --output-on-failure > …/test-1-baseline.log 2>&1` | 0 (2/2 passed: #184 RevisionTimelineTest, #185 ProjectRevisionRestoreStagingTest) |
| Standalone syntax check of the Windows-branch statement | `g++ -std=c++20 -fsyntax-only $(pkg-config --cflags Qt6Core) win_branch_check.cpp > …/syntax-1.log 2>&1` (file in `/tmp`, same statement as the fix) | 0 |
| Fixed build | `CCACHE_TEMPDIR=/tmp/ccache-tmp-p17 cmake --build build --target zene ProjectRevisionRestoreStagingTest RevisionTimelineTest -j4 > …/build-3-fixed.log 2>&1` | 0 |
| Fixed tests | `cd build/tests && ctest -R '^(ProjectRevisionRestoreStagingTest\|RevisionTimelineTest)$' --output-on-failure > …/test-2-fixed.log 2>&1` | 0 (2/2 passed) |
| Gates | `python3 feature_check.py <worktree>` exited with `PermissionError` (it creates `<worktree>-feature-check/` beside the worktree, outside the write-jail; no table produced). Re-run through a `/tmp` wrapper that loads the script unchanged and reuses its `SIMPLE`/`RATCHET` lists and comparison, with logs in the run folder: `python3 -I /tmp/p17-syntax/gate_wrap.py <worktree> > …/gates-table-1.log 2>&1` | 0 (RESULT: PASS) |

Gate table from the wrapper:

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

## Regression proof — not obtained here

- **RED (must fail before the fix): not run.** The defective statement compiles only when `Q_OS_WIN` is defined. This host has no `x86_64-w64-mingw32-g++`; `wine` is installed but cannot build the product. A Linux test cannot fail on this defect, because the POSIX branch was already correct.
- **Step 4b (revert, fail, reapply, pass): not run.** On Linux the revert would change nothing the tests exercise. The baseline run (`test-1-baseline`, EXIT 0) and the fixed run (`test-2-fixed`, EXIT 0) show that Linux behaviour is unchanged. That is evidence of no collateral damage, not proof that the fix works.
- **No test was added.** A Linux test that passes both before and after would not be regression proof. A Windows-only test could not be compiled or run here, so I did not commit one: an uncompiled test in the Windows CI matrix could break that build.

### What a Windows lane should add to close this

In `tests/src/core/ProjectRevisionRestoreStagingTest.cpp`, under `#ifdef Q_OS_WIN`: create a project in a directory whose name holds characters outside the test machine's ANSI code page (for example Cyrillic or CJK under a Western code page), call `rotateProjectRevision` on it, and assert that the `.rev0` file appears with the project's bytes. Pre-fix on that machine the rename should fail or hit the wrong path; post-fix it should pass. Record the RED and GREEN EXIT codes from that Windows run. The code page matters: under a UTF-8 ANSI code page the bug may not reproduce.

## Out of scope, not verified

The same narrow-path pattern (`toLocal8Bit()` passed to a path-taking API) appears in `sf_open` calls at `src/core/BounceInPlace.cpp:528,577,598`, `src/core/ClipConsolidate.cpp:99`, `src/core/RenderQueue.cpp:89` and `src/core/audio/AudioFileFlac.cpp:75`. I did not check whether libsndfile treats those narrow paths as the ANSI code page on Windows. That needs its own cluster.

## Self-check

`submit_check.py <worktree> new-p17-133` (log: `…/logs/submit-check-1.log`, EXIT 1, RESULT FAIL, 2 items):

- PASS commits on the branch (`fa5bd3f91`). PASS working tree clean. PASS report present.
- **FAIL tests added or changed.** Correct: no test was added or changed. The reason is in "Regression proof" above; an untested Windows-only test in the Windows CI matrix was judged riskier than the gap.
- **FAIL gates (feature_check.py).** Not a gate failure. `feature_check.py` exits 1 with `PermissionError` creating `zene-qwen-new-p17-133-feature-check/` beside the worktree, outside the write-jail (`…/gates-table-2-direct.log`). The same gate commands, run through the `/tmp` wrapper with logs in the run folder, all pass (`gates-table-1.log`, EXIT 0, RESULT PASS). The script's log location needs to be writable, or configurable, in this sandbox; I did not edit the shared script.
- ? **Regression test fails with the fix reverted and passes with it:** not answered by evidence. There is no test, and the RED/GREEN run needs a Windows build. Step 4b was not run: on Linux a revert changes nothing the tests exercise. The two Linux runs above (`test-1-baseline` EXIT 0 and `test-2-fixed` EXIT 0) show unchanged Linux behaviour, which is not proof of the fix.
- ? **Deterministic:** no new test. The two existing families (`ProjectRevisionRestoreStagingTest`, `RevisionTimelineTest`) pass before and after; I added no sleeps, timing or races.
- ? **Fix at the cause and complete:** the defective conversion is the only narrow-path call in the revision sources (`grep -c toLocal8Bit` over `ProjectRevisions.cpp`, `ControlCommandsRevisions.cpp`, `ControlReversibilityTableRevisions.cpp`, `RevisionTimeline*.cpp`: 0 in each after the fix; the one hit before was `:70`). Both `replaceWithStaged` callers are covered: `copyRevision` (`:124-126`, reached from `shiftRevisionsUp` `:200` and `rotateProjectRevision` `:269`) and the project replacement (`:393-395`). The POSIX branch (`:75-78`) was already correct and is unchanged.
- ? **No collateral damage:** the only change is inside `#ifdef Q_OS_WIN`. The Linux build and both families are unchanged (above). No file format, limit or check changed, so old projects and saved data load exactly as before. On Windows, ASCII names address the same files as before; only names that the ANSI code page cannot represent change behaviour.
- Verdict is BLOCKED, not ALREADY-FIXED or FALSE-POSITIVE, so no "already fixed" citation is needed.

**Status:** the defect is real and the fix is committed. The regression test and the RED/GREEN proof are still open. They need a Windows lane to add the test described above and run it on a Windows build, where the RED run should fail before the fix and pass after.
