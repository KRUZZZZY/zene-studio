# Cluster 5348: ALREADY-FIXED

## Verdict: ALREADY-FIXED

The finding is real as a defect, and it is fixed on this branch and on its base (`050/m0-train-9`, `b61e5f965`). The fix is commit `470ef45b7` ("fix(session): a saved launch quantisation fell back to a different grid"). Its ancestry was checked:

- `git merge-base --is-ancestor 470ef45b7 b61e5f965` → EXIT=0
- `git merge-base --is-ancestor 470ef45b7 bugs/qwen-5348` → EXIT=0

The finding's line 83 is now the comment block at `src/core/SessionModelPrivate.h:80-85`. The old `switch` that stopped at `FourBars` is gone.

## Evidence (current tree)

- `include/SessionModel.h:70-88`: `LaunchQuantisation` names `Global` (-1), `None`, `Bar`, `TwoBars`, `FourBars`, `Sixteenth` (-2), `Eighth` (-3), `Quarter` (-4), `Half` (-5) and `EightBars` (8).
- `src/core/SessionModelPrivate.h:78-102`: `launchQuantisationFromInt()` walks a `Grids[]` table that lists all nine non-`Global` enumerators. A value that is not a grid still falls back: `Global` when `allowGlobal`, otherwise `Bar`. Every enumerator is listed, so no grid can fall through.
- `src/core/SessionClip.cpp:178-179`: per-clip path, `launchQuantisationFromInt( ..., true )`.
- `src/core/SessionModel.cpp:53-59` (`restoreGlobalLaunchQuantisation`): session-wide path, `launchQuantisationFromInt( value, false )`, with a `QString::toInt( &ok )` check so a spoiled value takes `DefaultLaunchQuantisation`. Writers: `src/core/SessionModel.cpp:341` (legacy `<session>`), `:488` (`<z:scenes>`).

Both finding symptoms map to the enum values the table now covers. The 1/16 cell (`-2`) and the global EightBars (`8`) both round-trip.

## Regression tests

Existing, from `470ef45b7`:

- `tests/src/core/SessionModelTest.cpp:473-514` `everyLaunchQuantisationSurvivesAReload`: per-clip path, all nine grids plus `Global` and a junk value.

Added on this branch, because the existing test does not reach the session-wide path that the finding's "global EightBars" symptom goes through:

- `tests/src/core/SessionModelTest.cpp:516-550` `everyGlobalLaunchQuantisationSurvivesAReload`: saves each of the nine grids through `saveScenesSection()` and reloads with `restoreScenesSection()`, then compares `globalLaunchQuantisation()`.

No wall-clock, sleep or race. `tests/src/core/SessionModelTest.cpp` is an existing registered file, so no registration change is needed.

## Commands and exit codes

LOG FOLDER: `/home/kruzzzzy/.local/share/zene-bunny-fix/qwen/runs/5348.r1/logs`

Build environment note: the first build failed with `ccache: error: failed to create temporary file for /run/user/1000/ccache-tmp/...: Permission denied` (`build-1.log`, EXIT=2). Every later build set `CCACHE_TEMPDIR=/tmp/zene-5348-ccache-tmp`. That is an environment fix, not a code change.

| Step | Command | Log | EXIT |
| --- | --- | --- | --- |
| build (failed, ccache) | `cmake --build build --target zene SessionModelTest -j4` | build-1.log | 2 |
| build | `CCACHE_TEMPDIR=/tmp/zene-5348-ccache-tmp cmake --build build --target zene SessionModelTest -j4` | build-2.log | 0 |
| GREEN ctest | `cd build/tests && ctest -R '^SessionModelTest$' --output-on-failure` | test-green-1.log | 0 |
| GREEN binary | `cd build/tests && ./SessionModelTest -v2` | green-verbose-1.log | 0 (`Totals: 13 passed, 0 failed`) |
| make the RED state | `git show 470ef45b7 -- src/core/SessionModelPrivate.h > fix.patch` | fix.patch | 0 |
| make the RED state | `git apply -R fix.patch` | | 0 |
| RED build | `CCACHE_TEMPDIR=... cmake --build build --target zene SessionModelTest -j4` | build-red-3.log | 0 |
| RED ctest | `cd build/tests && ctest -R '^SessionModelTest$' --output-on-failure` | test-red-3.log | 8 |
| RED binary | `cd build/tests && ./SessionModelTest -v2` | red-verbose-3.log | 2 (`Totals: 11 passed, 2 failed`) |
| restore the fix | `git apply fix.patch` | | 0 |
| GREEN build | `CCACHE_TEMPDIR=... cmake --build build --target zene SessionModelTest -j4` | build-green-4.log | 0 |
| GREEN ctest | `cd build/tests && ctest -R '^SessionModelTest$' --output-on-failure` | test-green-4.log | 0 |
| GREEN binary | `cd build/tests && ./SessionModelTest -v2` | green-verbose-4.log | 0 |

RED failures, from `red-verbose-3.log`:

- `FAIL! : SessionModelTest::everyLaunchQuantisationSurvivesAReload()` at `tests/src/core/SessionModelTest.cpp:493`: `Actual -1`, `Expected -2` (Sixteenth on the clip path).
- `FAIL! : SessionModelTest::everyGlobalLaunchQuantisationSurvivesAReload()` at `tests/src/core/SessionModelTest.cpp:547`: `Actual 1`, `Expected -2` (Sixteenth on the session-wide path, which fell back to Bar).

GREEN (`green-verbose-4.log`): `PASS : ...everyLaunchQuantisationSurvivesAReload()` and `PASS : ...everyGlobalLaunchQuantisationSurvivesAReload()`, both in the 13-pass run. The restored tree equals the commit: `git status` shows only the test file modified.

Affected family, with the fix in place:

| Command | Log | EXIT | Result |
| --- | --- | --- | --- |
| `cd build/tests && ctest -R 'Session\|Scene\|ClipSlot'` | family-1.log | 8 | 4 passed (`SessionModelTest`, `ControlSessionLaunch`, `ControlSessionLifecycleTranscript`, `ControlSessionApiProof`); 26 `Not Run` because their binaries are not built in this tree |
| `CCACHE_TEMPDIR=... cmake --build build --target ScenesUpconvertTest SessionCheckpointTest SessionCheckpointBudgetTest -j4` | build-family-5.log | 0 | built |
| `cd build/tests && ctest -R '^(ScenesUpconvertTest\|SessionCheckpointTest\|SessionCheckpointBudgetTest\|SessionModelTest)$'` | test-family-5.log | 0 | `100% tests passed, 0 tests failed out of 4` |

Of the 26 unbuilt family entries, `ScenesUpconvertTest`, `SessionCheckpointTest` and `SessionCheckpointBudgetTest` are the ones that load saved launch quantisations, and they were built and run. The rest were not built, to keep within the -j4 and disk budget. Their binaries are absent, so they are not claimed as run.

## Gates

`python3 /home/kruzzzzy/.local/share/zene-bunny-fix/qwen/feature_check.py <worktree>`: EXIT=1, `feature-check-6.log`. It failed before any gate ran: `PermissionError: ... zene-qwen-5348-feature-check`. The script writes its logs to a sibling directory of the worktree, outside the write-jail. I did not edit the shared script. I ran each gate command it lists, from the worktree, with the logs in the LOG FOLDER:

| Gate | Command | Log | EXIT |
| --- | --- | --- | --- |
| fork-sources | `bash tests/fork-sources-gate.sh` | gate-fork-sources-7.log | 0 |
| all-sources-reproduce | `bash tests/all-sources-reproduce.sh` | gate-all-sources-7.log | 0 |
| no-upstream-regression | `bash tests/no-upstream-regression-gate.sh` | gate-no-upstream-7.log | 0 |
| check-namespace | `tests/scripted/check-namespace` | gate-check-namespace-7.log | 0 |
| no-tautology | `bash tests/no-tautology-gate.sh` | gate-no-tautology-7.log | 0 |
| doc-figures | `python3 tests/doc-figures-gate.py` | gate-doc-figures-7.log | 0 |
| complexity | `bash tests/complexity-gate.sh --check` | gate-complexity-7.log | 1 |
| file-length | `bash tests/file-length-gate.sh --check` | gate-file-length-7.log | 1 |

Complexity and file-length are red on the train. Their `REGRESSION:` items (`gate-complexity-7.log:96-100`, `gate-file-length-7.log:25-39`) name `XyPadPairTest.cpp`, `DawProjectReadTracks.cpp`, `ControlCommandsMixerTools.cpp`, `ControlCommandsMixerStrip.cpp`, `ControlCommandsShortcuts.cpp`, `TakeLane.cpp` and several test files. None of them is `src/core/SessionModelPrivate.h` or `tests/src/core/SessionModelTest.cpp`, the only files this bug touches. Nothing new sits in the files I changed, so the ratchets are not failed by this work. The base-listing comparison in `feature_check.py` was not run, because it needs a sibling repo worktree.

## Self-check

submit_check (`submit-check-8.log`, EXIT=1): PASS commits on the branch, PASS working tree clean, PASS report, PASS tests changed. FAIL report has a Self-check section (fixed by this section). FAIL gates (feature_check.py), see BLOCKED below.

**BLOCKED, gates item.** The gate cannot run here, for an environment reason, not a code reason. `feature_check.py` exited 1 with `PermissionError: ... zene-qwen-5348-feature-check` (`feature-check-6.log`, and again inside `submit-check-8.log`). The script writes its logs to a directory beside the worktree, which is outside the write-jail. I did not change the shared script, did not create that directory, and did not route around the permission. The six simple gates and the two ratchets were run directly (Gates table above): six exit 0, complexity and file-length exit 1 with nothing new in the files this bug touches. Needed from the owner: run `feature_check.py` from a session that may write beside the worktree, or allow that path.

Answers to the ASK items:

1. **RED/GREEN.** RED: with the fix reverted (`git apply -R fix.patch`), `test-red-3.log` EXIT=8 and `red-verbose-3.log` shows `FAIL!` at `tests/src/core/SessionModelTest.cpp:493` (clip path, `Actual -1, Expected -2`) and at `:547` (global path, `Actual 1, Expected -2`). GREEN: fix restored, `test-green-4.log` EXIT=0 and `green-verbose-4.log` EXIT=0, both functions `PASS`.
2. **Deterministic.** No clock, sleep, thread or race. Both tests use literal integers. The clip test parses an inline `QDomDocument` into `ClipSlot::restoreState`. The session test uses `saveScenesSection` and `restoreScenesSection` on an in-memory `QDomDocument` and compares an enum. Nothing depends on order or timing.
3. **Fix at the cause, every caller checked.** The fix in `470ef45b7` is the single conversion `launchQuantisationFromInt` (`src/core/SessionModelPrivate.h:78-102`). Its two callers both go through it: per-clip `src/core/SessionClip.cpp:178-179` (`allowGlobal=true`) and session-wide `src/core/SessionModel.cpp:53-59` (`allowGlobal=false`). Writers and readers use the same integer: `SessionClip.cpp:127` writes `static_cast<int>`, and `SessionModel.cpp:341` and `:488` write the same way. No sibling of this function was changed in this bug. Not changed, and outside this finding: `SessionClip.cpp:178` reads `quantisation` with `toInt()` and no success check, so a non-integer string becomes 0, which is None. That is a different defect (a non-grid string, not a missing grid), and it is already noted in `docs/qwen-fixes-2026-10/cluster-new-p12-233.md`. It should be its own cluster. I did not change it here.
4. **No collateral.** Values that already mapped correctly (None, Bar, TwoBars, FourBars, and Global with `allowGlobal`) keep their meaning. Junk values still fall back: `SessionModelTest.cpp:506-513` checks `99` → Global. The enum values and the saved integers are unchanged, so existing project files load as before. No limit or check was added. Family run with the fix in place: `ScenesUpconvertTest`, `SessionCheckpointTest`, `SessionCheckpointBudgetTest` and `SessionModelTest` all pass (`test-family-5.log`, EXIT=0). The other 26 family entries were not built and are not claimed.
5. **ALREADY-FIXED evidence.** The fix is `470ef45b7`, which is an ancestor of this branch and of its base `b61e5f965`, both EXIT=0 (see Verdict). Its code is at `src/core/SessionModelPrivate.h:78-102`, and the clip and session call sites are at `src/core/SessionClip.cpp:178` and `src/core/SessionModel.cpp:58`. The global-path test added here at `tests/src/core/SessionModelTest.cpp:516-550` closes the gap the existing test left.
