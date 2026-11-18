Verdict: FIXED

# Cluster new-p16-76 - malformed tour steps launch as blank tours

## Finding and decision

The finding is real. Before the fix, `GuidedTour::start()` (`src/gui/HelpCenter.cpp:313`) accepted any file whose `steps` array was non-empty. A file holding `{"steps":[{}]}` therefore passed the check, and `showStep(0)` displayed an empty title and text. `GuidedTour::tours()` (`src/gui/HelpCenter.cpp:293`) also listed every `*.json` file in `data:/tours`, including files that are not JSON or have no steps. Those files appeared in the Help > Tours menu (`src/gui/HelpCenter.cpp:482`) and in `app.tours` (`src/gui/ControlCommandsHelp.cpp:88`).

Fix, at the cause: one validator now decides what a usable tour is, and both paths use it.

- `isUsableStep()` (`src/gui/HelpCenter.cpp:274`): a step is usable when it is a JSON object with a non-empty string `title` or `text`.
- `usableTour()` (`src/gui/HelpCenter.cpp:283`): a tour is usable when its `steps` is a non-empty array and every step is usable. Otherwise it returns an empty object.
- `tours()` (`src/gui/HelpCenter.cpp:301-305`): an unusable file is left out of the list and reported with `qWarning`.
- `start()` (`src/gui/HelpCenter.cpp:317-321`): an unusable file is refused with an error that names the tour, so `app.tour` returns `NotFound` with that message (`ControlCommandsHelp.cpp:94-96`).

Decisions I made:
- A file with any unusable step is rejected as a whole. Dropping only the bad step would make the tour skip a numbered step without telling anyone.
- "Reported" means a `qWarning` in `tours()` plus the error text from `start()`. The menu does not show invalid files as disabled entries. That would be a UI change beyond this bug.
- The error text for an unusable file changed from "has no steps: its file is empty or not valid JSON" to "is not usable: ... or has a step with no title or text". Nothing in `tests/` or `src/` matches the old text. Translations of the old string now fall back to English.

## Evidence

- Shipped tours are unaffected. I checked `data/tours/first-beat.json` (5 steps), `mixing.json` (4) and `recording.json` (4) with a parser: every step has a non-empty `title` and `text`. The `HelpAccessTest` run below loads them through the same validator.
- Callers and siblings checked: `GuidedTour::tours()` has two callers, the menu build (`HelpCenter.cpp:482`) and `app.tours` (`ControlCommandsHelp.cpp:88`). `GuidedTour::start()` has two callers, `app.tour` (`ControlCommandsHelp.cpp:94`) and the menu's `addCommandAction` (through `app.tour`). Both go through `usableTour()`. The `window == nullptr` path and the not-found path keep their messages.
- No control command was added or changed, so the commands snapshot and canary are not affected.
- Saved projects are not involved. Tours are shipped data in `data:/tours`.
- Realtime, undo: not applicable. `app.tour` is `NotMutating` (`src/core/ControlReversibilityTableDevices.cpp:121`), and the code is GUI-thread only.
- The header comments in `include/HelpCenter.h` now describe the usable-tours contract.

## Regression test

`tests/src/gui/HelpAccessTest.cpp:177-216`, `HelpAccessTest::aMalformedTourIsNotListedOrStarted()`.

It writes three files into a `QTemporaryDir`: `good.json` (a usable tour), `blank.json` (`{"title":"Blank","steps":[{}]}`) and `broken.json` (`{ not json`). It points the `data` search path at that directory, the same seam `ControlAutomationScriptTest` uses, and restores the old paths right after the calls. It checks that `tours()` lists only `good`, that `start()` refuses `blank` and `broken` with an error that names them, and that `start()` still starts `good`. It also uses `QTest::ignoreMessage` to require the two `qWarning` reports. Nothing sleeps or depends on timing.

## Commands and results

Logs are in `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/new-p16-76.r1/logs/`.

| Step | Command (run from the worktree or `build/tests`) | EXIT | Log |
|---|---|---|---|
| build attempt 1 | `cmake --build build --target HelpAccessTest -j4` | 2 | `build-1.log` (ccache could not write `/run/user/1000/ccache-tmp`, outside the write-jail; no code error) |
| build RED | `CCACHE_TEMPDIR=.../zene-tmp/ccache-tmp cmake --build build --target HelpAccessTest -j4` | 0 | `build-2.log` |
| **RED** | `cd build/tests && ctest -R '^HelpAccessTest$' --output-on-failure` (fix not applied) | **8** | `test-red-1.log` |

RED assertion: `FAIL! : HelpAccessTest::aMalformedTourIsNotListedOrStarted()`, `Actual (listed.size()): 3`, `Expected (1): 1`, `Loc: HelpAccessTest.cpp(209)`. The totals line is `12 passed, 1 failed`.

The test stops at the first failing `QCOMPARE`, so the `blank` and `broken` start assertions were not reached in RED. The 4b step below covers them: with the fix reverted, `start("blank")` returns a tour and the test fails earlier, and the fixed run exercises those lines.

| Step | Command | EXIT | Log |
|---|---|---|---|
| **GREEN** build | `cmake --build build --target zene HelpAccessTest -j4` (the header changed, so dependents rebuilt) | 0 | `build-green-1.log` |
| **GREEN** | `ctest -R '^HelpAccessTest$' --output-on-failure` | **0** | `test-green-1.log` |
| GREEN verbose | `ctest -R '^HelpAccessTest$' -V` | 0 | `test-green-2.log`: `PASS aMalformedTourIsNotListedOrStarted()`, `Totals: 13 passed, 0 failed, 0 skipped` |

Affected test family: `HelpAccessTest` is the only test that covers the tour code (`grep` over `tests/src` for `GuidedTour`, `app.tour` and `HelpCenter` finds only this file). It is one ctest entry with 13 test slots. All 13 pass.

### 4b: the test catches the bug

| Step | Command | EXIT | Log |
|---|---|---|---|
| save fix | `git diff -- include/HelpCenter.h src/gui/HelpCenter.cpp > .../logs/fix.patch` | 0 | `fix.patch` |
| revert fix | `git apply -R .../logs/fix.patch` | 0 | |
| rebuild, reverted | `cmake --build build --target zene HelpAccessTest -j4` | 0 | `build-4b-reverted.log` |
| **test, reverted** | `ctest -R '^HelpAccessTest$' --output-on-failure` | **8** | `test-4b-reverted.log`: `FAIL! ... listed.size()`, `Actual 3`, `Expected 1`, line 209 |
| reapply fix | `git apply .../logs/fix.patch` | 0 | |
| rebuild, fixed | `cmake --build build --target zene HelpAccessTest -j4` | 0 | `build-4b-fixed.log` |
| **test, fixed** | `ctest -R '^HelpAccessTest$' -V` | **0** | `test-4b-fixed.log`: `13 passed, 0 failed` |

The test fails without the fix and passes with it.

### Gates

The shared `feature_check.py` could not run here as written. It creates `<worktree>-feature-check/` next to the worktree (`feature_check.py` `main()`, `logs.mkdir`), and that directory is outside the write-jail. The run failed with `PermissionError`. I did not change the shared script. I ran a copy of it from `/tmp` with two lines changed: `ROOT` pinned to the real `qwen/` folder (for `FIX-BASE` and the cached base listings), and the log folder moved to `runs/new-p16-76.r1/logs/gates-feature-check/`. Every check and its logic are unchanged.

| Step | Command | EXIT | Log |
|---|---|---|---|
| gates (copy, logs redirected) | `python3 -I /tmp/fc-new-p16-76/feature_check.py <worktree>` | **0** | `gates-1.log` |

Table: fork-sources PASS, all-sources-reproduce PASS, no-upstream-regression PASS, check-namespace PASS, no-tautology PASS, doc-figures PASS, complexity PASS (no new item in changed files), file-length PASS (no new item in changed files). RESULT: PASS.

The shared script's own run stopped with `PermissionError` before any check ran (exit 1, traceback on stdout, not saved to a log).

Files committed: `include/HelpCenter.h`, `src/gui/HelpCenter.cpp`, `tests/src/gui/HelpAccessTest.cpp`, `docs/qwen-fixes-2026-10/cluster-new-p16-76.md`. No new files, so `tests/fork-sources.txt` and `tests/upstream-modifications.txt` did not need entries. The no-upstream-regression gate passed.

## Self-check

- **The new test FAILS with the product change reverted and PASSES with it.** Yes. Reverted: `test-4b-reverted.log`, EXIT 8, `listed.size()` 3 vs 1 at `HelpAccessTest.cpp:209`. Restored: `test-4b-fixed.log`, EXIT 0, 13 passed.
- **Every acceptance criterion is proven by a test I ran.** The criterion is "a malformed tour does not launch as a blank tour, and malformed files are not listed or reported as tours". Covered by `aMalformedTourIsNotListedOrStarted`: `blank` and `broken` cannot start (with an error naming them), they are not listed, and the `qWarning` reports are required by `ignoreMessage`. The `blank` and `broken` start assertions are only reached in the fixed and 4b runs; RED stopped at the earlier listing assertion (see above).
- **Deterministic.** Uses `QTemporaryDir`, a restored `data` search path and `ignoreMessage`. No sleeps, no timers, no wall-clock values.
- **Fix at the cause, siblings checked.** One validator (`usableTour`) is used by both `tours()` and `start()`. Callers listed under Evidence.
- **No collateral damage.** The three shipped tours validate (checked by parser). Saved projects are not involved. The only user-visible text change is the error message for an unusable tour; nothing matches the old text.
- **Audio thread.** Not applicable. The code runs on the GUI thread and is called only from the menu build and the control commands.
- **Undo.** Not applicable. `app.tour` is `NotMutating` (`ControlReversibilityTableDevices.cpp:121`) and does not change the project.
- **Sibling commands.** No command file changed. `app.tours` and `app.tour` both use the fixed functions, so their behaviour changes only for unusable files.
- **Portability.** Plain Qt 6 API (`QJsonValue`, `QJsonArray`, `std::all_of`, `qWarning`). `QTest::ignoreMessage` with `QRegularExpression` is used in the test; it compiled and ran on this Qt.
- **Verdict FIXED is supported.** The bug is reproduced (RED) and fixed (GREEN); the gates pass in the copy-run described above.
