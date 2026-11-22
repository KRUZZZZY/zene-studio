FIXED

# cluster ci-d1d791015ce10efd: CI regression, build at ebf644847a1b

## Verdict: REAL, test-source build defect (not product code, not infrastructure)

Hosted `linux-arm64` stopped compiling `tests/src/core/UpdateCheckTest.cpp`:

    UpdateCheckTest.cpp:394:108: error: ignoring return value of 'bool QTest::qWaitFor(Functor, int)',
    declared with attribute 'nodiscard' [-Werror=unused-result]

Evidence on this branch (HEAD 4b6e2af151, which contains ebf644847a1b: `git merge-base --is-ancestor ebf644847a1b HEAD` EXIT=0):
- `tests/src/core/UpdateCheckTest.cpp:394` (before the fix) called `QTest::qWaitFor([&] { return heard; }, 30000);` as a bare statement in `checkAndWait()`.
- Qt declares the function nodiscard: `/usr/include/qt6/QtCore/qtestsupport_core.h:78` `[[nodiscard]] bool qWaitFor(Functor predicate, int timeout)` (Qt 5 on the runner: `qtestsupport_core.h:55` `Q_REQUIRED_RESULT`).
- The hosted job builds with `-Werror`; this box's build tree has `-Wno-error=unused-result` in `CMAKE_CXX_FLAGS` (build/CMakeCache.txt), which is why it compiled locally.
- Sibling search: `rg -n "(^\s*|[{;]\s*)(QTest::)?qWaitFor\(" tests src plugins` found only this one discarded call.

## Fix
`tests/src/core/UpdateCheckTest.cpp` `checkAndWait()`: the qWaitFor result is now used; if the update check never reports within 30 s the test records `QTest::qFail(...)` instead of silently returning a default `Outcome`. When `check()` returns `Start::Off` nothing is waited on, as before. No product code, no assertion weakened (a timeout is now an explicit failure, which is stricter).

## Regression test
`tests/qwaitfor-result-check.py`, registered as ctest `QWaitForResultCheck` (tests/CMakeLists.txt, after UnusedResultFortifyCheck). It recompiles the real `UpdateCheckTest.cpp` with its compile_commands.json flags, minus `-Werror` and the unused-result relaxations, plus `-Werror=unused-result`. Two fixtures under the same flags prove a discarded qWaitFor is rejected and a used one compiles, so a green run cannot be vacuous. Deterministic: compile only, no timing.

Registration: `tests/fork-sources.txt` (new script), `tests/upstream-modifications.txt` (tests/CMakeLists.txt reason).

## Commands and exit codes (logs in /home/kruzzzzy/.config/kads/zene/state/runs/ci-d1d791015ce10efd.r1/logs)
- `cmake build` → refused by the lane guard ("configuring is the parent's job"), EXIT=97 (01-configure.log); `cmake --build` regenerated instead.
- RED: `cmake --build build --target zene UpdateCheckTest -j4` EXIT=0 (03); `ctest -R '^QWaitForResultCheck$' --output-on-failure` EXIT=8 (04-test-red.log, shows the same `UpdateCheckTest.cpp:394:108 ... [-Werror=unused-result]` as CI).
- GREEN: build EXIT=0 (05); `ctest -R '^(QWaitForResultCheck|UpdateCheck)'` EXIT=8 with UpdateCheckWindowsTest/UpdateCheckHiddenTest "Not Run" (executables not built yet, 06); built them EXIT=0 (07); rerun EXIT=0, 4/4 passed (08-test-family.log).
- 4b: `git diff -- tests/src/core/UpdateCheckTest.cpp > logs/fix.patch && git apply -R logs/fix.patch` EXIT=0; rebuild EXIT=0 (09); `ctest -R '^QWaitForResultCheck$'` EXIT=8 (10-test-reverted.log); `git apply logs/fix.patch` EXIT=0; rebuild EXIT=0 (11); `ctest -R '^(QWaitForResultCheck|UpdateCheckTest)$'` EXIT=0, 2/2 passed (12-test-reapplied.log).
- Gates: `python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>` EXIT=0, RESULT: PASS, all 8 gates PASS (13-feature-check.log).

## Notes
- The arm64/Qt 5 runner was not reproduced on its own hardware; the local check reproduces the identical diagnostic with Qt 6, where qWaitFor is also `[[nodiscard]]`.
- No audio-thread, undo, control-command or saved-data impact: only a test source and test registration changed.
- No GUI product change, so no screenshot artifact.

## Other findings
- None.

## Self-check
- Red/green: with the fix reverted (`git apply -R logs/fix.patch`, EXIT=0, rebuild EXIT=0) `ctest -R '^QWaitForResultCheck$'` EXIT=8 (10-test-reverted.log, shows `UpdateCheckTest.cpp:394:108 ... [-Werror=unused-result]`); reapplied (`git apply logs/fix.patch`, EXIT=0, rebuild EXIT=0) `ctest -R '^(QWaitForResultCheck|UpdateCheckTest)$'` EXIT=0 (12-test-reapplied.log).
- Deterministic: the regression is a compile of the real translation unit with fixed flags from compile_commands.json plus `-Werror=unused-result`; no timing, sleeps or threads. Its bad/good fixtures prove the flags can fail and are not over-strict.
- At the cause and complete: the cause is the one discarded `QTest::qWaitFor` call (`checkAndWait()`, UpdateCheckTest.cpp:394). All six callers of `checkAndWait()` (UpdateCheckTest.cpp:163, 188, 221, 228, 287, 315) get the same Outcome as before when the check reports; a 30 s timeout now records an explicit failure instead of a silent default Outcome. A repo-wide search (`rg -n "(^\s*|[{;]\s*)(QTest::)?qWaitFor\(" tests src plugins`) found no other discarded qWaitFor. UpdateCheckTest, UpdateCheckWindowsTest, UpdateCheckHiddenTest all pass (08-test-family.log, 4/4).
- No collateral damage: only a test source, a new test script and test registration changed; no product code, file format, saved data or limits touched. Gates PASS (13-feature-check.log).
- ALREADY-FIXED / FALSE-POSITIVE: not applicable (verdict FIXED; the defect reproduced on this branch, 04-test-red.log).
- Sibling commands (app.about, app.ask, app.help, app.online_help, app.task_cancel, app.tasks, app.tour, app.tours, app.update_check_get, app.update_check_run, app.update_check_set, app.version, app.whats_new, arrangement.fit_tracks, arrangement.get_state): not affected. No command file and no command handler changed; the edit is inside the test's private `checkAndWait()` helper, and the test's command-presence assertions (`CommandIds`, UpdateCheckTest.cpp:37-38) are untouched and still pass.
